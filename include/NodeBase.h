#pragma once

#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QMap>
#include <QJsonObject>
#include <QVariant>
#include <QVariantMap>
#include <QSet>
#include <QPair>
#include <atomic>
#include <QVector>
#include <QSharedPointer>
#include <QWidget>
#include <QVBoxLayout>
#include <QMutex>
#include <memory>

#include "PortDataType.h"
#include "NodeStateBody.h"   // 身份状态体（切片 C 立项书 P2 期段一：非 QObject 共享载体）
#include "ThreadSafeParams.h"   // 参数内核（立项书 P2 期段一：paramCore 虚钩子返回类型）
#include "FlowRunContext.h"   // 归属上下文块（切片 C 立项书 P2c 期：算子侧改持它）
#include "FlowExecutorToken.h"   // 生命周期令牌（切片 C P3c 期：残余面改持它的弱引用）
#include "NodeResultFields.h"   // 结果字段展示口径（VM「模块结果」）

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
    /// 「当前结果」面板只显示这里声明过的结果字段（顺序/名称/单位/精度即官方口径）。
    /// 默认空表 = 沿用旧行为（面板平铺全部参数）；对齐 VM 的算子覆写本函数。
    virtual QVector<NodeResultField> resultDisplayFields() const { return {}; }

    /// 框架级公共结果字段（耗时 / 矩形 ROI 检测区域）：「结果显示 → 结果判断」与
    /// 结果表快照共用同一份取值，避免判断依据和面板显示不是同一个值。
    /// 默认只给 elapsedMs；HalconNode 覆写补 roiCenter* / roiWidth / roiHeight / roiAngle。
    virtual QVariantMap commonResultFields(qint64 elapsedMs) const;

    /// VM「结果显示 → 结果判断」通用条件求值（对所有节点类型统一可用）。
    /// commonFields 里的键优先于 getParam()：公共字段不在节点参数表里。
    /// 返回 false 时 reason 给出第一条不满足条件（含字段名与实际值）。
    bool evaluateResultJudgment(const QVariantMap &commonFields, QString *reason) const;

    virtual void drawResult() = 0;

    /// 新一次执行开始时的钩子（2026-10-28）：默认空实现。
    /// 用于"按批次复位"的状态（如流水线计数器的累计值）——由 FlowExecutor 在轮首统一调用，
    /// 节点不必自己猜"什么时候算新一批"。
    virtual void onRoundStart() {}
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
    /// 逻辑输入槽（B2 单输入契约）：用于"第 2..N 个输入"。
    /// 与 addInputPort 的区别：**不在画布上生成输入点**，下游无法用连线接入；
    /// 数据由「订阅绑定」（setInputBinding）在执行前注入到同一个下标上。
    /// VM 4.4 的多图/多值输入正是这种形态（基本参数里订阅前序模块，而不是多画一个点）。
    /// 约束：必须在全部真实输入端口 addInputPort 之后调用，返回值即该槽的逻辑下标
    /// （inputBindings 以此为键；不在画布上生成输入点）。
    int addInputSlot(const QString &name, PortDataType dataType = PortDataType::Any);
    int inputSlotCount() const { return m_inputSlots.size(); }
    /// 逻辑输入总数（真实端口 + 逻辑槽）。
    int logicalInputPortCount() const { return m_inputPorts.size() + m_inputSlots.size(); }

    /// 输入订阅绑定：把某个逻辑槽绑定到场景内另一算子的输出端口（按模块号，方案存盘稳定）。
    /// 只允许绑定逻辑槽（下标 >= 真实端口数），真实端口仍由连线驱动。
    struct InputBinding
    {
        int sourceModuleId = -1;   ///< 源算子模块号（NodeBase::moduleId()）
        int sourcePort = 0;        ///< 源算子输出端口号
    };
    void setInputBinding(int slot, int sourceModuleId, int sourcePort);
    void clearInputBinding(int slot);
    bool hasInputBinding(int slot) const;
    /// 返回副本：运行线程读、界面线程写，避免持锁遍历时被改
    QMap<int, InputBinding> inputBindings() const;
    /// 逻辑槽显示名（下标为逻辑输入下标；真实端口返回空）
    QString inputSlotName(int slot) const;
    /// 逻辑槽声明类型（真实端口下标返回 Any；供自检/界面按类型喂数据）
    PortDataType inputSlotType(int slot) const;

    /// 虚函数：HalconNode 覆写后把"第 2 个及以后"的输出折叠成同一端口上的具名子结果
    /// （B1 统一输出：一个算子只对外暴露一个输出点）。
    virtual Port *addOutputPort(const QString &name, PortDataType dataType = PortDataType::Image);

    /// 本节点是否把输出聚合到单个端口（B1）。逻辑/分支类节点（条件判断 TRUE/FALSE 两路）
    /// 覆写为 false —— 分支是拓扑语义，不是数据多类型，合并会让分支路由失效。
    virtual bool aggregatesOutputPorts() const { return false; }
    /// 逻辑输出个数（含被折叠的子输出）：给「按端口号取结果」的老读取者一个上界。
    /// 默认 = 真实端口数；聚合节点覆写为 1 + 子输出数。
    virtual int logicalOutputPortCount() const { return outputPorts().size(); }
    /// 具名输出表（端口名 → 数据）：默认按真实端口枚举；聚合节点覆写为"主输出 + 子结果"，
    /// 供运行界面按名取值（IO 状态灯的 值/成功/错误 等）。
    virtual QVector<QPair<QString, QSharedPointer<DataObject>>> namedOutputs() const;

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
    /// 逻辑输入槽：不在画布上生成点，仅占住"逻辑输入下标"并供订阅绑定注入数据
    struct InputSlot
    {
        QString name;
        PortDataType type = PortDataType::Any;
    };
    QVector<InputSlot> m_inputSlots;
    /// 逻辑槽 → 订阅源（按模块号）；运行线程读，界面线程写
    mutable QMutex m_bindingMutex;
    QMap<int, InputBinding> m_inputBindings;
    QMap<int, QSharedPointer<DataObject>> m_inputData;
    QMap<int, QSharedPointer<DataObject>> m_outputData;
    /// 数据访问互斥：run 线程写（setInputData/setOutputData）与 GUI 线程读（getOutputData）
    /// 并发，QMap 非线程安全，必须加锁防结构破坏
    mutable QMutex m_dataMutex;
    bool m_executionSuccess; // 执行状态，true表示成功，false表示失败
    /// 一轮执行结果的**原子快照**（执行线程写、UI 线程读）。
    /// 业务判 NG（moduleStatus=false）但输出结果仍有效（结果判断类算子）时，
    /// process() 与 FlowExecutor 据此保留输出端口；异常/执行失败仍按原逻辑清空。
    /// 打包成单个原子位，避免 UI 分别读参数表里的 moduleStatus 与本标志时拿到不同轮次的值
    /// ——例如轮首刚复位 NG、参数表仍是上一轮的 false。
    /// bit0 = 本轮已执行完（commit 过）/ bit1 = moduleStatus 为 OK / bit2 = resultNg。
    std::atomic<quint32> m_resultState { 0 };
    /// 本轮 process() 自测耗时：判断与结果表快照共用，避免执行器外层计时（含框架开销）
    /// 与判断依据不是同一个值。执行线程写、UI/结果表读，故用原子。
    std::atomic<qint64> m_lastElapsedMs { 0 };
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
    /// 「判 NG 但结果有效」：结果判断（卡尺/Blob 等）不满足时仍保留输出，
    /// 供画布和结果面板显示红色结果；异常/执行失败不走这条。
    /// 本轮执行结果的一致快照：一次原子读出，供画布/结果表判定染色与状态。
    struct ResultState {
        bool executed = false;   ///< 本轮已执行并提交过结果
        bool moduleOk = false;   ///< moduleStatus 为 OK
        bool resultNg = false;   ///< 判 NG 但结果有效
    };
    ResultState resultState() const;
    /// 轮首 / 跳过 / 异常：整轮状态作废（executed 也清掉，UI 不再按上一轮染色）
    void resetRoundState();
    /// 执行结束提交：从 m_resultState 保留 NG 位，写入 executed / moduleOk
    void commitResultState(bool moduleOk);

    /// 只改 NG 位（供 process()/execute() 内部置位），不影响其它位
    void setResultNg(bool on);
    bool isResultNg() const { return (m_resultState.load(std::memory_order_acquire) & 4u) != 0; }
    qint64 lastElapsedMs() const { return m_lastElapsedMs.load(std::memory_order_relaxed); }
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
