#include "ProjectManager.h"
#include "SchemePackage.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "Connection.h"
#include "Port.h"
#include "NodeRegistry.h"
#include "GlobalVariableManager.h"
#include "GlobalCameraManager.h"
#include "CommunicationManager.h"
#include "GlobalTriggerManager.h"
#include "HeartbeatManager.h"
#include "CalibrationManager.h"
#include "AppLog.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QFileDialog>
#include <QMessageBox>
#include <QWidget>

namespace {
/// 运行界面布局文件（与 RuntimeInterfaceDesigner::defaultLayoutPath 一致）
QString runtimeLayoutPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/runtime_interface.json");
}

/// 方案文件结构版本。1 = 早期无 typeId 的版本；2 = 写入节点注册表 typeId（可自描述）。
constexpr int kProjectSchemaVersion = 2;
const QString kTypeIdKey = QStringLiteral("typeId");
}

using MyConnection = MyProject::Connection;

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
{
}

bool ProjectManager::saveProjectInteractive(QWidget *parent, const QList<FlowScene *> &scenes,
                                            QString *savedPath)
{
    // 方案扩展名 .vfp，与需求文档一致
    QString fileName = QFileDialog::getSaveFileName(parent, tr("保存项目"), QString(),
                                                    tr("方案文件 (*.vfp)"));
    if (fileName.isEmpty())
        return false;   // 用户取消
    if (!fileName.endsWith(QStringLiteral(".vfp"), Qt::CaseInsensitive)) {
        fileName += QStringLiteral(".vfp");
    }

    // 只读方案：禁止覆盖原分发文件（G-P1-10）。允许另选路径另存为新方案（写后自动解除只读）。
    if (m_loadedReadonly && !m_lastFilePath.isEmpty() &&
        QFileInfo(fileName).absoluteFilePath().compare(
            QFileInfo(m_lastFilePath).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        QMessageBox::warning(parent, tr("只读方案"),
            tr("当前方案为只读，禁止覆盖原文件。如需修改，请另选路径保存为新方案。"));
        if (savedPath)
            *savedPath = QString();
        return false;
    }

    if (savedPath)
        *savedPath = fileName;

    const bool success = saveProject(fileName, scenes);
    if (success) {
        // FR3.3 保存确认：仅写日志不足以让操作员确认保存结果，需显式提示
        QMessageBox::information(parent, tr("保存方案"), tr("方案已保存:\n%1").arg(fileName));
    } else {
        QMessageBox::warning(parent, tr("保存方案"),
                             tr("方案保存失败，请确认目标路径可写后重试:\n%1").arg(fileName));
    }
    return success;
}

QString ProjectManager::askOpenProjectPath(QWidget *parent)
{
    return QFileDialog::getOpenFileName(parent, tr("加载项目"), QString(),
                                        tr("方案文件 (*.vfp)"));
}

ProjectManager::~ProjectManager()
{
}

QString ProjectManager::annotationDir() const
{
    if (m_lastFilePath.isEmpty())
        return QCoreApplication::applicationDirPath() + QStringLiteral("/annotations");
    return QFileInfo(m_lastFilePath).absolutePath() + QStringLiteral("/annotations");
}

bool ProjectManager::saveProject(const QString &filePath, const QList<FlowScene *> &scenes)
{
    m_lastFilePath = filePath;
    m_loadedReadonly = false; // 明文保存即为可编辑方案

    // 序列化与自动保存共用同一实现（buildProjectJson）：两套实现一旦分叉，表现是
    // "崩溃恢复出来的方案缺东西 / 与手动保存不一致"——极难发现，故此处不留第二份拷贝。
    const QJsonObject root = buildProjectJson(scenes);

    QJsonDocument doc(root);
    // 原子写（QSaveFile）：先写临时文件，commit 成功才改名到目标。
    // 历史实现直写目标文件——写入中途崩溃/断电会留下被截断的方案（现场"方案打不开"）；
    // 字节数校验保留，并保留写失败时不破坏旧文件的能力。
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        VFP_DEBUG << "方案保存失败：无法写入" << filePath << file.errorString();
        return false;
    }

    const QByteArray payload = doc.toJson();
    const qint64 written = file.write(payload);
    if (written != payload.size()) {
        VFP_DEBUG << "方案保存不完整:" << written << "/" << payload.size()
                  << " error:" << file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        VFP_DEBUG << "方案保存失败（commit）:" << filePath << file.errorString();
        return false;
    }

    return true;
}

