#include "HalconNode.h"
#include <QBuffer>
#include <QByteArray>
#include <QIODevice>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QElapsedTimer>
#include <QTimer>
#include <QFileDialog>
#include <QTimer>
#include <QSignalBlocker>
#include "DataObject.h"
#include "Port.h"
#include "Connection.h"
#include "FlowScene.h"
#include "AppLog.h"
#include "GlobalVariableManager.h"
#include <QMenu>

HalconNode::HalconNode(QObject *parent)
    : NodeBase(parent)
{
    m_type = IMAGE_PROCESSING;

    // 初始化实时预览定时器
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(PREVIEW_DELAY_MS);
    connect(m_previewTimer, &QTimer::timeout, this, &HalconNode::executePreview);
}

HalconNode::~HalconNode()
{
    m_previewTimer->stop();
    m_inputImage.Clear();
    m_outputImage.Clear();
}

void HalconNode::init()
{
    // HalconNode是非图像获取算子，必须有输入图像参数
    addInputPort(QStringLiteral("输入图像"));

    // 添加输出端口（一个红点代表所有输出参数）
    addOutputPort(QStringLiteral("输出图像"));
    
    // 初始化默认参数
    m_params["moduleStatus"] = false; // 模块状态（bool量）
}

void HalconNode::run(bool autoSwitch)
{
    // Default implementation - just pass through the image
    m_outputImage = m_inputImage;
}

void HalconNode::setParam(const QString &name, const QVariant &value)
{
    QVariant normalized = value;

    // 按 ParamSpec 声明的数值范围钳制越界值（仅 Int/Double 且显式声明了 hasRange）。
    // 说明：早期只有 fromJson 会做范围校验，程序化 setParam（参数面板、通讯下发、
    // 节点内部写参）可以写入 300 这类越界阈值直达算法，造成不可预期的结果。
    // 此处只收窄数值，不改类型、不改枚举语义，避免影响既有节点行为。
    for (const ParamSpec &spec : m_paramSpecs) {
        if (spec.name != name || !spec.hasRange)
            continue;
        if (spec.type == ParamType::Int) {
            bool ok = false;
            const int intValue = value.toInt(&ok);
            if (ok)
                normalized = qBound(spec.minValue.toInt(), intValue, spec.maxValue.toInt());
        } else if (spec.type == ParamType::Double) {
            bool ok = false;
            const double doubleValue = value.toDouble(&ok);
            if (ok)
                normalized = qBound(spec.minValue.toDouble(), doubleValue, spec.maxValue.toDouble());
        }
        break;
    }

    m_params[name] = normalized;

    // 参数变化时触发延迟预览
    if (m_autoPreviewEnabled) {
        triggerDelayedPreview();
    }
}

QVariant HalconNode::getParam(const QString &name) const
{
    return m_params.value(name);
}

bool HalconNode::hasParam(const QString &name) const
{
    return m_params.contains(name);
}

void HalconNode::setAutoPreviewEnabled(bool enabled)
{
    m_autoPreviewEnabled = enabled;
    if (!enabled && m_previewTimer) {
        m_previewTimer->stop();
    }
}

void HalconNode::triggerDelayedPreview()
{
    if (m_previewTimer && m_autoPreviewEnabled) {
        m_previewTimer->start();  // 重启定时器，实现防抖
    }
}

void HalconNode::executePreview()
{
    if (!m_autoPreviewEnabled) return;

    // 记录执行时间
    QElapsedTimer timer;
    timer.start();

    bool success = false;
    try {
        // 检查是否有输入数据
        bool hasInput = false;
        for (auto it = m_inputData.begin(); it != m_inputData.end(); ++it) {
            if (it.value() && !it.value()->getHImage().IsInitialized()) {
                hasInput = true;
                break;
            }
        }

        if (hasInput || m_inputImage.IsInitialized()) {
            success = process();
            if (success) {
                displayImage();
            }
        }
    } catch (const HalconCpp::HException &e) {
        VFP_DEBUG << "Preview HALCON error:" << e.ErrorMessage();
        success = false;
    } catch (const std::exception &e) {
        VFP_DEBUG << "Preview error:" << e.what();
        success = false;
    }

    qint64 elapsed = timer.elapsed();
    emit executionTimeMeasured(this, elapsed);
    emit previewCompleted(this, success);

    VFP_DEBUG << "Auto preview for" << m_name << ":" << (success ? "success" : "failed")
              << "time:" << elapsed << "ms";
}

void HalconNode::drawResult()
{
    // Default implementation - do nothing
}

