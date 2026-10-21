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

FlowExecutorToken::~FlowExecutorToken()
{
    if (!m_owner) {
        return;
    }
    FlowExecutor *ex = m_owner;
    m_owner = nullptr;
    // joinForDestroy 是「停止 ＋ 有界等待退出」的唯一实现（析构走同一函数，两路口径不分叉）。
    if (ex->joinForDestroy(15000)) {
        delete ex;
        return;
    }
    // 超时仍未退出：绝不 delete（线程析构会硬杀，比一个随进程退出被回收的线程更危险）。
    // 解除父子 ＋ 断所有信号（避免本体回调已失效的槽），挂 finished 自删；所有权交给自删路径。
    VFP_DEBUG << "FlowExecutorToken: 执行器超时未退出，放弃强杀，转 finished 自删";
    ex->disconnect();
    QObject::connect(ex, &QThread::finished, ex, &QObject::deleteLater);
}
