#pragma once

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
///  · 令牌**不可拷贝**（单一拥有者；需要共享时用 shared_ptr 持它）。
class FlowExecutorToken final {
public:
    /// 接管一个执行器本体的所有权（解除其父子关系）。
    explicit FlowExecutorToken(FlowExecutor *executor);
    ~FlowExecutorToken();

    FlowExecutorToken(const FlowExecutorToken &) = delete;
    FlowExecutorToken &operator=(const FlowExecutorToken &) = delete;

    /// 借出本体（持有令牌期间本体必存活）。
    FlowExecutor *executor() const { return m_owner; }

private:
    /// 本体由本令牌拥有；收尾后交出（超时分支由自删路径接管）。
    FlowExecutor *m_owner = nullptr;
};