QJsonObject ProjectManager::buildProjectJson(const QList<FlowScene *> &scenes) const
{
    QJsonObject root;
    QJsonArray sceneArray;
    for (FlowScene *scene : scenes) {
        if (!scene)
            continue;   // 防御：调用方列表里可能短暂含空指针（加载/恢复中途失败的残留）
        sceneArray.append(sceneToJson(scene));
    }
    root[QStringLiteral("scenes")] = sceneArray;

    // 全局配置随项目文件一起保存（保证保存/重开不丢失）
    root[QStringLiteral("globalVariables")] = GlobalVariableManager::instance()->toJson();
    root[QStringLiteral("cameras")] = GlobalCameraManager::instance()->toJson();
    root[QStringLiteral("communication")] = CommunicationManager::instance()->toJson();
    root[QStringLiteral("globalTriggers")] = GlobalTriggerManager::instance()->toJson();
    root[QStringLiteral("heartbeat")] = HeartbeatManager::instance()->toJson();
    root[QStringLiteral("calibrations")] = CalibrationManager::instance()->toJson();
    if (!m_lastFilePath.isEmpty())
        root[QStringLiteral("projectPath")] = m_lastFilePath;

    // 运行界面布局随方案保存（独立文件换机丢失/换方案串用的问题）
    {
        QFile layoutFile(runtimeLayoutPath());
        if (layoutFile.exists() && layoutFile.open(QIODevice::ReadOnly)) {
            root[QStringLiteral("runtimeLayout")] = QString::fromUtf8(layoutFile.readAll());
            layoutFile.close();
        }
    }

    root[QStringLiteral("schemaVersion")] = kProjectSchemaVersion;
    return root;
}

bool ProjectManager::loadProject(const QString &filePath, QList<FlowScene *> &scenes,
                                 const QString &passphrase)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        VFP_DEBUG << "方案加载失败：无法读取" << filePath << file.errorString();
        return false;
    }

    const QByteArray raw = file.readAll();
    file.close();

    m_lastFilePath = filePath;
    m_loadedReadonly = false;

    // 信封方案（加密 / 只读打包）：解密 → 解析 JSON → 应用；老明文 .vfp 走原路径。
    if (SchemePackage::isEnvelope(raw)) {
        bool readonly = false, ok = false;
        const QByteArray json = SchemePackage::importScheme(raw, passphrase, &readonly, &ok);
        if (!ok) {
            VFP_DEBUG << "方案加载失败：信封解析/解密失败（口令错误或文件损坏）" << filePath;
            return false; // 不改动 scenes，调用方可以保留当前方案
        }
        QJsonParseError parseError{};
        QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            VFP_DEBUG << "方案加载失败：信封内 JSON 非法，偏移" << parseError.offset;
            return false;
        }
        m_loadedReadonly = readonly;
        return applyProjectJson(doc.object(), scenes);
    }

    // 原实现未校验解析结果：文件损坏时 doc.object() 为空、循环不执行，
    // 仍返回 true 报“加载成功”，而调用方已销毁原方案 → 数据静默丢失。
    QJsonParseError parseError{};
    QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        VFP_DEBUG << "方案加载失败：文件不是合法 JSON，偏移" << parseError.offset
                  << parseError.errorString();
        return false; // 不改动 scenes，调用方可以保留当前方案
    }

    // 应用方案内容（与崩溃恢复共用 applyProjectJson：场景 + 全局配置 + 运行界面布局）
    return applyProjectJson(doc.object(), scenes);
}

bool ProjectManager::exportEncryptedProject(const QString &filePath,
                                           const QList<FlowScene *> &scenes,
                                           const QString &passphrase, bool readonly)
{
    m_lastFilePath = filePath;

    const QJsonObject root = buildProjectJson(scenes);
    const QByteArray json = QJsonDocument(root).toJson();
    const QByteArray envelope = SchemePackage::exportScheme(json, passphrase, readonly);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        VFP_DEBUG << "加密方案导出失败：无法写入" << filePath << file.errorString();
        return false;
    }
    if (file.write(envelope) != envelope.size()) {
        file.cancelWriting();
        VFP_DEBUG << "加密方案导出不完整" << filePath;
        return false;
    }
    if (!file.commit()) {
        VFP_DEBUG << "加密方案导出失败（commit）" << filePath << file.errorString();
        return false;
    }
    return true;
}