QJsonObject HalconNode::toJson() const
{
    QJsonObject json;
    json["name"] = m_name;
    json["type"] = m_type;
    json["nodeType"] = m_type;
    json["nodeName"] = m_name;
    json["position"] = QJsonObject({
        {"x", m_position.x()},
        {"y", m_position.y()}
    });
    json["size"] = QJsonObject({
        {"width", m_size.width()},
        {"height", m_size.height()}
    });
    json["moduleId"] = m_moduleId;
    json["executionSuccess"] = m_executionSuccess;
    json["enabled"] = m_enabled;
    
    // Add parameters
    QJsonObject params;
    for (auto it = m_params.constBegin(); it != m_params.constEnd(); ++it) {
        params[it.key()] = QJsonValue::fromVariant(it.value());
    }
    json["params"] = params;

    if (!m_editMask.isNull()) {
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        m_editMask.save(&buf, "PNG");
        json[QStringLiteral("editMaskPng")] = QString::fromLatin1(bytes.toBase64());
    }

    return json;
}

void HalconNode::setEditMask(const QImage &mask)
{
    if (mask.isNull()) {
        m_editMask = QImage();
        return;
    }
    m_editMask = mask.convertToFormat(QImage::Format_Grayscale8);
}

void HalconNode::clearEditMask()
{
    m_editMask = QImage();
}

// 依据 ParamSpec 校验并修复参数值（类型/范围/枚举），非法时回退默认值
static QVariant validateParamValue(const ParamSpec &spec, const QVariant &value)
{
    switch (spec.type) {
    case ParamType::Int: {
        bool ok = false;
        const int v = value.toInt(&ok);
        if (!ok) return spec.defaultValue;
        int r = v;
        if (spec.hasRange)
            r = qBound(spec.minValue.toInt(), r, spec.maxValue.toInt());
        return r;
    }
    case ParamType::Double: {
        bool ok = false;
        const double v = value.toDouble(&ok);
        if (!ok) return spec.defaultValue;
        double r = v;
        if (spec.hasRange)
            r = qBound(spec.minValue.toDouble(), r, spec.maxValue.toDouble());
        return r;
    }
    case ParamType::Bool:
        return value.toBool();
    case ParamType::Enum: {
        bool ok = false;
        const int idx = value.toInt(&ok);
        if (!ok || idx < 0 || idx >= spec.enumValues.size())
            return spec.defaultValue;
        return idx;
    }
    case ParamType::String:
    case ParamType::FilePath:
    case ParamType::MultiLine:
        return value.toString();
    case ParamType::Point:
    case ParamType::Rect:
        return value;  // 复合类型按原值
    }
    return value;
}

void HalconNode::fromJson(const QJsonObject &json)
{
    m_name = json["name"].toString();
    if (json.contains("nodeName")) {
        m_name = json["nodeName"].toString();
    }
    m_type = static_cast<NodeType>(json["type"].toInt());
    if (json.contains("nodeType")) {
        m_type = static_cast<NodeType>(json["nodeType"].toInt());
    }
    
    QJsonObject posJson = json["position"].toObject();
    m_position = QPointF(posJson["x"].toDouble(), posJson["y"].toDouble());
    
    QJsonObject sizeJson = json["size"].toObject();
    m_size = QSizeF(sizeJson["width"].toDouble(), sizeJson["height"].toDouble());
    
    // 恢复模块ID（可选，因为模块ID是运行时生成的）
    if (json.contains("moduleId")) {
        int storedId = json["moduleId"].toInt();
        if (storedId != m_moduleId) {
            NodeBase::releaseModuleId(m_moduleId);
            m_moduleId = storedId;
        }
    }
    
    // 恢复执行状态
    if (json.contains("executionSuccess")) {
        m_executionSuccess = json["executionSuccess"].toBool();
    }
    if (json.contains(QStringLiteral("enabled")))
        m_enabled = json.value(QStringLiteral("enabled")).toBool(true);
    
    // Load parameters（带 schema 校验：类型/范围/枚举非法时回退默认值）
    QJsonObject paramsJson = json["params"].toObject();
    for (auto it = paramsJson.constBegin(); it != paramsJson.constEnd(); ++it) {
        const QString key = it.key();
        const ParamSpec *spec = nullptr;
        for (const ParamSpec &s : m_paramSpecs) {
            if (s.name == key) { spec = &s; break; }
        }
        if (spec) {
            const QVariant raw = it.value().toVariant();
            const QVariant fixed = validateParamValue(*spec, raw);
            if (fixed != raw) {
                VFP_DEBUG << "ParamSchemaFix:" << m_name << key
                          << "raw=" << raw.toString() << "->" << fixed.toString();
            }
            m_params[key] = fixed;
        } else {
            m_params[key] = it.value().toVariant();
        }
    }

    m_editMask = QImage();
    const QString maskB64 = json.value(QStringLiteral("editMaskPng")).toString();
    if (!maskB64.isEmpty()) {
        const QByteArray bytes = QByteArray::fromBase64(maskB64.toLatin1());
        m_editMask.loadFromData(bytes, "PNG");
        if (!m_editMask.isNull())
            m_editMask = m_editMask.convertToFormat(QImage::Format_Grayscale8);
    }
}

