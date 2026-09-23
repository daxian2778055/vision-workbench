#pragma once

#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QMap>
#include <QJsonObject>
#include <QVariant>
#include <QSet>
#include <QWidget>
#include <QVBoxLayout>
#include <QMutex>

#include "PortDataType.h"

#include <QSet>

class Port;
class DataObject;
class FlowScene;
class FlowExecutor;   // 前向声明：仅以指针持有，避免与 FlowExecutor.h 循环依赖

class NodeBase : public QObject
{
    Q_OBJECT

public:
    enum NodeType {
        IMAGE_ACQUISITION,
        IMAGE_PROCESSING,
        SHAPE_ANALYSIS,
        MEASUREMENT,
        LOGIC,
        OUTPUT
    };

    NodeBase(QObject *parent = nullptr);
    virtual ~NodeBase();

    virtual void init() = 0;
    virtual void run(bool autoSwitch = true) = 0;
    virtual void setParam(const QString &name, const QVariant &value) = 0;
    virtual QVariant getParam(const QString &name) const = 0;
    /// 全部参数名（供全局变量输出等按名扫描）
    virtual QList<QString> getAllParamNames() const { return {}; }
    virtual void drawResult() = 0;
    virtual QJsonObject toJson() const = 0;
    virtual void fromJson(const QJsonObject &json) = 0;
    
    // 参数处理和图像显示接口
    virtual QWidget *createParamPanel() = 0;
    virtual void updateParamPanel(QWidget *panel) = 0;
    virtual void displayImage() = 0;

    void setPosition(const QPointF &pos);
    QPointF position() const;

    void setSize(const QSizeF &size);
    QSizeF size() const;

    QString name() const;
    void setName(const QString &name);

    NodeType type() const;

    /// 是否为工业相机图像源（硬触发模式下用于判断流程是否有触发帧门控）
    virtual bool isCameraSource() const { return false; }

    QList<Port *> inputPorts() const;
    QList<Port *> outputPorts() const;

    Port *addInputPort(const QString &name, PortDataType dataType = PortDataType::Image);
    Port *addOutputPort(const QString &name, PortDataType dataType = PortDataType::Image);

    bool hasConnection() const;

    bool execute();
    void setInputData(int portIndex, QSharedPointer<DataObject> data);
    QSharedPointer<DataObject> getInputData(int portIndex) const;
    QSharedPointer<DataObject> getOutputData(int portIndex) const;
    void setOutputData(int portIndex, QSharedPointer<DataObject> data);

    /// 算子是否启用（禁用后运行时跳过该算子）
    void setEnabled(bool on) { m_enabled = on; }
    bool isEnabled() const { return m_enabled; }

    /// 输出是否"可复用"：声明本算子的输出是其输入/参数的**确定性函数**。
    /// 用于局部执行（「执行到此」「重算下游」）的增量优化：确定性源节点（例如单图模式的
    /// 「读取图像」）若此前已成功执行、且没有被判为失效，就没必要再跑一遍（例如重读文件）。
    /// 默认 false —— 多数算子应保持"每次都跑"，尤其是相机取图这类必须取新数据的源节点。
    virtual bool reusesCachedOutput() const { return false; }

protected:
    virtual bool process() = 0;

    QPointF m_position;
    QSizeF m_size;
    QString m_name;
    int m_moduleId; // 唯一的模块号
    NodeType m_type;
    QList<Port *> m_inputPorts;
    QList<Port *> m_outputPorts;
    QMap<int, QSharedPointer<DataObject>> m_inputData;
    QMap<int, QSharedPointer<DataObject>> m_outputData;
    /// 数据访问互斥：run 线程写（setInputData/setOutputData）与 GUI 线程读（getOutputData）
    /// 并发，QMap 非线程安全，必须加锁防结构破坏
    mutable QMutex m_dataMutex;
    bool m_executionSuccess; // 执行状态，true表示成功，false表示失败
    bool m_hasExecuted; // 是否已经执行过
    bool m_enabled = true;   // 算子是否启用（禁用后运行时跳过）

public:
    int moduleId() const;
    QString fullName() const; // 包含模块号的完整名称
    bool executionSuccess() const; // 获取执行状态
    bool hasExecuted() const; // 获取是否已经执行过

    /// 所属流程场景（供参数引用枚举模块输出参数；由 FlowScene::createNode 绑定）
    void setFlowSceneRef(FlowScene *scene) { m_flowSceneRef = scene; }
    FlowScene *flowSceneRef() const { return m_flowSceneRef; }

    /// 所属执行器（多流程隔离：节点查询运行状态应走 owner，而非全局 current()）
    void setOwnerExecutor(FlowExecutor *exec) { m_ownerExecutor = exec; }
    FlowExecutor *ownerExecutor() const { return m_ownerExecutor; }

    // 模块号分配：单调且**不回收**（见 NodeBase.cpp）。回收复用会让"新节点"继承"旧节点"
    // 在执行器缓存里的输出变量表（S4）。
    static int allocateModuleId();
    static void releaseModuleId(int id);   // 保留为空实现，避免改动析构等调用点
    /// 从方案文件恢复节点时调用：把全局计数器抬高到 id 之上，防止随后新建节点复用同一号
    static void reserveModuleId(int id);

    /// 仅供"从方案文件恢复节点身份"使用（执行期严禁修改；只应由 ProjectManager::sceneFromJson 调用）。
    /// 模块号是 {模块号.参数名} 引用、结果表键、配方键的稳定标识：加载后必须与该方案原值一致，
    /// 否则这些引用会静默指向**另一个**算子（结果错但不报错）。内部先把全局计数器抬到 id 之上。
    void setModuleId(int id);

protected:
    FlowScene *m_flowSceneRef = nullptr;
    /// 所属执行器（多流程隔离：节点查询运行状态应走 owner，而非全局 current()）
    FlowExecutor *m_ownerExecutor = nullptr;
};
