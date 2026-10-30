#include <thread>
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
    m_owner = nullptr;   // 收尾期间令牌不再借出本体
    constexpr int kFinishTimeoutMs = 15000;

    // ── 注入 finisher（测试钉超时分支）：保持同步，改异步会让断言时序失效 ──
    if (m_finisher) {
        const bool joined = m_finisher(ex, kFinishTimeoutMs);
        if (joined) {
            delete ex;
            return;
        }
        abandonBody(ex, kFinishTimeoutMs);
        return;
    }

    // ── 默认路径（r19 P1-13）：短等待内联、长等待挪出 GUI 线程 ──
    // 此前 GUI 线程最多冻 15 秒（切方案/关流程都走这里）。空闲/已停的执行器 300ms 内
    // 一定能收掉，常规关闭仍是同步语义；收不掉的（长节点/硬件等待）交给后台，界面继续可用。
    constexpr int kInlineWaitMs = 300;
    if (ex->joinForDestroy(kInlineWaitMs)) {
        delete ex;
        return;
    }
    std::thread([ex]() {
        constexpr int kRestMs = 15000 - 300;
        if (ex->joinForDestroy(kRestMs)) {
            delete ex;
            return;
        }
        abandonBody(ex, 15000);
    }).detach();
}

void FlowExecutorToken::abandonBody(FlowExecutor *ex, int timeoutMs)
{
    // 超时仍未退出——绝不 delete（线程析构会硬杀）。先把本体置入**可观测终态**
    //（Error + executionAbandoned + 关键日志），再断开信号并挂 finished 自删。
    const QString why = QStringLiteral(
        "FlowExecutorToken 收尾：执行器工作线程未在 %1ms 内退出，已放弃强杀并转入可观测终态"
        "（进程继续；线程退出后本体自删）。").arg(timeoutMs);
    FlowExecutor::invokeOrderlyStopHook(why);
    ex->markExecutionAbandoned(why);
    ex->disconnect();
    QObject::connect(ex, &QThread::finished, ex, &QObject::deleteLater);
}

