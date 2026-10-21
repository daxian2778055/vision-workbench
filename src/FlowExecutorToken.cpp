#include "FlowExecutorToken.h"

#include "AppLog.h"
#include "FlowExecutor.h"

#include <QObject>
#include <QThread>

// 执行器生命周期令牌（词条切片 C P3a 期）
// 收尾两分支与界面侧回收件同口径：等待成功即回收；等待超时绝不强杀（硬杀可能卡在图形栈
// 上下文、数据库线程连接或持锁），改为断开信号 ＋ 解除父子 ＋ 挂 finished 自删。

FlowExecutorToken::FlowExecutorToken(FlowExecutor *executor)
    : m_owner(executor)
{
    if (m_owner) {
        // 解除父子：删除权归令牌（否则父对象析构会与令牌收尾抢同一个本体）。
        m_owner->setParent(nullptr);
    }
}

std::shared_ptr<FlowExecutorToken> FlowExecutorToken::create(FlowExecutor *executor)
{
    std::shared_ptr<FlowExecutorToken> token = std::make_shared<FlowExecutorToken>(executor);
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
    // joinForDestroy 是「停止 ＋ 有界等待退出」的唯一实现（析构走同一函数，两路口径不分叉）。
    if (ex->joinForDestroy(15000)) {
        // 顺序要点：**先回收再置空**。本体析构里要按归属块把节点的归属槽清空（P3c），
        // 那一段发生在 delete 之内；令牌若提前置空，令牌面在本体收尾期间就借不到本体了。
        delete ex;
        m_owner = nullptr;
        return;
    }
    // 超时仍未退出：绝不 delete（线程析构会硬杀，比一个随进程退出被回收的线程更危险）。
    // 解除父子 ＋ 断所有信号（避免本体回调已失效的槽），挂 finished 自删；所有权交给自删路径。
    VFP_DEBUG << "FlowExecutorToken: 执行器超时未退出，放弃强杀，转 finished 自删";
    ex->disconnect();
    QObject::connect(ex, &QThread::finished, ex, &QObject::deleteLater);
    m_owner = nullptr;   // 所有权交给自删路径：令牌此后不得再借出本体
}
