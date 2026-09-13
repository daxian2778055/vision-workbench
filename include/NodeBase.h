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

    // 模块ID分配/回收（删除节点时释放ID，新建节点时复用）
    static int allocateModuleId();
    static void releaseModuleId(int id);

protected:
    FlowScene *m_flowSceneRef = nullptr;
};
