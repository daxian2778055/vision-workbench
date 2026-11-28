#include <QElapsedTimer>
#include <QThread>
#include "CommunicationNodeBase.h"
#include "FlowExecutor.h"
#include <atomic>
#include "DataObject.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>

CommunicationNodeBase::CommunicationNodeBase(QObject *parent)
    : HalconNode(parent)
{
    // 发送请求强制队列投递：SendDataNode 在流程线程调用 sendData，
    // socket/串口 write 必须在节点所属线程（GUI 线程）执行，避免跨线程不安全访问
    connect(this, &CommunicationNodeBase::sendRequested,
            this, &CommunicationNodeBase::handleSendRequested, Qt::QueuedConnection);
}

void CommunicationNodeBase::init()
{
    HalconNode::init();
    setParamDirect(QStringLiteral("connected"), false);
}

namespace {

// r19 复核 P1-11：计数必须**按流程**分桶——进程级静态量会让 A 流程的发送失败把 B 流程
// 的轮次一起判 NG。键用 FlowRunContext 裸指针（流程存活期内唯一；流程销毁后条目失效，
// 由 reset 清理）。
QMutex s_failMutex;
QHash<const void *, int> s_sendWriteFailures;
// AH1c：同一流程可能有多笔在途发送，按 key 记引用数并持有上下文，防止排队期间被销毁/地址复用。
QMutex s_liveContextMutex;
QHash<const void *, std::shared_ptr<FlowRunContext>> s_liveSendContexts;
QHash<const void *, int> s_liveSendRefs;
namespace {
QHash<const void *, int> s_pendingSends;   // 按流程统计排队中的发送
QMutex s_pendingMutex;
QWaitCondition s_pendingDrained;   // AG2：轮末排空改条件等待，不再 2ms 忙轮询
}   // namespace

int bumpFailure(const void *flow)
{
    QMutexLocker<QMutex> locker(&s_failMutex);
    return ++s_sendWriteFailures[flow];
}

}   // namespace

int CommunicationNodeBase::sendWriteFailureCount(const void *flow)
{
    QMutexLocker<QMutex> locker(&s_failMutex);
    return s_sendWriteFailures.value(flow, 0);
}

void CommunicationNodeBase::resetSendWriteFailureCount(const void *flow)
{
    QMutexLocker<QMutex> locker(&s_failMutex);
    if (flow)
        s_sendWriteFailures.remove(flow);
    else
        s_sendWriteFailures.clear();
}

void CommunicationNodeBase::bumpSendPending(const void *flow)
{
    QMutexLocker<QMutex> lock(&s_pendingMutex);
    ++s_pendingSends[flow];
}

void CommunicationNodeBase::dropSendPending(const void *flow)
{
    QMutexLocker<QMutex> lock(&s_pendingMutex);
    auto it = s_pendingSends.find(flow);
    if (it != s_pendingSends.end()) {
        if (--it.value() <= 0) {
            s_pendingSends.erase(it);
            s_pendingDrained.wakeAll();
        }
    }
}

void CommunicationNodeBase::retainSendContext(const void *flow,
                                              const std::shared_ptr<FlowRunContext> &ctx)
{
    if (!flow || !ctx)
        return;
    {
        QMutexLocker locker(&s_liveContextMutex);
        ++s_liveSendRefs[flow];
        s_liveSendContexts[flow] = ctx;
    }
    ++m_heldSendKeys[flow];
}

void CommunicationNodeBase::releaseSendContext(const void *flow)
{
    auto held = m_heldSendKeys.find(flow);
    if (held == m_heldSendKeys.end())
        return;
    if (--held.value() <= 0)
        m_heldSendKeys.erase(held);
    QMutexLocker locker(&s_liveContextMutex);
    auto it = s_liveSendRefs.find(flow);
    if (it == s_liveSendRefs.end())
        return;
    if (--it.value() <= 0) {
        s_liveSendRefs.erase(it);
        s_liveSendContexts.remove(flow);
    }
}

CommunicationNodeBase::~CommunicationNodeBase()
{
    // AH1d：排队事件随接收方销毁而丢弃，槽不会运行；析构时把本节点持有的引用全部还回去。
    for (auto it = m_heldSendKeys.constBegin(); it != m_heldSendKeys.constEnd(); ++it) {
        QMutexLocker locker(&s_liveContextMutex);
        auto ref = s_liveSendRefs.find(it.key());
        if (ref == s_liveSendRefs.end())
            continue;
        for (int i = 0; i < it.value() && ref.value() > 0; ++i)
            --ref.value();
        if (ref.value() <= 0) {
            s_liveSendRefs.erase(ref);
            s_liveSendContexts.remove(it.key());
        }
    }
    m_heldSendKeys.clear();
}

int CommunicationNodeBase::pendingSendCount(const void *flow)
{
    QMutexLocker<QMutex> lock(&s_pendingMutex);
    return s_pendingSends.value(flow, 0);
}

bool CommunicationNodeBase::waitSendDrain(const void *flow, int timeoutMs)
{
    // AG2：轮末有界等待改为条件变量——原实现每 2ms 醒来查一次计数，
    // 在 2000ms 上限下最多空转 1000 次；写完即唤醒，既不忙等也不超过上限。
    QMutexLocker<QMutex> lock(&s_pendingMutex);
    QElapsedTimer t;
    t.start();
    while (s_pendingSends.value(flow, 0) > 0) {
        const qint64 remain = qint64(timeoutMs) - t.elapsed();
        if (remain <= 0)
            return false;
        s_pendingDrained.wait(&s_pendingMutex, static_cast<unsigned long>(remain));
    }
    return true;
}

void CommunicationNodeBase::handleSendRequested(const QByteArray &data, const void *flowKey)
{
    // AH1：入队时冻结的 key 全程随事件走，绝不重读可能被别的流程覆盖的成员。
    m_activeSendFlow.store(flowKey, std::memory_order_release);
    onSendRequested(data);                    // 子类实现（串口/网络…）
    m_activeSendFlow.store(nullptr, std::memory_order_release);
    dropSendPending(flowKey);                 // 无论成败都销入队时的那条
    releaseSendContext(flowKey);              // AH1c：在途请求结算，释放排队期间持有的上下文
}

void CommunicationNodeBase::noteSendWriteFailure(const QString &why)
{
    // 异步发送路径用活动 key；同步寄存器写入等没有活动发送时仍走原有归属回退。
    const void *key = activeSendFlow();
    bumpFailure(key ? key : sendFlowContext());
    emit communicationError(why);
}

void CommunicationNodeBase::run(bool /*autoSwitch*/)
{
    // Communication nodes don't process images - pass through
    m_outputImage = m_inputImage;

    if (m_outputImage.IsInitialized()) {
        HalconCpp::HImage output(m_outputImage);
        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(output);
        setOutputData(0, outObj);
    }
    m_params["moduleStatus"] = m_connected;
}