HObject HalconNode::getInputImage() const
{
    return m_inputImage;
}

void HalconNode::setInputImage(const HObject &image)
{
    m_inputImage = image;
}

HObject HalconNode::getOutputImage() const
{
    return m_outputImage;
}

bool HalconNode::process()
{
    try {
        // 与 FlowExecutor::propagateData 对齐：连线上的图像进入 m_inputData，此处同步到 Halcon HObject
        if (!m_inputPorts.isEmpty()) {
            if (m_inputData.contains(0) && m_inputData[0]) {
                const HalconCpp::HImage wired = m_inputData[0]->getHImage();
                if (wired.IsInitialized()) {
                    m_inputImage = wired;
                }
            }
        }

        run();

        // 失败状态传播：子类 run() 在 catch 中设置 moduleStatus=false 时，
        // 此处必须如实返回失败，避免上游把失败当成功继续传播
        if (m_params.contains(QStringLiteral("moduleStatus"))
            && !m_params[QStringLiteral("moduleStatus")].toBool()) {
            m_outputImage.Clear();
            return false;
        }

        HalconCpp::HImage output(m_outputImage);
        if (output.IsInitialized()) {
            auto outObj = QSharedPointer<DataObject>::create();
            outObj->setHImage(output);
            setOutputData(0, outObj);
        } else if (!m_outputPorts.isEmpty()) {
            setOutputData(0, QSharedPointer<DataObject>());
        }

        m_params["moduleStatus"] = true;
        return true;
    } catch (...) {
        m_params["moduleStatus"] = false;
        return false;
    }
}