bool ProjectManager::fileNeedsPassphrase(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(64); // 足够容纳 magic(4)+version(1)+flags(1)
    file.close();
    return SchemePackage::isEncryptedEnvelope(head);
}

bool ProjectManager::applyProjectJson(const QJsonObject &root, QList<FlowScene *> &scenes)
{
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(1);
    if (schemaVersion > kProjectSchemaVersion) {
        VFP_DEBUG << "警告：方案文件版本" << schemaVersion << "高于本程序支持的版本"
                  << kProjectSchemaVersion << "，可能存在无法识别的算子，请勿直接覆盖保存";
    }

    QJsonArray sceneArray = root[QStringLiteral("scenes")].toArray();
    if (sceneArray.isEmpty()) {
        VFP_DEBUG << "方案加载失败：文件中没有任何流程";
        return false;
    }
    
    for (int i = 0; i < sceneArray.size(); ++i) {
        QJsonObject sceneJson = sceneArray[i].toObject();
        FlowScene *scene = new FlowScene();
        sceneFromJson(sceneJson, scene);
        scenes.append(scene);
    }

    // 恢复全局配置（兼容旧项目文件：缺失字段时跳过）
    if (root.contains(QStringLiteral("globalVariables")))
        GlobalVariableManager::instance()->fromJson(root[QStringLiteral("globalVariables")].toObject());
    if (root.contains(QStringLiteral("cameras")))
        GlobalCameraManager::instance()->fromJson(root[QStringLiteral("cameras")].toObject());
    if (root.contains(QStringLiteral("communication")))
        CommunicationManager::instance()->fromJson(root[QStringLiteral("communication")].toObject());
    if (root.contains(QStringLiteral("globalTriggers")))
        GlobalTriggerManager::instance()->fromJson(root[QStringLiteral("globalTriggers")].toObject());
    if (root.contains(QStringLiteral("heartbeat")))
        HeartbeatManager::instance()->fromJson(root[QStringLiteral("heartbeat")].toObject());
    if (root.contains(QStringLiteral("calibrations")))
        CalibrationManager::instance()->fromJson(root[QStringLiteral("calibrations")].toObject());

    // 恢复运行界面布局：校验后原子写回布局文件，运行界面加载时自动生效
    if (root.contains(QStringLiteral("runtimeLayout"))) {
        const QString layout = root[QStringLiteral("runtimeLayout")].toString();
        if (!layout.isEmpty()) {
            // 校验必须是合法 JSON 对象再落盘：损坏/伪造内容不得覆盖全局布局文件
            // （历史实现不校验直写——坏数据会把运行界面变成空窗/乱版）
            const QJsonDocument layoutDoc = QJsonDocument::fromJson(layout.toUtf8());
            if (layoutDoc.isObject()) {
                QSaveFile layoutFile(runtimeLayoutPath());
                if (layoutFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    layoutFile.write(layout.toUtf8());
                    layoutFile.commit();
                }
            } else {
                VFP_DEBUG << "方案内 runtimeLayout 不是合法 JSON 对象，已忽略（不覆盖现有布局）";
            }
        }
    }

    return true;
}

