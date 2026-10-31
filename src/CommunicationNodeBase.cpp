#include "CommunicationNodeBase.h"
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
            this, &CommunicationNodeBase::onSendRequested, Qt::QueuedConnection);
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

void CommunicationNodeBase::noteSendWriteFailure(const QString &why)
{
    // 归属本节点的流程上下文（没有上下文时记为 nullptr 桶，兼容非流程场景）
    bumpFailure(ownerContext().get());
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
