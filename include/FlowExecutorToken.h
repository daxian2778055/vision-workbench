#pragma once

#include <functional>
#include <memory>

class FlowExecutor;

/// 执行器生命周期令牌（词条切片 C P3a 期：令牌与持有面）。
///
/// 存在理由：执行器本体的销毁权此前是「调用方记得先 join」的纪律要求——触发面与节点残余面
/// 各自持一个执行器裸指针，一旦将来的路由挪到别的线程，裸指针就可能在调用期间失效。
/// 令牌把这层变成**结构保证**：令牌**拥有**执行器本体，持有令牌期间本体必存活；
/// 最后一个令牌释放时执行收尾（停止 ＋ 有界等待 ＋ 回收），于是「本体何时可被销毁」不再
/// 依赖调用方的自觉。
///
/// 契约钉：
///  · 构造即接管所有权（**解除父子**，父对象不再替它做删除——删除权归令牌）；
///  · 收尾两分支与今天的回收件同口径：等待成功 ⇒ 回收本体；等待超时 ⇒ **绝不强杀**
///    （线程析构会 terminate 硬杀，可能卡在图形栈上下文或数据库线程连接或持锁），
///    改为断开信号 ＋ 解除父子 ＋ 挂 finished 自删（自删路径接管所有权）；
///  · 收尾**只发生一次**（最后一个令牌释放时）；
///  · 收尾是**同步**的：释放令牌返回时，本体已经收干净（界面侧的释放点依赖这份同步语义
///    ——它要的是「调用方等它停干净」，不是「稍后某处替它收」）；
///  · 令牌**不可拷贝**（单一拥有者；需要共享时用 shared_ptr 持它）。
///  · P3d 期（终局撤除）：等待超时分支先让本体进入**可观测终态**（Error ＋ executionAbandoned
///    ＋ 关键日志），再断开信号并挂 finished 自删——进程不中止；硬中止只留在执行器析构里作
///    直接 delete 的不可达兜底（生产侧删除只经本令牌）。
///
/// 词条切片 C P3b 期（销毁权归一）：界面侧已改持令牌——执行器的执行器表里存的就是令牌，
/// 释放即收尾 ⇒ **本体的删除只有本件这一处**（界面侧不再自己 delete 执行器）。
class FlowExecutorToken final {
public:
    /// 收尾策略：返回 true = 本体已停稳可回收；false = 超时（放弃强杀）。
    /// 默认空 = 调本体的 joinForDestroy；非空仅供测试注入（钉超时分支，不必真等 15s）。
    using Finisher = std::function<bool(FlowExecutor *, int)>;

    /// 接管一个执行器本体的所有权（解除其父子关系）。
    explicit FlowExecutorToken(FlowExecutor *executor, Finisher finisher = nullptr);
    ~FlowExecutorToken();

    /// 建令牌，并把它的**弱引用**挂到本体上（P3c 期：本体建立节点归属时据此借令牌）。
    /// 为什么由工厂而不由构造函数挂：构造函数里拿不到指向自身的 shared_ptr；
    /// 漏挂的后果是节点侧归属恒空（子流程调用拿不到执行器），故统一从这里建。
    static std::shared_ptr<FlowExecutorToken> create(FlowExecutor *executor,
                                                     Finisher finisher = nullptr);

    FlowExecutorToken(const FlowExecutorToken &) = delete;
    FlowExecutorToken &operator=(const FlowExecutorToken &) = delete;

    /// 借出本体（持有令牌期间本体必存活）。
    FlowExecutor *executor() const { return m_owner; }

private:
    /// 本体由本令牌拥有；收尾后交出（超时分支由自删路径接管）。
    FlowExecutor *m_owner = nullptr;
    /// 收尾策略（默认空 = joinForDestroy；测试可注入以钉超时分支）。
    Finisher m_finisher;
};