QJsonObject ProjectManager::sceneToJson(FlowScene *scene) const
{
    QJsonObject sceneJson;
    QJsonArray nodesArray;
    QJsonArray connectionsArray;
    
    // Save nodes with unique IDs
    QMap<NodeBase *, int> nodeIds;
    int nodeId = 0;
    for (NodeBase *node : scene->nodes()) {
        QJsonObject nodeJson = node->toJson();
        nodeJson["id"] = nodeId;
        nodeJson["moduleId"] = node->moduleId();
        nodeJson["executionSuccess"] = node->executionSuccess();
        nodeJson["nodeType"] = node->type();
        nodeJson["nodeName"] = node->name();

        // 写入注册表类型 ID，使方案文件自描述。
        // 仅靠 nodeName（显示名）重建节点时，显示名被改名或被另一实现复用时
        // 会静默换成别的算子（参数名不同 → 参数丢失、按默认值运行）。
        const QString typeId = node->property("vfpNodeTypeId").toString();
        if (!typeId.isEmpty())
            nodeJson[kTypeIdKey] = typeId;

        // 兜底补齐位置/大小：个别节点历史上未链接基类 toJson（如 HalconImageSourceNode），
        // 会丢失 position/size 导致重载后全部堆到 (0,0)。
        if (!nodeJson.contains(QStringLiteral("position"))) {
            nodeJson["position"] = QJsonObject({
                {"x", node->position().x()},
                {"y", node->position().y()}
            });
        }
        if (!nodeJson.contains(QStringLiteral("size"))) {
            nodeJson["size"] = QJsonObject({
                {"width", node->size().width()},
                {"height", node->size().height()}
            });
        }

        
        // 保存输入端口信息
        QJsonArray inputPortsArray;
        for (Port *port : node->inputPorts()) {
            QJsonObject portJson;
            portJson["name"] = port->name();
            portJson["isConnected"] = port->isConnected();
            inputPortsArray.append(portJson);
        }
        nodeJson["inputPorts"] = inputPortsArray;
        
        // 保存输出端口信息
        QJsonArray outputPortsArray;
        for (Port *port : node->outputPorts()) {
            QJsonObject portJson;
            portJson["name"] = port->name();
            portJson["isConnected"] = port->isConnected();
            outputPortsArray.append(portJson);
        }
        nodeJson["outputPorts"] = outputPortsArray;
        
        nodesArray.append(nodeJson);
        nodeIds[node] = nodeId;
        nodeId++;
    }
    
    // Save connections
    for (MyConnection *conn : scene->connections()) {
        QJsonObject connJson;
        Port *sourcePort = conn->sourcePort();
        Port *targetPort = conn->targetPort();
        
        if (sourcePort && targetPort) {
            NodeBase *sourceNode = sourcePort->node();
            NodeBase *targetNode = targetPort->node();
            
            if (nodeIds.contains(sourceNode) && nodeIds.contains(targetNode)) {
                connJson["sourceNode"] = nodeIds[sourceNode];
                connJson["sourcePortIndex"] = sourceNode->outputPorts().indexOf(sourcePort);
                connJson["targetNode"] = nodeIds[targetNode];
                connJson["targetPortIndex"] = targetNode->inputPorts().indexOf(targetPort);
                connectionsArray.append(connJson);
            }
        }
    }
    
    sceneJson["nodes"] = nodesArray;
    sceneJson["connections"] = connectionsArray;
    const QJsonObject extras = scene->extrasToJson();
    for (auto it = extras.constBegin(); it != extras.constEnd(); ++it)
        sceneJson[it.key()] = it.value();
    
    return sceneJson;
}

