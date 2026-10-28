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
#include <memory>

#include "PortDataType.h"
#include "NodeStateBody.h"   // 身份状态体（切片 C 立项书 P2 期段一：非 QObject 共享载体）
#include "ThreadSafeParams.h"   // 参数内核（立项书 P2 期段一：paramCore 虚钩子返回类型）
#include "FlowRunContext.h"   // 归属上下文块（切片 C 立项书 P2c 期：算子侧改持它）
#include "FlowExecutorToken.h"   // 生命周期令牌（切片 C P3c 期：残余面改持它的弱引用）

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
    NodeType m_type;
    QList<Port *> m_inputPorts;
    QList<Port *> m_outputPorts;
    QMap<int, QSharedPointer<DataObject>> m_inputData;
    QMap<int, QSharedPointer<DataObject>> m_outputData;
    /// 数据访问互斥：run 线程写（setInputData/setOutputData）与 GUI 线程读（getOutputData）
    /// 并发，QMap 非线程安全，必须加锁防结构破坏
    mutable QMutex m_dataMutex;
    bool m_executionSuccess; // 执行状态，true表示成功，false表示失败
    bool m_runAborted = false;   // 本轮被用户中止（2026-10-28 P3-14：第三态）
    bool m_hasExecuted; // 是否已经执行过
    bool m_enabled = true;   // 算子是否启用（禁用后运行时跳过）
    /// 身份状态体（切片 C 立项书 P2 期段一）：名字与模块号的非 QObject 共享载体。
    /// 壳持一份，快照捕获时快照列再持一份；壳与场景先亡后身份仍可读（§13.1 净收益）。
    /// 写入面只在停稳期（方案装载与重命名），并发面与拆分前裸读写一致，故不加锁。
    std::shared_ptr<NodeStateBody> m_stateBody;

public:
    int moduleId() const;
    QString fullName() const; // 包含模块号的完整名称
    bool executionSuccess() const; // 获取执行状态
    /// 本轮是否被用户中止（与成功/失败并列的第三态；画布用琥珀色，
    /// 表示"没跑完"而不是"跑成功了"——2026-10-28 P3-14）
    bool executionAborted() const;
    void setRunAborted(bool on);
    bool hasExecuted() const; // 获取是否已经执行过

    /// 身份状态体共享句柄（立项书 P2 期段一）：仅供快照捕获入快照列。
    /// 增（拷贝）可在图锁内＝纯原子加；减（可能真析构身份体）不得在任何其他锁的
    /// 临界区内（立项书 §13.2 补充行）。
    const std::shared_ptr<NodeStateBody> &stateBody() const { return m_stateBody; }

    /// 参数内核共享句柄（立项书 P2 期段一）：默认空（无参数表的算子）；
    /// 带参数表的家族（HalconNode）覆写并交出自己的内核，快照捕获时入快照列。
    virtual std::shared_ptr<ThreadSafeParams::Core> paramCore() const { return {}; }

    /// 所属流程场景（供参数引用枚举模块输出参数；由 FlowScene::createNode 绑定）
    void setFlowSceneRef(FlowScene *scene) { m_flowSceneRef = scene; }
    FlowScene *flowSceneRef() const { return m_flowSceneRef; }

    /// 所属执行器（多流程隔离：节点查询运行状态应走 owner，而非全局 current()）
    void setOwnerContext(std::shared_ptr<FlowRunContext> ctx) { m_ownerContext = std::move(ctx); }
    std::shared_ptr<FlowRunContext> ownerContext() const { return m_ownerContext; }
    /// 生命周期令牌（词条切片 C P3c 期：残余面收口）——节点持**弱令牌**而不是执行器裸指针：
    /// 借本体前先 lock（拿到的强引用在调用期间保证本体存活），令牌已收则借不到 ⇒ 不可能摸到
    /// 已死的本体。为什么不是强令牌（实测依据）：场景拥有节点，而界面侧的释放点之后才删场景；
    /// 节点若强持令牌，最后一个释放就推迟到**场景析构**，那时执行器析构里的
    /// 「flowScene()->nodes() 清归属」正要回调一个正在拆的场景 ⇒ 真实的悬垂窗口。
    /// 弱令牌同时保住 P3b 的「释放即同步收尾」语义（强持有只留在拥有本体的界面侧）。
    void setOwnerToken(const std::shared_ptr<FlowExecutorToken> &token) { m_ownerToken = token; }
    std::shared_ptr<FlowExecutorToken> ownerToken() const { return m_ownerToken.lock(); }
    /// 借出本体（令牌已收或无令牌 ⇒ 返回空；调用方用完即放）。
    FlowExecutor *ownerExecutor() const
    {
        const std::shared_ptr<FlowExecutorToken> token = m_ownerToken.lock();
        return token ? token->executor() : nullptr;
    }

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
    /// 归属上下文（切片 C 立项书 P2c 期：算子侧改持运行上下文块，不再持执行器裸指针）
    std::shared_ptr<FlowRunContext> m_ownerContext;
    /// 残余面（切片 C P3c 期）：改持生命周期令牌的**弱**引用（理由见上：强持有会把执行器
    /// 的收尾推迟到场景析构，而收尾要回调场景清归属 ⇒ 悬垂）。
    std::weak_ptr<FlowExecutorToken> m_ownerToken;
};
