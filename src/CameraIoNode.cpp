#include "CameraIoNode.h"
#include "GlobalCameraManager.h"
#include "AppLog.h"
#include <QWidget>

CameraIoNode::CameraIoNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("相机IO控制"));
    m_type = NodeBase::OUTPUT;
}

CameraIoNode::~CameraIoNode()
{
}

void CameraIoNode::init()
{
    HalconNode::init();
    // 输入：值（Bool，写操作的动态值，置于继承的"输入图像"之后为索引1）；
    //       执行（Any，便于串接在检测/判定之后）
    addInputPort(QStringLiteral("值"), PortDataType::Bool);
    addInputPort(QStringLiteral("执行"), PortDataType::Any);
    // 输出：值（Bool 回读/写入值）；成功（Bool）；错误（String）
    addOutputPort(QStringLiteral("值"), PortDataType::Bool);
    addOutputPort(QStringLiteral("成功"), PortDataType::Bool);
    addOutputPort(QStringLiteral("错误"), PortDataType::String);
    registerParams({
        makeStringParam(QStringLiteral("cameraName"), QString(),
                       QStringLiteral("全局相机名（需在相机管理绑定并打开，如 Camera 1）")),
        makeIntParam(QStringLiteral("lineIndex"), 0, 0, 15,
                     QStringLiteral("IO 线序号（0 基，对应 Line1..）")),
        makeIntParam(QStringLiteral("lineMode"), 1, 0, 1,
                     QStringLiteral("线方向（0=输入, 1=输出）")),
        makeIntParam(QStringLiteral("action"), 1, 0, 1,
                     QStringLiteral("动作（0=读, 1=写）")),
        makeBoolParam(QStringLiteral("writeValue"), false,
                      QStringLiteral("写入值（action=写 时生效）")),
    });
    m_params[QStringLiteral("lastError")] = QString();
    m_params[QStringLiteral("moduleStatus")] = false;
}

void CameraIoNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("lastError")] = QString();
    m_params[QStringLiteral("moduleStatus")] = false;
    bool value = false;
    try {
        const QString cameraName =
            m_params.value(QStringLiteral("cameraName"), QString()).toString().trimmed();
        if (cameraName.isEmpty()) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("请设置相机名");
            return;
        }
        GlobalCameraManager *gcm = GlobalCameraManager::instance();

        const int lineIndex = m_params.value(QStringLiteral("lineIndex"), 0).toInt();
        const int lineMode  = m_params.value(QStringLiteral("lineMode"), 1).toInt();
        const int action    = m_params.value(QStringLiteral("action"), 1).toInt();

        // 写操作要求线方向为输出（参数级校验，与相机是否在线无关）
        if (action == 1 && lineMode == 0) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("写入操作要求线方向=输出(1)");
            return;
        }

        void *hCamera = gcm->getCameraHandle(cameraName);
        if (!hCamera) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("相机未打开或不存在");
            return;
        }

        if (action == 1) {
            // 动态写入值：若"值"输入端口已连接则优先，否则用参数
            auto inVal = getInputData(1);
            if (inVal && inVal->getType() == DataObject::DataType::Bool)
                value = inVal->getData().toBool();
            else
                value = m_params.value(QStringLiteral("writeValue"), false).toBool();
            if (!gcm->setCameraLineValue(cameraName, lineIndex, value)) {
                m_params[QStringLiteral("lastError")] = QStringLiteral("写入相机 IO 失败");
                return;
            }
        } else {
            // 读
            if (!gcm->getCameraLineValue(cameraName, lineIndex, value)) {
                m_params[QStringLiteral("lastError")] = QStringLiteral("读取相机 IO 失败");
                return;
            }
        }

        m_params[QStringLiteral("moduleStatus")] = true;

        // 端口0：值（Bool）
        auto valObj = QSharedPointer<DataObject>::create();
        valObj->setData(QVariant(value));
        valObj->setType(DataObject::DataType::Bool);
        setOutputData(0, valObj);

        // 端口1：成功（Bool）
        auto okObj = QSharedPointer<DataObject>::create();
        okObj->setData(QVariant(true));
        okObj->setType(DataObject::DataType::Bool);
        setOutputData(1, okObj);

        // 端口2：错误（String）
        auto errObj = QSharedPointer<DataObject>::create();
        errObj->setData(QVariant(QString()));
        errObj->setType(DataObject::DataType::String);
        setOutputData(2, errObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "CameraIoNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(e.what());
    } catch (...) {
        m_params[QStringLiteral("lastError")] = QStringLiteral("未知错误");
    }
}

QWidget *CameraIoNode::createParamPanel()
{
    return createAutoParamPanel();
}

void CameraIoNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