void ProjectManager::sceneFromJson(const QJsonObject &json, FlowScene *scene)
{
    // Clear existing scene
    scene->clearScene();
    
    // Load nodes
    QJsonArray nodesArray = json["nodes"].toArray();
    QMap<int, NodeBase *> nodeMap;
    QSet<int> usedModuleIds;   ///< 已占用的模块号（防"同一个号两个算子"的脏文件）
    
    for (int i = 0; i < nodesArray.size(); ++i) {
        QJsonObject nodeJson = nodesArray[i].toObject();
        
        // 使用保存的节点类型和名称创建节点
        NodeBase::NodeType type = static_cast<NodeBase::NodeType>(nodeJson["nodeType"].toInt());
        QString nodeName = nodeJson["nodeName"].toString();
        QPointF pos(nodeJson["position"].toObject()["x"].toDouble(),
                    nodeJson["position"].toObject()["y"].toDouble());
        
        // 使用nodeName创建节点，确保创建正确的节点类型
        NodeBase *node = scene->createNode(type, pos, nodeName);
        if (node) {
            // 算子类型一致性校验：识别「算子已下线被降级成通用算子」与「显示名被另一实现复用」
            // 两种情况。原实现为静默降级，算法直接消失或换成另一套实现而用户毫无察觉。
            const QString wantTypeId = nodeJson[kTypeIdKey].toString();
            const QString gotTypeId = node->property("vfpNodeTypeId").toString();
            if (gotTypeId.isEmpty()) {
                VFP_DEBUG << "警告：方案中的算子未注册，已降级为通用算子（算法已丢失）:" << nodeName
                          << " 期望类型:"
                          << (wantTypeId.isEmpty() ? QStringLiteral("(旧版本方案未记录)") : wantTypeId);
            } else if (!wantTypeId.isEmpty() && gotTypeId != wantTypeId) {
                VFP_DEBUG << "警告：方案算子类型不匹配，已按当前实现加载:" << nodeName
                          << " 期望:" << wantTypeId << " 实际:" << gotTypeId
                          << "（参数可能不兼容，请核对该算子配置）";
            }

            node->fromJson(nodeJson);

            // 恢复模块号（节点身份）：{模块号.参数名} 引用、结果表键、配方键都按模块号索引，
            // 若加载后按"当前发号顺序"重发，旧方案里的引用会静默指向**另一个**算子。
            // 历史实现从不恢复模块号 ⇒ 号按文件里节点的出现顺序重发，而该顺序来自
            // FlowScene::nodes()（按指针地址排序），与创建顺序不保证一致（删过节点/地址复用即错位），
            // 于是跨模块引用会整体错位——表面正常、结果错误。
            // 防御：文件里出现重复/非法模块号时保留新发的号（宁可该引用失效，也不能一号两算子）。
            const int savedModuleId = nodeJson[QStringLiteral("moduleId")].toInt(0);
            if (savedModuleId > 0 && !usedModuleIds.contains(savedModuleId)) {
                node->setModuleId(savedModuleId);
            } else if (savedModuleId > 0) {
                VFP_DEBUG << "警告：方案中出现重复模块号，保留本算子的新号:" << savedModuleId
                          << nodeName;
            }
            // 双向唯一性：上面的"跳过恢复"只保证不重复占用**文件里**的号，但这个算子当前拿到的
            // 新号本身可能已被前面某个算子**恢复**成同一个值（实测踩到：两节点都成了 32）。
            // 模块号是执行器缓存与 {模块号.参数名} 引用的索引键，"一号两算子"会互相污染，
            // 故这里必须再确认一次，撞了就重新发号（宁可该引用失效，也不能一号两算子）。
            if (usedModuleIds.contains(node->moduleId())) {
                const int fresh = NodeBase::allocateModuleId();
                VFP_DEBUG << "警告：模块号冲突（可能来自重复号的文件），已重新发号:"
                          << node->moduleId() << "->" << fresh << nodeName;
                node->setModuleId(fresh);
            }
            usedModuleIds.insert(node->moduleId());

            int nodeId = nodeJson["id"].toInt();
            nodeMap[nodeId] = node;
            
            // 恢复模块状态
            // 注意：executionSuccess是运行时状态，不应该从文件恢复
            // 但是我们需要确保节点有正确的输入输出端口配置
        }
    }
    
    // Load connections
    QJsonArray connectionsArray = json["connections"].toArray();
    for (int i = 0; i < connectionsArray.size(); ++i) {
        QJsonObject connJson = connectionsArray[i].toObject();
        int sourceNodeId = connJson["sourceNode"].toInt();
        int sourcePortIndex = connJson["sourcePortIndex"].toInt();
        int targetNodeId = connJson["targetNode"].toInt();
        int targetPortIndex = connJson["targetPortIndex"].toInt();
        
        if (nodeMap.contains(sourceNodeId) && nodeMap.contains(targetNodeId)) {
            NodeBase *sourceNode = nodeMap[sourceNodeId];
            NodeBase *targetNode = nodeMap[targetNodeId];
            
            if (sourcePortIndex >= 0 && sourcePortIndex < sourceNode->outputPorts().size() &&
                targetPortIndex >= 0 && targetPortIndex < targetNode->inputPorts().size()) {
                Port *sourcePort = sourceNode->outputPorts()[sourcePortIndex];
                Port *targetPort = targetNode->inputPorts()[targetPortIndex];
                
                scene->createConnection(sourcePort, targetPort, true);
            }
        }
    }

    scene->extrasFromJson(json);
}
