#include "FlowExecutorToken.h"

#include "FlowExecutor.h"

#include <QDebug>
#include <QObject>
#include <QThread>

// 执行器生命周期令牌（词条切片 C P3a 期）
// 收尾两分支与界面侧回收件同口径：等待成功即回收；等待超时绝不强杀（硬杀可能卡在图形栈
// 上下文、数据库线程连接或持锁），改为断开信号 ＋ 解除父子 ＋ 挂 finished 自删。

FlowExecutorToken::FlowExecutorToken(FlowExecutor *executor, Finisher finisher)
    : m_owner(executor)
    , m_finisher(std::move(finisher))
{
    if (m_owner) {
        // 解除父子：删除权归令牌（否则父对象析构会与令牌收尾抢同一个本体）。
        m_owner->setParent(nullptr);
    }
}

std::shared_ptr<FlowExecutorToken> FlowExecutorToken::create(FlowExecutor *executor,
                                                             Finisher finisher)
{
    std::shared_ptr<FlowExecutorToken> token =
        std::make_shared<FlowExecutorToken>(executor, std::move(finisher));
    if (executor) {
        executor->setLifetimeToken(token);   // 本体只存弱引用（强持会成环）
    }
    return token;
}

FlowExecutorToken::~FlowExecutorToken()
{
    if (!m_owner) {
        return;
    }
    FlowExecutor *ex = m_owner;
    // 收尾策略：默认 joinForDestroy（停止 ＋ 有界等待退出，唯一实现）；非空 = 测试注入。
    constexpr int kFinishTimeoutMs = 15000;
    const bool joined = m_finisher ? m_finisher(ex, kFinishTimeoutMs)
                                   : ex->joinForDestroy(kFinishTimeoutMs);
    if (joined) {
        // 顺序要点：**先回收再置空**。本体析构里要按归属块把节点的归属槽清空（P3c），
        // 那一段发生在 delete 之内；令牌若提前置空，令牌面在本体收尾期间就借不到本体了。
        delete ex;
        m_owner = nullptr;
        return;
    }
    // P3d（终局撤除）：超时仍未退出——绝不 delete（线程析构会硬杀，比一个随进程退出被回收
    // 的线程更危险）。先把本体置入**可观测终态**（Error ＋ executionAbandoned ＋ 关键日志），
    // 再断开信号并挂 finished 自删；所有权交给自删路径。与旧行为的差：此前只写一条调试
    // 日志就静默消失，界面可能停在「运行中」且现场无提示；进程同样不中止。
    const QString why = QStringLiteral(
        "FlowExecutorToken 收尾：执行器工作线程未在 %1ms 内退出，已放弃强杀并转入可观测终态"
        "（进程继续；线程退出后本体自删）。").arg(kFinishTimeoutMs);
    ex->markExecutionAbandoned(why);
    ex->disconnect();
    QObject::connect(ex, &QThread::finished, ex, &QObject::deleteLater);
    m_owner = nullptr;   // 所有权交给自删路径：令牌此后不得再借出本体
}