QWidget *HalconNode::createParamPanel()
{
    // 声明式参数系统：注册了 ParamSpec 的算子自动生成参数面板
    if (!m_paramSpecs.isEmpty())
        return createAutoParamPanel();

    try {
        // Default implementation - create a panel with input image parameter
        QWidget *panel = new QWidget();
        QVBoxLayout *layout = new QVBoxLayout(panel);
        
        // 输入图像选择（非图像获取算子必须有输入图像参数）
        QLabel *inputImageLabel = new QLabel("输入图像:");
        QComboBox *inputImageCombo = new QComboBox();
        inputImageCombo->setObjectName("inputImageCombo");
        
        // Add a default label
        QLabel *label = new QLabel("No additional parameters available for this node");
        
        // 连接输入图像选择信号
        connect(inputImageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
            QVariant data = inputImageCombo->itemData(index);
            if (data.canConvert<QPair<NodeBase*, int>>()) {
                QPair<NodeBase*, int> pair = data.value<QPair<NodeBase*, int>>();
                NodeBase *sourceNode = pair.first;
                int portIndex = pair.second;
                
                // 这里可以保存选择的输入图像来源
                m_params["inputImageSource"] = QVariant::fromValue(QPair<NodeBase*, int>(sourceNode, portIndex));
                VFP_DEBUG << "Selected input image source:" << sourceNode->name() << "port" << portIndex;
            }
        });
        
        layout->addWidget(inputImageLabel);
        layout->addWidget(inputImageCombo);
        layout->addWidget(label);
        
        // 延迟初始化输入图像下拉菜单，确保能获取到场景
        QTimer::singleShot(100, [=]() {
            // 保存当前选择
            int currentIndex = inputImageCombo->currentIndex();
            QVariant currentData = inputImageCombo->itemData(currentIndex);
            
            // 清空现有选项
            inputImageCombo->clear();
            // 添加默认选项
            inputImageCombo->addItem("连接到输入端口", QVariant());
            
            // 尝试获取当前场景
            FlowScene *scene = nullptr;
            
            // 方法1: 通过节点的父对象查找
            QObject *parentObj = this->parent();
            while (parentObj) {
                scene = qobject_cast<FlowScene*>(parentObj);
                if (scene) {
                    break;
                }
                parentObj = parentObj->parent();
            }
            
            // 方法2: 如果方法1失败，尝试通过面板的父控件查找
            if (!scene) {
                QWidget *parentWidget = panel->parentWidget();
                while (parentWidget) {
                    scene = qobject_cast<FlowScene*>(parentWidget);
                    if (scene) {
                        break;
                    }
                    parentWidget = parentWidget->parentWidget();
                }
            }
            
            // 如果找到了场景，枚举所有可连接的输出参数
            if (scene) {
                VFP_DEBUG << "Found scene, nodes count:" << scene->nodes().size();
                
                // 检查是否有箭头连接到当前节点
                bool hasConnection = false;
                QList<MyProject::Connection*> connectionsList = scene->connections();
                for (int i = 0; i < connectionsList.size(); ++i) {
                    MyProject::Connection *connection = connectionsList[i];
                    if (connection->targetPort() && connection->targetPort()->node() == this) {
                        hasConnection = true;
                        break;
                    }
                }

                // 只有当有箭头连接时，才枚举其他节点的输出参数
                if (hasConnection) {
                    QList<NodeBase*> nodes = scene->nodes();
                    for (int i = 0; i < nodes.size(); ++i) {
                        NodeBase *node = nodes[i];
                        // 跳过当前节点
                        if (node == this) {
                            continue;
                        }
                        
                        VFP_DEBUG << "Checking node:" << node->name() << "hasExecuted:" << node->hasExecuted();
                        
                        // 检查节点是否已经执行过
                        if (node->hasExecuted()) {
                            // 枚举节点的所有输出端口
                            for (int i = 0; i < node->outputPorts().size(); ++i) {
                                Port *outputPort = node->outputPorts()[i];
                                QSharedPointer<DataObject> outputData = node->getOutputData(i);
                                
                                VFP_DEBUG << "  Port:" << outputPort->name() << "outputData:" << (outputData ? "valid" : "null");
                                
                                // 检查输出数据是否存在
                                if (outputData) {
                                    // 将枚举类型转换为整数输出
                                    VFP_DEBUG << "  Data type:" << static_cast<int>(outputData->getType());
                                    // 检查输出数据类型是否为图像
                                    if (outputData->getType() == DataObject::DataType::Image) {
                                        QString itemText = QString("%1 [%2] - %3").arg(node->name()).arg(node->moduleId()).arg(outputPort->name());
                                        inputImageCombo->addItem(itemText, QVariant::fromValue(QPair<NodeBase*, int>(node, i)));
                                        VFP_DEBUG << "  Added item:" << itemText;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    VFP_DEBUG << "No connections to current node, skipping output parameter enumeration";
                }
            } else {
                VFP_DEBUG << "Scene not found";
            }
            
            // 恢复之前的选择
            if (currentData.isValid()) {
                for (int i = 0; i < inputImageCombo->count(); ++i) {
                    QVariant data = inputImageCombo->itemData(i);
                    if (data == currentData) {
                        inputImageCombo->setCurrentIndex(i);
                        break;
                    }
                }
            }
            
            // 或者从参数中恢复选择
            if (inputImageCombo->currentIndex() == 0 && m_params.contains("inputImageSource")) {
                QVariant sourceData = m_params["inputImageSource"];
                if (sourceData.canConvert<QPair<NodeBase*, int>>()) {
                    QPair<NodeBase*, int> pair = sourceData.value<QPair<NodeBase*, int>>();
                    NodeBase *sourceNode = pair.first;
                    int portIndex = pair.second;
                    
                    for (int i = 0; i < inputImageCombo->count(); ++i) {
                        QVariant data = inputImageCombo->itemData(i);
                        if (data.canConvert<QPair<NodeBase*, int>>()) {
                            QPair<NodeBase*, int> comboPair = data.value<QPair<NodeBase*, int>>();
                            if (comboPair.first == sourceNode && comboPair.second == portIndex) {
                                inputImageCombo->setCurrentIndex(i);
                                break;
                            }
                        }
                    }
                }
            }
        });
        
        return panel;
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in createParamPanel:" << e.what();
        return new QWidget();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in createParamPanel";
        return new QWidget();
    }
}

void HalconNode::updateParamPanel(QWidget *panel)
{
    // 声明式参数系统：自动刷新
    if (!m_paramSpecs.isEmpty()) {
        updateAutoParamPanel(panel);
        return;
    }
    // Default implementation - do nothing
    if (!panel) return;
}

void HalconNode::displayImage()
{
    // Default implementation - do nothing
    QSharedPointer<DataObject> outputData = getOutputData(0);
    if (outputData) {
        HalconCpp::HImage image = outputData->getHImage();
        if (image.IsInitialized()) {
            // 这里可以添加默认的图像显示逻辑
            VFP_DEBUG << "Displaying image from HalconNode";
        }
    }
}

// ==================== 声明式参数系统 ====================

void HalconNode::registerParam(const ParamSpec &spec)
{
    m_paramSpecs.append(spec);
    // 默认值写入 m_params（不覆盖已有值）
    if (!m_params.contains(spec.name) && spec.defaultValue.isValid())
        m_params[spec.name] = spec.defaultValue;
}

void HalconNode::registerParams(const ParamSpecList &specs)
{
    for (const auto &s : specs)
        registerParam(s);
}

static QWidget *makeParamEditor(const ParamSpec &spec, QVariant &outInit)
{
    switch (spec.type) {
    case ParamType::Int: {
        auto *w = new QSpinBox();
        if (spec.hasRange) {
            w->setRange(spec.minValue.toInt(), spec.maxValue.toInt());
        } else {
            w->setRange(-2147483647, 2147483647);
        }
        if (!spec.unit.isEmpty()) w->setSuffix(QStringLiteral(" %1").arg(spec.unit));
        if (!spec.tooltip.isEmpty()) w->setToolTip(spec.tooltip);
        outInit = spec.defaultValue.toInt();
        return w;
    }
    case ParamType::Double: {
        auto *w = new QDoubleSpinBox();
        if (spec.hasRange) {
            w->setRange(spec.minValue.toDouble(), spec.maxValue.toDouble());
        } else {
            w->setRange(-1e12, 1e12);
        }
        w->setDecimals(4);
        if (!spec.unit.isEmpty()) w->setSuffix(QStringLiteral(" %1").arg(spec.unit));
        if (!spec.tooltip.isEmpty()) w->setToolTip(spec.tooltip);
        outInit = spec.defaultValue.toDouble();
        return w;
    }
    case ParamType::Bool: {
        auto *w = new QCheckBox();
        if (!spec.tooltip.isEmpty()) w->setToolTip(spec.tooltip);
        outInit = spec.defaultValue.toBool();
        return w;
    }
    case ParamType::String: {
        // 容器：输入框 + 参数引用按钮（{} 弹出全局变量/模块输出参数菜单）
        auto *container = new QWidget();
        auto *lay = new QHBoxLayout(container);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(2);
        auto *w = new QLineEdit();
        w->setText(spec.defaultValue.toString());
        w->setObjectName(spec.name);
        if (!spec.tooltip.isEmpty()) w->setToolTip(spec.tooltip);
        auto *btn = new QPushButton(QStringLiteral("{}"));
        btn->setFixedWidth(32);
        btn->setToolTip(QStringLiteral("插入参数引用：{模块号.参数} 或 {global.全局变量}"));
        btn->setProperty("_refParamName", spec.name);
        lay->addWidget(w, 1);
        lay->addWidget(btn);
        outInit = spec.defaultValue.toString();
        return container;
    }
    case ParamType::MultiLine: {
        auto *w = new QPlainTextEdit();
        w->setPlainText(spec.defaultValue.toString());
        w->setMaximumHeight(120);
        outInit = spec.defaultValue.toString();
        return w;
    }
    case ParamType::Enum: {
        auto *w = new QComboBox();
        w->addItems(spec.enumValues);
        if (!spec.tooltip.isEmpty()) w->setToolTip(spec.tooltip);
        outInit = spec.defaultValue.toInt();
        return w;
    }
    case ParamType::FilePath: {
        auto *container = new QWidget();
        auto *lay = new QHBoxLayout(container);
        lay->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QLineEdit(spec.defaultValue.toString());
        auto *btn = new QPushButton(QStringLiteral("..."));
        btn->setFixedWidth(32);
        lay->addWidget(edit, 1);
        lay->addWidget(btn);
        edit->setProperty("_paramEditor", true);
        edit->setObjectName(spec.name);
        // 浏览按钮存为子属性，供连接逻辑使用
        btn->setProperty("_fileParamName", spec.name);
        outInit = spec.defaultValue.toString();
        return container;
    }
    case ParamType::Point: {
        auto *container = new QWidget();
        auto *lay = new QHBoxLayout(container);
        lay->setContentsMargins(0, 0, 0, 0);
        auto *x = new QDoubleSpinBox();
        x->setRange(-1e7, 1e7); x->setDecimals(4);
        auto *y = new QDoubleSpinBox();
        y->setRange(-1e7, 1e7); y->setDecimals(4);
        lay->addWidget(x, 1);
        lay->addWidget(y, 1);
        x->setObjectName(spec.name + QStringLiteral("_x"));
        y->setObjectName(spec.name + QStringLiteral("_y"));
        outInit = spec.defaultValue;
        return container;
    }
    case ParamType::Rect: {
        auto *container = new QWidget();
        auto *lay = new QHBoxLayout(container);
        lay->setContentsMargins(0, 0, 0, 0);
        QStringList suffix = {QStringLiteral("_x"), QStringLiteral("_y"),
                              QStringLiteral("_w"), QStringLiteral("_h")};
        for (const auto &s : suffix) {
            auto *sb = new QDoubleSpinBox();
            sb->setRange(-1e7, 1e7); sb->setDecimals(4);
            sb->setObjectName(spec.name + s);
            lay->addWidget(sb, 1);
        }
        outInit = spec.defaultValue;
        return container;
    }
    }
    auto *w = new QLineEdit();
    outInit = spec.defaultValue;
    return w;
}

QWidget *HalconNode::createAutoParamPanel()
{
    auto *panel = new QWidget();
    auto *mainLayout = new QVBoxLayout(panel);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(4);

    // 按 group 分组
    QStringList groups;
    QMap<QString, QList<const ParamSpec *>> byGroup;
    for (const auto &spec : m_paramSpecs) {
        QString g = spec.group;
        if (g.isEmpty()) g = QStringLiteral("\u53C2\u6570");
        if (!byGroup.contains(g)) groups.append(g);
        byGroup[g].append(&spec);
    }
    if (groups.isEmpty()) groups.append(QStringLiteral("\u53C2\u6570"));

    for (const auto &g : groups) {
        auto *groupBox = new QGroupBox(g);
        auto *form = new QFormLayout(groupBox);
        form->setContentsMargins(8, 10, 8, 8);
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(6);

        for (const ParamSpec *specPtr : byGroup[g]) {
            const ParamSpec &spec = *specPtr;
            QVariant initVal;
            QWidget *editor = makeParamEditor(spec, initVal);

            // 覆盖 objectName 为参数名，便于 updateAutoParamPanel 查找
            if (spec.type == ParamType::Point || spec.type == ParamType::Rect
                || spec.type == ParamType::FilePath) {
                // 复合控件：子控件已有 objectName，无需覆盖
            } else {
                editor->setObjectName(spec.name);
            }

            QString labelText = spec.label.isEmpty() ? spec.name : spec.label;
            if (!spec.unit.isEmpty())
                labelText += QStringLiteral(" (%1)").arg(spec.unit);
            form->addRow(labelText, editor);

            // 恢复当前参数值
            QVariant cur = m_params.value(spec.name, initVal);

            switch (spec.type) {
            case ParamType::Int:
                if (auto *w = qobject_cast<QSpinBox *>(editor)) {
                    QSignalBlocker b(w);
                    w->setValue(cur.toInt());
                }
                break;
            case ParamType::Double:
                if (auto *w = qobject_cast<QDoubleSpinBox *>(editor)) {
                    QSignalBlocker b(w);
                    w->setValue(cur.toDouble());
                }
                break;
            case ParamType::Bool:
                if (auto *w = qobject_cast<QCheckBox *>(editor)) {
                    QSignalBlocker b(w);
                    w->setChecked(cur.toBool());
                }
                break;
            case ParamType::String:
            case ParamType::FilePath:
                if (auto *w = qobject_cast<QLineEdit *>(editor)) {
                    QSignalBlocker b(w);
                    w->setText(cur.toString());
                } else {
                    // FilePath 复合控件
                    auto *ed = editor->findChild<QLineEdit *>(spec.name);
                    if (ed) { QSignalBlocker b(ed); ed->setText(cur.toString()); }
                }
                break;
            case ParamType::MultiLine:
                if (auto *w = qobject_cast<QPlainTextEdit *>(editor)) {
                    QSignalBlocker b(w);
                    w->setPlainText(cur.toString());
                }
                break;
            case ParamType::Enum:
                if (auto *w = qobject_cast<QComboBox *>(editor)) {
                    QSignalBlocker b(w);
                    w->setCurrentIndex(qBound(0, cur.toInt(), w->count() - 1));
                }
                break;
            case ParamType::Point: {
                auto *x = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_x"));
                auto *y = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_y"));
                QPointF p = cur.toPointF();
                if (x) { QSignalBlocker b(x); x->setValue(p.x()); }
                if (y) { QSignalBlocker b(y); y->setValue(p.y()); }
                break;
            }
            case ParamType::Rect: {
                QRectF r = cur.toRectF();
                auto *x = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_x"));
                auto *y = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_y"));
                auto *w2 = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_w"));
                auto *h = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_h"));
                if (x) { QSignalBlocker b(x); x->setValue(r.x()); }
                if (y) { QSignalBlocker b(y); y->setValue(r.y()); }
                if (w2) { QSignalBlocker b(w2); w2->setValue(r.width()); }
                if (h) { QSignalBlocker b(h); h->setValue(r.height()); }
                break;
            }
            }

            // 连接信号 → setParam
            switch (spec.type) {
            case ParamType::Int:
                if (auto *w = qobject_cast<QSpinBox *>(editor)) {
                    connect(w, qOverload<int>(&QSpinBox::valueChanged), this,
                            [this, spec](int v) { setParam(spec.name, v); });
                }
                break;
            case ParamType::Double:
                if (auto *w = qobject_cast<QDoubleSpinBox *>(editor)) {
                    connect(w, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                            [this, spec](double v) { setParam(spec.name, v); });
                }
                break;
            case ParamType::Bool:
                if (auto *w = qobject_cast<QCheckBox *>(editor)) {
                    connect(w, &QCheckBox::toggled, this,
                            [this, spec](bool v) { setParam(spec.name, v); });
                }
                break;
            case ParamType::String: {
                // 输入框（容器内）
                QLineEdit *w = nullptr;
                if (auto *direct = qobject_cast<QLineEdit *>(editor)) {
                    w = direct;
                } else {
                    w = editor->findChild<QLineEdit *>(spec.name);
                }
                if (w) {
                    connect(w, &QLineEdit::textChanged, this,
                            [this, spec](const QString &v) { setParam(spec.name, v); });
                }
                // 参数引用按钮：弹出 全局变量 + 模块输出参数 菜单
                for (QPushButton *btn : editor->findChildren<QPushButton *>()) {
                    if (btn->property("_refParamName").toString() != spec.name)
                        continue;
                    connect(btn, &QPushButton::clicked, this, [this, w]() {
                        QMenu menu;
                        auto *gvm = GlobalVariableManager::instance();
                        QMenu *gMenu = menu.addMenu(QStringLiteral("全局变量"));
                        const auto vars = gvm->variables();
                        if (vars.isEmpty()) {
                            gMenu->addAction(QStringLiteral("（无全局变量，请在系统菜单→全局变量中添加）"))
                                  ->setEnabled(false);
                        }
                        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
                            QAction *act = gMenu->addAction(it.key());
                            connect(act, &QAction::triggered, this, [w, key = it.key()]() {
                                if (w)
                                    w->insert(QStringLiteral("{global.%1}").arg(key));
                            });
                        }
                        QMenu *mMenu = menu.addMenu(QStringLiteral("模块输出参数"));
                        if (FlowScene *scene = this->flowSceneRef()) {
                            const auto nodes = scene->nodes();
                            for (NodeBase *n : nodes) {
                                if (n == this) continue;
                                QMenu *sub =
                                    mMenu->addMenu(QStringLiteral("%1（模块%2）")
                                                       .arg(n->name())
                                                       .arg(n->moduleId()));
                                QAction *vAct =
                                    sub->addAction(QStringLiteral("主测量值"));
                                connect(vAct, &QAction::triggered, this, [w, n]() {
                                    if (w)
                                        w->insert(
                                            QStringLiteral("{%1}").arg(n->moduleId()));
                                });
                                const auto keys = n->getAllParamNames();
                                for (const QString &k : keys) {
                                    QAction *act = sub->addAction(k);
                                    connect(act, &QAction::triggered, this,
                                            [w, n, k]() {
                                        if (w)
                                            w->insert(QStringLiteral("{%1.%2}")
                                                          .arg(n->moduleId())
                                                          .arg(k));
                                    });
                                }
                            }
                        }
                        menu.exec(QCursor::pos());
                    });
                    break;
                }
                break;
            }
            case ParamType::MultiLine:
                if (auto *w = qobject_cast<QPlainTextEdit *>(editor)) {
                    connect(w, &QPlainTextEdit::textChanged, this, [this, spec, w]() {
                        setParam(spec.name, w->toPlainText());
                    });
                }
                break;
            case ParamType::Enum:
                if (auto *w = qobject_cast<QComboBox *>(editor)) {
                    connect(w, qOverload<int>(&QComboBox::currentIndexChanged), this,
                            [this, spec](int idx) { setParam(spec.name, idx); });
                }
                break;
            case ParamType::FilePath: {
                auto *ed = editor->findChild<QLineEdit *>(spec.name);
                auto *btn = editor->findChild<QPushButton *>();
                if (ed) {
                    connect(ed, &QLineEdit::textChanged, this,
                            [this, spec](const QString &v) { setParam(spec.name, v); });
                }
                if (btn) {
                    connect(btn, &QPushButton::clicked, this, [this, spec, ed]() {
                        QString path = QFileDialog::getOpenFileName(
                            nullptr, spec.label, ed ? ed->text() : QString());
                        if (!path.isEmpty()) {
                            if (ed) ed->setText(path);
                            setParam(spec.name, path);
                        }
                    });
                }
                break;
            }
            case ParamType::Point: {
                auto *x = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_x"));
                auto *y = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_y"));
                if (x && y) {
                    auto update = [this, spec, x, y]() {
                        setParam(spec.name, QVariant(QPointF(x->value(), y->value())));
                    };
                    connect(x, qOverload<double>(&QDoubleSpinBox::valueChanged), this, update);
                    connect(y, qOverload<double>(&QDoubleSpinBox::valueChanged), this, update);
                }
                break;
            }
            case ParamType::Rect: {
                auto *x = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_x"));
                auto *y = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_y"));
                auto *w2 = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_w"));
                auto *h = editor->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_h"));
                if (x && y && w2 && h) {
                    auto update = [this, spec, x, y, w2, h]() {
                        setParam(spec.name, QVariant(QRectF(x->value(), y->value(),
                                                            w2->value(), h->value())));
                    };
                    connect(x, qOverload<double>(&QDoubleSpinBox::valueChanged), this, update);
                    connect(y, qOverload<double>(&QDoubleSpinBox::valueChanged), this, update);
                    connect(w2, qOverload<double>(&QDoubleSpinBox::valueChanged), this, update);
                    connect(h, qOverload<double>(&QDoubleSpinBox::valueChanged), this, update);
                }
                break;
            }
            }
        }

        mainLayout->addWidget(groupBox);
    }

    // 添加实时预览开关
    auto *previewLayout = new QHBoxLayout();
    previewLayout->setContentsMargins(0, 4, 0, 0);

    auto *previewCheckBox = new QCheckBox(QStringLiteral("自动预览"));
    previewCheckBox->setChecked(m_autoPreviewEnabled);
    previewCheckBox->setToolTip(QStringLiteral("参数修改后自动执行算子并刷新图像"));
    connect(previewCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        setAutoPreviewEnabled(checked);
    });

    auto *previewLabel = new QLabel(QStringLiteral("（参数修改后自动执行）"));
    previewLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));

    previewLayout->addWidget(previewCheckBox);
    previewLayout->addWidget(previewLabel);
    previewLayout->addStretch();

    mainLayout->addLayout(previewLayout);
    mainLayout->addStretch();

    // 保存预览复选框引用，便于后续更新
    panel->setProperty("_previewCheckBox", QVariant::fromValue(previewCheckBox));

    return panel;
}

