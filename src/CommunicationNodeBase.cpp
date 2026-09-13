#include "CommunicationNodeBase.h"
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
    m_params[QStringLiteral("connected")] = false;
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