void HalconNode::updateAutoParamPanel(QWidget *panel)
{
    if (!panel) return;
    for (const auto &spec : m_paramSpecs) {
        QVariant cur = m_params.value(spec.name, spec.defaultValue);
        switch (spec.type) {
        case ParamType::Int:
            if (auto *w = panel->findChild<QSpinBox *>(spec.name)) {
                QSignalBlocker b(w);
                w->setValue(cur.toInt());
            }
            break;
        case ParamType::Double:
            if (auto *w = panel->findChild<QDoubleSpinBox *>(spec.name)) {
                QSignalBlocker b(w);
                w->setValue(cur.toDouble());
            }
            break;
        case ParamType::Bool:
            if (auto *w = panel->findChild<QCheckBox *>(spec.name)) {
                QSignalBlocker b(w);
                w->setChecked(cur.toBool());
            }
            break;
        case ParamType::String:
        case ParamType::FilePath:
            if (auto *w = panel->findChild<QLineEdit *>(spec.name)) {
                QSignalBlocker b(w);
                w->setText(cur.toString());
            }
            break;
        case ParamType::MultiLine:
            if (auto *w = panel->findChild<QPlainTextEdit *>(spec.name)) {
                QSignalBlocker b(w);
                w->setPlainText(cur.toString());
            }
            break;
        case ParamType::Enum:
            if (auto *w = panel->findChild<QComboBox *>(spec.name)) {
                QSignalBlocker b(w);
                w->setCurrentIndex(qBound(0, cur.toInt(), w->count() - 1));
            }
            break;
        case ParamType::Point: {
            QPointF p = cur.toPointF();
            if (auto *x = panel->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_x"))) {
                QSignalBlocker b(x); x->setValue(p.x());
            }
            if (auto *y = panel->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_y"))) {
                QSignalBlocker b(y); y->setValue(p.y());
            }
            break;
        }
        case ParamType::Rect: {
            QRectF r = cur.toRectF();
            if (auto *x = panel->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_x"))) {
                QSignalBlocker b(x); x->setValue(r.x());
            }
            if (auto *y = panel->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_y"))) {
                QSignalBlocker b(y); y->setValue(r.y());
            }
            if (auto *w2 = panel->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_w"))) {
                QSignalBlocker b(w2); w2->setValue(r.width());
            }
            if (auto *h = panel->findChild<QDoubleSpinBox *>(spec.name + QStringLiteral("_h"))) {
                QSignalBlocker b(h); h->setValue(r.height());
            }
            break;
        }
        }
    }
}


