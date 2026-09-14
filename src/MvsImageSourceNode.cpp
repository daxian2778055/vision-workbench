#include "MvsImageSourceNode.h"
#include "DataObject.h"
#include "GlobalCameraManager.h"
#include "GlobalCameraDialog.h"
#include <QDebug>
#include <QDir>
#include <QThread>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QThreadPool>
#include <QTimer>
#include <QGroupBox>
#include <QApplication>
#include <QPointer>
#include <QDateTime>
#include "AppLog.h"
#include "VisionWorkbenchStyle.h"
#include <vector>
#include <QFileInfo>
#include "FlowExecutor.h"

// 后台线程类，用于执行相机操作
class CameraOperationTask : public QRunnable
{
public:
    enum OperationType {
        OpenCamera,
        ApplyParams,
        DetectCameras,
        ReadGlobalCameras
    };

    CameraOperationTask(MvsImageSourceNode *node, OperationType operation)
        : m_node(node), m_operation(operation) {}

    void run() override
    {
        if (!m_node) {
            return;
        }
        switch (m_operation) {
        case OpenCamera:
            m_node->openCameraInThread();
            break;
        case ApplyParams:
            m_node->applyCameraParamsInThread();
            break;
        case DetectCameras:
            m_node->detectCamerasInThread();
            break;
        case ReadGlobalCameras:
            m_node->readGlobalCamerasInThread();
            break;
        }
    }

private:
    QPointer<MvsImageSourceNode> m_node;
    OperationType m_operation;
};

MvsImageSourceNode::MvsImageSourceNode(QObject *parent) : HalconNode(parent)
    , m_sourceType(CAMERA)
    , m_filePath("")
    , m_isDirectory(false)
    , m_currentImageIndex(0)
    , m_cameraName("")
    , m_globalCameraName("")
    , m_triggerMode(FREE_RUN)
    , m_exposureTime(10000.0)
    , m_gain(0.0)
    , m_frameRate(30.0)
    , m_pixelFormat("Mono8")
    , m_cameraOpened(false)
    , m_hCamera(nullptr)
    , m_isDetectingCameras(false)
    , m_imageWidth(0)
    , m_imageHeight(0)
    , m_outputImageWidth(0)
    , m_outputImageHeight(0)
    , m_outputPixelFormat("Mono8")
    , m_inputTrigger(false)
    , m_inputExposure(0.0)
    , m_inputGain(0.0)
    , m_cameraConsumerId(QStringLiteral("mvs_%1").arg(
          reinterpret_cast<quintptr>(this), QT_POINTER_SIZE * 2, 16, QLatin1Char('0')))
{
    setName(QStringLiteral("MVS\u56FE\u50CF\u6E90"));
    m_type = IMAGE_ACQUISITION;
    addOutputPort(QStringLiteral("\u8F93\u51FA\u56FE\u50CF"));
    // init() 由 FlowScene::createNode 统一调用，避免重复初始化
}

MvsImageSourceNode::~MvsImageSourceNode()
{
    closeCamera();
    
    // 标记相机为未使用
    if (!m_globalCameraName.isEmpty()) {
        GlobalCameraManager::instance()->markCameraUnused(m_globalCameraName, m_cameraConsumerId);
    }
    
    // 释放本地相机的 MVS SDK 资源，全局相机由GlobalCameraManager管理
    if (m_hCamera && m_globalCameraName.isEmpty()) {
        // 模拟释放 MVS SDK 资源，避免因缺少 MVS SDK 而崩溃
        VFP_DEBUG << "Releasing local camera MVS SDK resources";
        m_hCamera = nullptr;
    }
}

void MvsImageSourceNode::init()
{
    connect(GlobalCameraManager::instance(), &GlobalCameraManager::cameraChanged,
            this, [=](const QString &cameraName, const QString &deviceName) {
        QMutexLocker locker(&m_mutex);
        if (m_globalCameraName == cameraName && deviceName.isEmpty()) {
            m_cameraOpened = false;
            m_hCamera = nullptr;
            emit cameraStatusChanged(false);
        }
    }, Qt::QueuedConnection);

    // 不需要在这里读取全局相机列表，因为 createParamPanel 会调用
}

void MvsImageSourceNode::readGlobalCamerasInThread()
{
    try {
        // 获取全局相机列表
        GlobalCameraManager *manager = GlobalCameraManager::instance();
        auto cameras = manager->cameras();
        QStringList cameraNames;
        
        // 构建相机列表
        for (auto it = cameras.cbegin(); it != cameras.cend(); ++it) {
            cameraNames.append(it.key());
        }
        
        // 处理全局相机列表，移除未绑定的相机
        QStringList boundCameras;
        for (const auto &cameraName : cameraNames) {
            if (manager->isCameraBound(cameraName)) {
                boundCameras.append(cameraName);
            }
        }
        
        VFP_DEBUG << "readGlobalCamerasInThread 找到" << boundCameras.size() << "个已绑定相机";
        
        // 在主线程中更新UI
        QMetaObject::invokeMethod(this, [=]() {
            updateGlobalCameras(boundCameras);
        }, Qt::QueuedConnection);
        
    } catch (const std::exception& e) {
        VFP_DEBUG << "readGlobalCamerasInThread exception:" << e.what();
    } catch (...) {
        VFP_DEBUG << "readGlobalCamerasInThread unknown exception";
    }
}

void MvsImageSourceNode::setParam(const QString &key, const QVariant &value)
{
    QMutexLocker locker(&m_mutex);
    VFP_DEBUG << "MvsImageSourceNode::setParam key=" << key << "value=" << value;
    
    if (key == "sourceType") {
        m_sourceType = static_cast<SourceType>(value.toInt());
    } else if (key == "cameraName") {
        m_cameraName = value.toString();
    } else if (key == "globalCameraName") {
        QString oldGlobal = m_globalCameraName;
        m_globalCameraName = value.toString();
        
        // 如果旧的全局相机名非空且与新的不同，标记旧相机为未使用
        if (!oldGlobal.isEmpty() && oldGlobal != m_globalCameraName) {
            GlobalCameraManager::instance()->markCameraUnused(oldGlobal, m_cameraConsumerId);
        }
        
        // 如果新的全局相机名非空，标记新相机为已使用
        if (!m_globalCameraName.isEmpty()) {
            GlobalCameraManager::instance()->markCameraUsed(m_globalCameraName, m_cameraConsumerId);
        }
        
        // 重置打开状态，下次process会重新尝试打开
        m_cameraOpened = false;
        m_hCamera = nullptr;
    } else if (key == "exposureTime") {
        m_exposureTime = value.toDouble();
    } else if (key == "gain") {
        m_gain = value.toDouble();
    } else if (key == "frameRate") {
        m_frameRate = value.toDouble();
    } else if (key == "pixelFormat") {
        m_pixelFormat = value.toString();
    } else if (key == "triggerMode") {
        m_triggerMode = static_cast<TriggerMode>(value.toInt());
    } else if (key == "filePath") {
        m_filePath = value.toString();
    } else if (key == "imageWidth") {
        m_imageWidth = value.toInt();
    } else if (key == "imageHeight") {
        m_imageHeight = value.toInt();
    }

    emit paramChanged(key, value);
}

QVariant MvsImageSourceNode::getParam(const QString &key) const
{
    QMutexLocker locker(&m_mutex);
    
    if (key == "sourceType")
        return static_cast<int>(m_sourceType);
    if (key == "cameraName")
        return m_cameraName;
    if (key == "globalCameraName")
        return m_globalCameraName;
    if (key == "exposureTime")
        return m_exposureTime;
    if (key == "gain")
        return m_gain;
    if (key == "frameRate")
        return m_frameRate;
    if (key == "pixelFormat")
        return m_pixelFormat;
    if (key == "triggerMode")
        return static_cast<int>(m_triggerMode);
    if (key == "filePath")
        return m_filePath;
    if (key == "imageWidth")
        return m_imageWidth;
    if (key == "imageHeight")
        return m_imageHeight;
    
    return QVariant();
}

QJsonObject MvsImageSourceNode::toJson() const
{
    QMutexLocker locker(&m_mutex);
    QJsonObject json = HalconNode::toJson();
    json["sourceType"] = static_cast<int>(m_sourceType);
    json["cameraName"] = m_cameraName;
    json["globalCameraName"] = m_globalCameraName;
    json["exposureTime"] = m_exposureTime;
    json["gain"] = m_gain;
    json["frameRate"] = m_frameRate;
    json["pixelFormat"] = m_pixelFormat;
    json["triggerMode"] = static_cast<int>(m_triggerMode);
    json["filePath"] = m_filePath;
    json["imageWidth"] = m_imageWidth;
    json["imageHeight"] = m_imageHeight;
    return json;
}

void MvsImageSourceNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    QMutexLocker locker(&m_mutex);
    m_sourceType = static_cast<SourceType>(json["sourceType"].toInt(static_cast<int>(CAMERA)));
    m_cameraName = json["cameraName"].toString("");
    m_globalCameraName = json["globalCameraName"].toString("");
    m_exposureTime = json["exposureTime"].toDouble(10000.0);
    m_gain = json["gain"].toDouble(0.0);
    m_frameRate = json["frameRate"].toDouble(30.0);
    m_pixelFormat = json["pixelFormat"].toString("Mono8");
    m_triggerMode = static_cast<TriggerMode>(json["triggerMode"].toInt(static_cast<int>(FREE_RUN)));
    m_filePath = json["filePath"].toString("");
    m_imageWidth = json["imageWidth"].toInt(0);
    m_imageHeight = json["imageHeight"].toInt(0);

    // 如果加载的全局相机名非空，标记占用
    if (!m_globalCameraName.isEmpty()) {
        GlobalCameraManager::instance()->markCameraUsed(m_globalCameraName, m_cameraConsumerId);
    }
}

void MvsImageSourceNode::drawResult()
{
    // 在图像显示控件上绘制结果
    displayImage();
}

void MvsImageSourceNode::displayImage()
{
    VFP_DEBUG << "MvsImageSourceNode::displayImage";
}

void MvsImageSourceNode::run(bool autoSwitch)
{
    try {
        // 执行图像采集逻辑
        VFP_DEBUG << "开始执行MVS图像源节点";
        
        if (m_sourceType == CAMERA) {
            bool hasCamera = !m_cameraName.isEmpty() || !m_globalCameraName.isEmpty();
            
            if (!hasCamera) {
                VFP_DEBUG << "未选择相机源，跳过执行";
                return;
            }
            
            // 尝试打开相机（如果尚未打开或未设置全局相机句柄）
            {
                QMutexLocker locker(&m_mutex);
                if (!m_cameraOpened || (m_globalCameraName.isEmpty() && !m_hCamera)) {
                    locker.unlock();
                    openCamera();
                }
            }
            
            // 执行图像采集
            process();
        } else if (m_sourceType == LOCAL_FILE) {
            process();
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "MvsImageSourceNode::run exception:" << e.what();
    } catch (...) {
        VFP_DEBUG << "MvsImageSourceNode::run unknown exception";
    }
}

bool MvsImageSourceNode::process()
{
    QMutexLocker locker(&m_mutex);
    VFP_DEBUG << "MvsImageSourceNode::process - 开始执行";
    
    if (m_sourceType == LOCAL_FILE) {
        return loadImageFromFile();
    } else if (m_sourceType == CAMERA) {
        // 如果设置了全局相机，直接使用全局相机的句柄
        if (!m_globalCameraName.isEmpty()) {
            void* handle = GlobalCameraManager::instance()->getCameraHandle(m_globalCameraName);
            if (handle) {
                m_hCamera = handle;
                m_cameraOpened = true;
            } else {
                VFP_DEBUG << "全局相机句柄无效:" << m_globalCameraName;
                locker.unlock();
                openCamera();
                locker.relock();
            }
        }
        
        if (!m_cameraOpened || !m_hCamera) {
            VFP_DEBUG << "相机未打开，无法采集图像";
            return false;
        }
        
        locker.unlock();
        bool ret = grabImage();
        return ret;
    }
    
    return false;
}

void MvsImageSourceNode::updateOutputInfo()
{
    // 如果正在运行，更新输出信息
    QMutexLocker locker(&m_mutex);
    
    if (!m_imageFiles.isEmpty() && m_currentImageIndex >= 0 && m_currentImageIndex < m_imageFiles.size()) {
        QFileInfo fileInfo(m_imageFiles[m_currentImageIndex]);
        if (m_outputImage.IsInitialized()) {
            m_outputImageWidth = m_outputImage.Width();
            m_outputImageHeight = m_outputImage.Height();
        }
    } else if (m_outputImage.IsInitialized()) {
        m_outputImageWidth = m_outputImage.Width();
        m_outputImageHeight = m_outputImage.Height();
    }

    // 确保输出端口数据有效
    if (!m_outputImage.IsInitialized()) {
        VFP_DEBUG << "输出图像未初始化";
    }
}

bool MvsImageSourceNode::grabImage()
{
    VFP_DEBUG << "MvsImageSourceNode::grabImage - 开始真实取图";

    if (!m_hCamera) {
        VFP_DEBUG << "取图失败: 相机句柄为空";
        return false;
    }

    try {
        // 使用 MVS SDK 获取一帧图像
        MV_FRAME_OUT_INFO_EX frameInfo;
        memset(&frameInfo, 0, sizeof(MV_FRAME_OUT_INFO_EX));
        unsigned int payload = 0;
        {
            QMutexLocker locker(&m_mutex);
            payload = m_payloadSize;
        }
        if (payload == 0) {
            VFP_DEBUG << "取图失败: PayloadSize 为 0，相机可能尚未正确配置";
            return false;
        }

        // 帧缓冲改用 std::vector 托管：所有退出路径（含异常）都会自动释放。
        // 原实现 new[]/delete[] 且 catch 分支不释放，异常时每帧泄漏一次 payload 大小内存。
        std::vector<unsigned char> imgBufStorage(payload);
        unsigned char *imgBuf = imgBufStorage.data();
        unsigned int nDataSize = payload;

        // 软触发模式：取图前下发软件触发命令
        {
            QMutexLocker locker(&m_mutex);
            if (m_triggerMode == SOFTWARE_TRIGGER) {
                MV_CC_SetCommandValue(m_hCamera, "TriggerSoftware");
            }
        }

        // 硬触发模式：一直等待相机触发源生效产生帧（期间每 1s 检查一次流程是否停止）
        FlowExecutor *exec = ownerExecutor() ? ownerExecutor() : FlowExecutor::current();
        bool hardwareWait = false;
        if (exec) {
            hardwareWait = (exec->getFlowMode() == FlowMode::HardwareTrigger);
        }

        int nRet = MV_CC_GetOneFrameTimeout(m_hCamera, imgBuf, nDataSize, &frameInfo, 1000);
        if (nRet != MV_OK && hardwareWait) {
            // 超时未取到帧：若流程仍在运行则继续等待下一次硬件触发。
            // 单次等待用 200ms（而非 1s）：FlowExecutor::stopExecution 只能在节点边界生效，
            // 缩短单次阻塞可让“停止/退出”在 ~200ms 内响应，避免线程未退出就被销毁。
            while (nRet != MV_OK) {
                bool stillRunning = exec
                                    && exec->getState() == ExecutionState::Running;
                if (!stillRunning) {
                    break;
                }
                nRet = MV_CC_GetOneFrameTimeout(m_hCamera, imgBuf, nDataSize, &frameInfo, 200);
            }
        }
        if (nRet != MV_OK) {
            VFP_DEBUG << "取图失败，错误码:" << nRet;

            // 取图失败自动重连（连续/软触发模式下相机断开后自动恢复）
            if (tryAutoReconnect()) {
                return grabImage();
            }
            return false;
        }

        // 取图成功：重置失败计数
        m_consecutiveGrabFailures = 0;
        
        int width = static_cast<int>(frameInfo.nWidth);
        int height = static_cast<int>(frameInfo.nHeight);
        unsigned char* imgPtr = imgBuf;

        VFP_DEBUG << "取图成功: " << width << "x" << height
                  << " pixelType=0x" << QString::number(static_cast<int>(frameInfo.enPixelType), 16);

        // 根据像素格式构造 Halcon 图像
        HalconCpp::HImage image;
        switch (frameInfo.enPixelType) {
        case PixelType_Gvsp_Mono8:
            image.GenImage1("byte", width, height, imgPtr);
            break;
        case PixelType_Gvsp_Mono10:
        case PixelType_Gvsp_Mono12:
        case PixelType_Gvsp_Mono16:
            image.GenImage1("uint2", width, height, imgPtr);
            break;
        case PixelType_Gvsp_RGB8_Packed:
            image.GenImageInterleaved(imgPtr, "rgb",  width, height, 0,
                                      "byte", width, height, 0, 0, 8, 0);
            break;
        case PixelType_Gvsp_BGR8_Packed:
            image.GenImageInterleaved(imgPtr, "bgr",  width, height, 0,
                                      "byte", width, height, 0, 0, 8, 0);
            break;
        default:
            VFP_DEBUG << "不支持的像素格式 0x"
                      << QString::number(static_cast<int>(frameInfo.enPixelType), 16)
                      << "，按 Mono8 后备处理";
            image.GenImage1("byte", width, height, imgPtr);
            break;
        }

        // 更新成员 & 输出端口
        {
            QMutexLocker locker(&m_mutex);
            m_outputImage = image;
            m_outputImageWidth  = width;
            m_outputImageHeight = height;

            QString pfName = GlobalCameraManager::pixelFormatEnumToName(frameInfo.enPixelType);
            m_outputPixelFormat = pfName.isEmpty() ? QStringLiteral("Mono8") : pfName;
        }

        QSharedPointer<DataObject> output(new DataObject(DataObject::DataType::Image, QVariant()));
        output->setHImage(image);
        setOutputData(0, output);

        VFP_DEBUG << "真实取图成功:" << width << "x" << height
                  << " fmt=" << m_outputPixelFormat;
        return true;

    } catch (const std::exception& e) {
        VFP_DEBUG << "取图异常:" << e.what();
        return false;
    } catch (...) {
        VFP_DEBUG << "取图未知异常";
        return false;
    }
}

bool MvsImageSourceNode::loadImageFromFile()
{
    try {
        if (m_imageFiles.isEmpty()) {
            VFP_DEBUG << "没有要加载的图像文件";
            return false;
        }
        
        QString filePath = m_imageFiles[m_currentImageIndex];
        
        try {
            HalconCpp::HImage image;
            image.ReadImage(filePath.toLocal8Bit().data());
            
            m_outputImage = image;
            if (m_outputImage.IsInitialized()) {
                m_outputImageWidth = m_outputImage.Width();
                m_outputImageHeight = m_outputImage.Height();
            }
            
            QSharedPointer<DataObject> output(new DataObject(DataObject::DataType::Image, QVariant()));
            output->setHImage(m_outputImage);
            setOutputData(0, output);
            
            VFP_DEBUG << "加载图像成功:" << filePath;
            return true;
        } catch (...) {
            VFP_DEBUG << "加载图像失败:" << filePath;
            return false;
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "loadImageFromFile异常:" << e.what();
        return false;
    } catch (...) {
        VFP_DEBUG << "loadImageFromFile未知异常";
        return false;
    }
}

void MvsImageSourceNode::updateImageFiles()
{
    if (m_isDirectory && !m_filePath.isEmpty()) {
        QDir dir(m_filePath);
        m_imageFiles = dir.entryList({"*.bmp", "*.jpg", "*.png", "*.tif", "*.tiff"}, QDir::Files, QDir::Name);
        for (int i = 0; i < m_imageFiles.size(); i++) {
            m_imageFiles[i] = dir.absoluteFilePath(m_imageFiles[i]);
        }
    }
}

void MvsImageSourceNode::readCameraParams()
{
    QMutexLocker locker(&m_mutex);
    VFP_DEBUG << "MvsImageSourceNode::readCameraParams";

    if (!m_hCamera) {
        VFP_DEBUG << "相机句柄为空，无法读取参数";
        return;
    }

    try {
        // 使用真实 MVS SDK 读取参数
        {
            // 曝光
            MVCC_FLOATVALUE stFloat;
            memset(&stFloat, 0, sizeof(MVCC_FLOATVALUE));
            if (MV_CC_GetFloatValue(m_hCamera, "ExposureTime", &stFloat) == MV_OK) {
                m_exposureTime = stFloat.fCurValue;
            }
            // 增益
            memset(&stFloat, 0, sizeof(MVCC_FLOATVALUE));
            if (MV_CC_GetFloatValue(m_hCamera, "Gain", &stFloat) == MV_OK) {
                m_gain = stFloat.fCurValue;
            }
            // 帧率
            memset(&stFloat, 0, sizeof(MVCC_FLOATVALUE));
            if (MV_CC_GetFloatValue(m_hCamera, "AcquisitionFrameRate", &stFloat) == MV_OK) {
                m_frameRate = stFloat.fCurValue;
            }
            // 像素格式 — 使用整数枚举值读取
            {
                unsigned int hwVal = GlobalCameraManager::readPixelFormatValue(m_hCamera);
                QString hwPixelFormat = GlobalCameraManager::pixelFormatEnumToName(hwVal);
                if (!hwPixelFormat.isEmpty()) {
                    m_pixelFormat = hwPixelFormat;
                    m_outputPixelFormat = hwPixelFormat;
                    VFP_DEBUG << "读取像素格式:" << hwPixelFormat;
                } else {
                    VFP_DEBUG << "读取像素格式失败，枚举值:" << hwVal;
                }
            }
            VFP_DEBUG << "相机参数读取完成";
            VFP_DEBUG << "曝光:" << m_exposureTime << "增益:" << m_gain
                      << "帧率:" << m_frameRate << "像素格式:" << m_pixelFormat;
        }
    } catch (...) {
        VFP_DEBUG << "读取相机参数失败";
    }
}

bool MvsImageSourceNode::openCamera()
{
    QMutexLocker locker(&m_mutex);
    
    // 如果设置了全局相机，直接使用全局相机的句柄
    if (!m_globalCameraName.isEmpty()) {
        GlobalCameraManager *manager = GlobalCameraManager::instance();
        void* cameraHandle = manager->getCameraHandle(m_globalCameraName);
        if (cameraHandle) {
            m_hCamera = cameraHandle;
            m_cameraOpened = true;
            VFP_DEBUG << "直接使用全局相机句柄，相机已打开";
            emit cameraStatusChanged(true);
            return true;
        }
    }
    
    // 否则启动后台线程执行相机打开操作
    QThreadPool::globalInstance()->start(new CameraOperationTask(this, CameraOperationTask::OpenCamera));
    return true;
}

void MvsImageSourceNode::openCameraInThread()
{
    try {
        QString cameraName;
        {
            QMutexLocker locker(&m_mutex);
            cameraName = m_cameraName;
        }

        VFP_DEBUG << "正在打开本地相机:" << cameraName;

        // 使用真实 MVS SDK 枚举设备查找
        MV_CC_DEVICE_INFO_LIST stDeviceList;
        memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
        int nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
        if (nRet != MV_OK) {
            VFP_DEBUG << "枚举设备失败，错误码:" << nRet;
            emit cameraStatusChanged(false);
            return;
        }

        MV_CC_DEVICE_INFO* pTargetDevice = nullptr;
        // 在设备名中定位序号
        // "Camera N" -> N-1 索引
        int idx = cameraName.indexOf("Camera ");
        if (idx >= 0) {
            QString numStr = cameraName.mid(idx + 7);
            int spacePos = numStr.indexOf(' ');
            if (spacePos > 0) numStr = numStr.left(spacePos);
            bool ok = false;
            int n = numStr.toInt(&ok);
            if (ok && n >= 1 && static_cast<unsigned int>(n) <= stDeviceList.nDeviceNum) {
                pTargetDevice = stDeviceList.pDeviceInfo[n - 1];
            }
        }

        if (!pTargetDevice) {
            VFP_DEBUG << "未找到匹配的设备:" << cameraName;
            emit cameraStatusChanged(false);
            return;
        }

        void* hCamera = nullptr;
        nRet = MV_CC_CreateHandle(&hCamera, pTargetDevice);
        if (nRet != MV_OK) {
            VFP_DEBUG << "创建相机句柄失败，错误码:" << nRet;
            emit cameraStatusChanged(false);
            return;
        }

        // 打开设备
        nRet = MV_CC_OpenDevice(hCamera);
        if (nRet != MV_OK) {
            VFP_DEBUG << "打开设备失败，错误码:" << nRet;
            MV_CC_DestroyHandle(hCamera);
            emit cameraStatusChanged(false);
            return;
        }

        // 开始采集
        nRet = MV_CC_StartGrabbing(hCamera);
        if (nRet != MV_OK) {
            VFP_DEBUG << "开始采集失败，错误码:" << nRet;
        }

        // 查询单帧数据量，动态分配取图缓冲（高分辨率相机 >10MB 时原固定缓冲会取图失败）
        {
            MVCC_INTVALUE stIntParam;
            memset(&stIntParam, 0, sizeof(MVCC_INTVALUE));
            if (MV_CC_GetIntValue(hCamera, "PayloadSize", &stIntParam) == MV_OK
                && stIntParam.nCurValue > 0) {
                QMutexLocker locker(&m_mutex);
                m_payloadSize = static_cast<unsigned int>(stIntParam.nCurValue);
                VFP_DEBUG << "相机 PayloadSize:" << m_payloadSize;
            }
        }

        // 按触发模式配置相机寄存器（TriggerMode/TriggerSource）
        {
            QMutexLocker locker(&m_mutex);
            m_hCamera = hCamera;
            m_cameraOpened = true;
        }
        applyTriggerConfig();

        VFP_DEBUG << "本地相机打开成功:" << cameraName;
        emit cameraStatusChanged(true);
    } catch (const std::exception& e) {
        VFP_DEBUG << "打开相机时发生异常:" << e.what();
        emit cameraStatusChanged(false);
    } catch (...) {
        VFP_DEBUG << "打开相机时发生未知异常";
        emit cameraStatusChanged(false);
    }
}

void MvsImageSourceNode::closeCamera()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_hCamera && m_globalCameraName.isEmpty()) {
        // 仅关闭本地相机（非全局相机）
        MV_CC_StopGrabbing(m_hCamera);
        MV_CC_CloseDevice(m_hCamera);
        MV_CC_DestroyHandle(m_hCamera);
        m_hCamera = nullptr;
        m_cameraOpened = false;
        VFP_DEBUG << "本地相机关闭成功";
    } else if (!m_globalCameraName.isEmpty()) {
        // 全局相机由 GlobalCameraManager 管理，此处仅清除本地引用
        m_hCamera = nullptr;
        m_cameraOpened = false;
        VFP_DEBUG << "清除全局相机引用:" << m_globalCameraName;
    }
    
    emit cameraStatusChanged(false);
}

void MvsImageSourceNode::applyTriggerConfig()
{
    void *hCamera = nullptr;
    TriggerMode mode = FREE_RUN;
    {
        QMutexLocker locker(&m_mutex);
        hCamera = m_hCamera;
        mode = m_triggerMode;
    }
    if (!hCamera) return;

    switch (mode) {
    case HARDWARE_TRIGGER:
        // 硬件触发：外部信号（默认 Line0）驱动帧
        MV_CC_SetEnumValue(hCamera, "TriggerMode", MV_TRIGGER_MODE_ON);
        MV_CC_SetEnumValue(hCamera, "TriggerSource", MV_TRIGGER_SOURCE_LINE0);
        VFP_DEBUG << "相机已配置为硬触发 (Line0)";
        break;
    case SOFTWARE_TRIGGER:
        // 软触发：流程执行时下发软件触发命令（见 grabImage）
        MV_CC_SetEnumValue(hCamera, "TriggerMode", MV_TRIGGER_MODE_ON);
        MV_CC_SetEnumValue(hCamera, "TriggerSource", MV_TRIGGER_SOURCE_SOFTWARE);
        VFP_DEBUG << "相机已配置为软触发";
        break;
    case FREE_RUN:
    default:
        // 自由运行：连续出帧
        MV_CC_SetEnumValue(hCamera, "TriggerMode", MV_TRIGGER_MODE_OFF);
        VFP_DEBUG << "相机已配置为自由运行";
        break;
    }
}

void MvsImageSourceNode::applyCameraParams()
{
    QThreadPool::globalInstance()->start(new CameraOperationTask(this, CameraOperationTask::ApplyParams));
}

void MvsImageSourceNode::applyCameraParamsInThread()
{
    VFP_DEBUG << "applyCameraParamsInThread - 开始";

    void* hCamera = nullptr;
    QString pixelFormat;
    {
        QMutexLocker locker(&m_mutex);
        hCamera = m_hCamera;
        pixelFormat = m_pixelFormat;
    }

    if (!hCamera) {
        VFP_DEBUG << "相机句柄为空";
        emit paramsApplyFailed(QStringLiteral("\u76F8\u673A\u53E5\u67C4\u4E3A\u7A7A\uFF0C\u65E0\u6CD5\u5E94\u7528\u53C2\u6570"));
        return;
    }

    // 先检查当前 Flow 是否处于连续模式或正在运行
    bool skipPixelFormat = false;
    if (FlowExecutor *exec = ownerExecutor() ? ownerExecutor() : FlowExecutor::current()) {
        FlowMode mode = exec->getFlowMode();
        ExecutionState state = exec->getState();
        skipPixelFormat = (mode == FlowMode::Continuous) || (state == ExecutionState::Running);
    }

    bool wroteExposure = false, wroteGain = false, wroteFps = false, wrotePf = false;

    // 曝光
    {
        double val;
        {
            QMutexLocker locker(&m_mutex);
            val = m_exposureTime;
        }
        wroteExposure = (MV_CC_SetFloatValue(hCamera, "ExposureTime", static_cast<float>(val)) == MV_OK);
    }

    // 增益
    {
        double val;
        {
            QMutexLocker locker(&m_mutex);
            val = m_gain;
        }
        wroteGain = (MV_CC_SetFloatValue(hCamera, "Gain", static_cast<float>(val)) == MV_OK);
    }

    // 帧率
    {
        double val;
        {
            QMutexLocker locker(&m_mutex);
            val = m_frameRate;
        }
        int fpsRet = MV_CC_SetFloatValue(hCamera, "AcquisitionFrameRate", static_cast<float>(val));
        wroteFps = (fpsRet == MV_OK);
        if (!wroteFps) {
            VFP_DEBUG << "\u8BBE\u7F6E\u5E27\u7387\u5931\u8D25\uFF08\u90E8\u5206\u76F8\u673A\u4E0D\u652F\u6301\uFF09\uFF0C\u9519\u8BEF\u7801:" << fpsRet;
        }
    }

    // 像素格式
    if (!skipPixelFormat) {
        unsigned int pfVal = GlobalCameraManager::pixelFormatNameToEnum(pixelFormat);
        if (pfVal != 0) {
            // 写入前先停止采集，写入后恢复
            MV_CC_StopGrabbing(hCamera);
            int pfRet = MV_CC_SetEnumValue(hCamera, "PixelFormat", pfVal);
            MV_CC_StartGrabbing(hCamera);
            wrotePf = (pfRet == MV_OK);

            if (wrotePf) {
                // 读回验证
                unsigned int readBack = GlobalCameraManager::readPixelFormatValue(hCamera);
                QString appliedPixelFormat = GlobalCameraManager::pixelFormatEnumToName(readBack);
                if (appliedPixelFormat.isEmpty()) {
                    appliedPixelFormat = pixelFormat;
                }

                {
                    QMutexLocker locker(&m_mutex);
                    m_pixelFormat = appliedPixelFormat;
                    m_outputPixelFormat = appliedPixelFormat;
                }

                // 更新全局相机缓存
                {
                    QMutexLocker locker(&m_mutex);
                    QString globalName = m_globalCameraName;
                    if (!globalName.isEmpty()) {
                        CameraParams cachedParams;
                        cachedParams.pixelFormat = appliedPixelFormat;
                        GlobalCameraManager::instance()->updateCameraCache(globalName, cachedParams);
                    }
                }

                emit paramChanged("pixelFormat", appliedPixelFormat);
            } else {
                VFP_DEBUG << "设置像素格式到设备失败，错误码:" << pfRet;
            }
        } else {
            VFP_DEBUG << "未知的像素格式名称:" << pixelFormat;
        }
    }

    QStringList failed;
    if (!wroteExposure) failed << "\u66DD\u5149\u65F6\u95F4";
    if (!wroteGain) failed << "\u589E\u76CA";
    if (!wroteFps) failed << "\u5E27\u7387";
    if (!wrotePf && !skipPixelFormat) failed << "\u50CF\u7D20\u683C\u5F0F";
    if (skipPixelFormat) {
        VFP_DEBUG << "\u5F53\u524D\u4E3A\u8FDE\u7EED\u6A21\u5F0F\u6216\u6D41\u7A0B\u6B63\u5728\u8FD0\u884C\uFF0C\u8DF3\u8FC7\u50CF\u7D20\u683C\u5F0F\u5199\u5165";
    }

    if (failed.isEmpty()) {
        VFP_DEBUG << "\u53C2\u6570\u5E94\u7528\u6210\u529F";
        emit paramsApplied();
    } else {
        QString reason = QStringLiteral("\u53C2\u6570\u5E94\u7528\u5931\u8D25: ")
                         + failed.join(QStringLiteral(", "))
                         + QStringLiteral(" \u5199\u5165\u5931\u8D25");
        VFP_DEBUG << reason;
        emit paramsApplyFailed(reason);
    }
}

QStringList MvsImageSourceNode::detectCamerasInThread()
{
    VFP_DEBUG << "detectCamerasInThread - 使用真实 MVS SDK 枚举设备";
    QStringList cameraList;
    try {
        MV_CC_DEVICE_INFO_LIST stDeviceList;
        memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
        int nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
        if (nRet != MV_OK) {
            VFP_DEBUG << "枚举设备失败，错误码:" << nRet;
            return cameraList;
        }

        VFP_DEBUG << "枚举到" << stDeviceList.nDeviceNum << "个设备";
        for (unsigned int i = 0; i < stDeviceList.nDeviceNum; i++) {
            MV_CC_DEVICE_INFO* pDeviceInfo = stDeviceList.pDeviceInfo[i];
            if (!pDeviceInfo) continue;

            QString deviceName;
            if (pDeviceInfo->nTLayerType == MV_GIGE_DEVICE) {
                const auto& gigEInfo = pDeviceInfo->SpecialInfo.stGigEInfo;
                QString ip = QString("%1.%2.%3.%4")
                    .arg((gigEInfo.nCurrentIp >> 24) & 0xFF)
                    .arg((gigEInfo.nCurrentIp >> 16) & 0xFF)
                    .arg((gigEInfo.nCurrentIp >> 8) & 0xFF)
                    .arg(gigEInfo.nCurrentIp & 0xFF);
                deviceName = QString("GigE: Camera %1 (IP: %2)").arg(i + 1).arg(ip);
            } else if (pDeviceInfo->nTLayerType == MV_USB_DEVICE) {
                deviceName = QString("USB: Camera %1").arg(i + 1);
            }
            if (!deviceName.isEmpty()) {
                cameraList.append(deviceName);
                VFP_DEBUG << "找到设备:" << deviceName;
            }
        }
    } catch (const std::exception& e) {
        VFP_DEBUG << "检测相机时发生异常:" << e.what();
    } catch (...) {
        VFP_DEBUG << "检测相机时发生未知异常";
    }
    return cameraList;
}

QStringList MvsImageSourceNode::getAvailableCameras()
{
    QStringList cameraList;
    
    // 使用真实 MVS SDK 枚举设备
    try {
        MV_CC_DEVICE_INFO_LIST stDeviceList;
        memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
        int nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
        if (nRet != MV_OK) {
            return cameraList;
        }

        for (unsigned int i = 0; i < stDeviceList.nDeviceNum; i++) {
            MV_CC_DEVICE_INFO* pDeviceInfo = stDeviceList.pDeviceInfo[i];
            if (!pDeviceInfo) continue;

            QString deviceName;
            if (pDeviceInfo->nTLayerType == MV_GIGE_DEVICE) {
                const auto& gigEInfo = pDeviceInfo->SpecialInfo.stGigEInfo;
                QString ip = QString("%1.%2.%3.%4")
                    .arg((gigEInfo.nCurrentIp >> 24) & 0xFF)
                    .arg((gigEInfo.nCurrentIp >> 16) & 0xFF)
                    .arg((gigEInfo.nCurrentIp >> 8) & 0xFF)
                    .arg(gigEInfo.nCurrentIp & 0xFF);
                deviceName = QString("GigE: Camera %1 (IP: %2)").arg(i + 1).arg(ip);
            } else if (pDeviceInfo->nTLayerType == MV_USB_DEVICE) {
                deviceName = QString("USB: Camera %1").arg(i + 1);
            }
            if (!deviceName.isEmpty()) {
                cameraList.append(deviceName);
            }
        }
    } catch (...) {
        // silently ignore
    }
    
    return cameraList;
}

QStringList MvsImageSourceNode::getGlobalCameras()
{
    GlobalCameraManager *manager = GlobalCameraManager::instance();
    auto cameras = manager->cameras();
    QStringList cameraNames;
    for (auto it = cameras.cbegin(); it != cameras.cend(); ++it) {
        cameraNames.append(it.key());
    }
    return cameraNames;
}

QString MvsImageSourceNode::getGlobalCameraName()
{
    QMutexLocker locker(&m_mutex);
    return m_globalCameraName;
}

void MvsImageSourceNode::setGlobalCameraName(const QString &name)
{
    QString oldCamera;
    {
        QMutexLocker locker(&m_mutex);
        oldCamera = m_globalCameraName;
        if (oldCamera == name) return;
        m_globalCameraName = name;
        m_cameraOpened = false;
        m_hCamera = nullptr;
    }

    // 标记占用状态
    if (!oldCamera.isEmpty()) {
        GlobalCameraManager::instance()->markCameraUnused(oldCamera, m_cameraConsumerId);
    }
    if (!name.isEmpty()) {
        GlobalCameraManager::instance()->markCameraUsed(name, m_cameraConsumerId);
        readCameraParams();
    }

    refreshPixelFormatCombo();
}

void MvsImageSourceNode::updateGlobalCameras(const QStringList &cameraNames)
{
    VFP_DEBUG << "updateGlobalCameras cameraNames=" << cameraNames;
    emit globalCamerasUpdated(cameraNames);
}

void MvsImageSourceNode::refreshPixelFormatEnabled()
{
    if (!m_pixelFormatCombo) return;

    bool running = false;
    if (FlowExecutor *exec = ownerExecutor() ? ownerExecutor() : FlowExecutor::current()) {
        FlowMode mode = exec->getFlowMode();
        ExecutionState state = exec->getState();
        running = (mode == FlowMode::Continuous) || (state == ExecutionState::Running);
    }

    m_pixelFormatCombo->setEnabled(!running);
    m_pixelFormatCombo->setToolTip(
        running ? QStringLiteral("\u5F53\u524D\u4E3A\u8FDE\u7EED\u6A21\u5F0F\u6216\u6D41\u7A0B\u6B63\u5728\u8FD0\u884C\uFF0C\u6682\u65F6\u65E0\u6CD5\u4FEE\u6539\u50CF\u7D20\u683C\u5F0F")
                : QStringLiteral("\u9009\u62E9\u50CF\u7D20\u683C\u5F0F"));
}

void MvsImageSourceNode::refreshPixelFormatCombo()
{
    if (!m_pixelFormatCombo) return;

    QSignalBlocker blocker(m_pixelFormatCombo);
    m_pixelFormatCombo->clear();

    // 获取当前绑定的全局相机句柄
    QString cameraName;
    {
        QMutexLocker locker(&m_mutex);
        cameraName = m_globalCameraName;
    }

    void *hCam = nullptr;
    if (!cameraName.isEmpty()) {
        hCam = GlobalCameraManager::instance()->getCameraHandle(cameraName);
    }

    QList<QPair<QString, unsigned int>> formats = GlobalCameraManager::getSupportedPixelFormats(hCam);
    for (const auto &pair : formats) {
        m_pixelFormatCombo->addItem(pair.first, pair.second);
    }

    if (formats.isEmpty()) {
        m_pixelFormatCombo->setEnabled(false);
        return;
    }

    // 尝试从硬件读取当前像素格式并选中
    QString currentPf;
    if (hCam) {
        unsigned int curEnum = GlobalCameraManager::readPixelFormatValue(hCam);
        currentPf = GlobalCameraManager::pixelFormatEnumToName(curEnum);
    }

    if (currentPf.isEmpty()) {
        QMutexLocker locker(&m_mutex);
        currentPf = m_pixelFormat;
    }

    int idx = m_pixelFormatCombo->findText(currentPf);
    if (idx >= 0) {
        m_pixelFormatCombo->setCurrentIndex(idx);
    }

    VisionWorkbenchStyle::markComboDirty(m_pixelFormatCombo, false);
}

void MvsImageSourceNode::setFilePath(const QString &path)
{
    m_filePath = path;
    updateImageFiles();
}

QString MvsImageSourceNode::getFilePath() const
{
    return m_filePath;
}

QString MvsImageSourceNode::name() const
{
    return QStringLiteral("MVS\u56FE\u50CF\u6E90");
}

NodeBase::NodeType MvsImageSourceNode::type() const
{
    return IMAGE_ACQUISITION;
}

bool MvsImageSourceNode::execute()
{
    return HalconNode::execute();
}

QString MvsImageSourceNode::getSelectedCamera()
{
    QMutexLocker locker(&m_mutex);
    return m_cameraName;
}

void MvsImageSourceNode::setSelectedCamera(const QString &camera)
{
    {
        QMutexLocker locker(&m_mutex);
        m_cameraName = camera;
    }
    emit paramChanged("cameraName", camera);
}

bool MvsImageSourceNode::isCameraOpen()
{
    QMutexLocker locker(&m_mutex);
    return m_cameraOpened;
}

QWidget *MvsImageSourceNode::createParamPanel()
{
    QWidget *panel = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(panel);

    // ── 图像源类型 ──
    QLabel *typeLabel = new QLabel(QStringLiteral("\u56FE\u50CF\u6E90\u7C7B\u578B:"));
    QComboBox *typeCombo = new QComboBox();
    typeCombo->addItem(QStringLiteral("\u672C\u5730\u6587\u4EF6"), LOCAL_FILE);
    typeCombo->addItem(QStringLiteral("\u76F8\u673A"), CAMERA);
    typeCombo->setCurrentIndex(m_sourceType);
    layout->addWidget(typeLabel);
    layout->addWidget(typeCombo);

    // ── 本地相机选择 ──
    QLabel *cameraLabel = new QLabel(QStringLiteral("\u672C\u5730\u76F8\u673A:"));
    QComboBox *cameraCombo = new QComboBox();
    cameraCombo->setObjectName("cameraCombo");
    layout->addWidget(cameraLabel);
    layout->addWidget(cameraCombo);

    auto refillCameraCombo = [cameraCombo, this](const QString &trySelect) {
        cameraCombo->clear();
        QStringList cameras = getAvailableCameras();
        if (cameras.isEmpty()) {
            cameraCombo->addItem(QStringLiteral("\u672A\u68C0\u6D4B\u5230\u76F8\u673A"), "");
        } else {
            cameraCombo->addItem(QStringLiteral("\u8BF7\u9009\u62E9\u76F8\u673A"), "");
            for (const QString &cam : cameras) {
                cameraCombo->addItem(cam, cam);
            }
        }
        if (!trySelect.isEmpty()) {
            int idx = cameraCombo->findText(trySelect);
            if (idx >= 0) cameraCombo->setCurrentIndex(idx);
        }
    };

    // ── 刷新按钮 ──
    QPushButton *detectButton = new QPushButton(QStringLiteral("\u5237\u65B0\u76F8\u673A\u5217\u8868"));
    layout->addWidget(detectButton);

    QPushButton *openCfgButton = new QPushButton(QStringLiteral("\u6253\u5F00\u300CMVS\u76F8\u673A\u914D\u7F6E\u300D\u2026"));
    openCfgButton->setToolTip(QStringLiteral("\u4E0E\u83DC\u5355 \u65B9\u6848\u2192MVS\u76F8\u673A\u914D\u7F6E \u76F8\u540C\u5BF9\u8BDD\u6846"));
    layout->addWidget(openCfgButton);

    // ── 文件路径 ──
    QLabel *fileLabel = new QLabel(QStringLiteral("\u6587\u4EF6\u8DEF\u5F84:"));
    QLineEdit *fileEdit = new QLineEdit(m_filePath);
    fileEdit->setObjectName("filePathEdit");
    QPushButton *browseButton = new QPushButton(QStringLiteral("\u6D4F\u89C8\u2026"));
    QHBoxLayout *fileLayout = new QHBoxLayout();
    fileLayout->addWidget(fileEdit);
    fileLayout->addWidget(browseButton);
    layout->addWidget(fileLabel);
    layout->addLayout(fileLayout);

    // ── 全局相机绑定 ──
    auto refillGlobalCombo = [](QComboBox *cb, const QString &tryPreserve) {
        cb->clear();
        cb->addItem(QStringLiteral("\u65E0"), QString());
        auto cameras = GlobalCameraManager::instance()->cameras();
        for (auto it = cameras.cbegin(); it != cameras.cend(); ++it) {
            cb->addItem(it.key(), it.key());
        }
        const QString target = tryPreserve.isEmpty()
            ? QString() : tryPreserve;
        const int idx = cb->findText(target);
        if (idx >= 0) {
            cb->setCurrentIndex(idx);
        }
    };

    QGroupBox *bindGroup =
        new QGroupBox(QStringLiteral("\u7ED1\u5B9A\u5168\u5C40\u76F8\u673A\u5B9E\u4F8B\uFF08\u5171\u7528\u83DC\u5355\u4E2D\u5DF2\u914D\u7F6E\u7684\u53E5\u67C4\uFF09"));
    QVBoxLayout *bindLay = new QVBoxLayout(bindGroup);
    QLabel *hint = new QLabel(
        QStringLiteral("\u8BF7\u5148\u5728\u83DC\u5355 <b>\u65B9\u6848 \u2192 MVS\u76F8\u673A\u914D\u7F6E</b> \u4E2D\u4E3A Camera1\uFF5ECamera8 \u9009\u62E9\u7269\u7406\u8BBE\u5907\u3001"
                       "\u7ED1\u5B9A\u5E76\u6253\u5F00\uFF1B\u672C\u5DE5\u5177\u4EC5\u4ECE\u5217\u8868\u4E2D<strong>\u5F15\u7528</strong>\u8BE5\u5B9E\u4F8B\uFF0C\u4E0D\u518D\u5355\u72EC\u5EFA\u4E00\u5957\u8BBE\u5907\u63A5\u53E3\u3002"));
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);

    QLabel *globalCameraLabel = new QLabel(QStringLiteral("\u9009\u7528\u5168\u5C40\u76F8\u673A:"));
    QComboBox *globalCameraCombo = new QComboBox();
    globalCameraCombo->setObjectName("globalCameraCombo");
    globalCameraCombo->setToolTip(
        QStringLiteral("\u5BF9\u5E94 GlobalCameraManager \u4E2D\u7684\u547D\u540D\u69FD\u4F4D\uFF1B\u91C7\u96C6\u65F6\u4F7F\u7528 getCameraHandle(\u540D\u79F0)"));

    QString currentCamera = getGlobalCameraName();
    refillGlobalCombo(globalCameraCombo, currentCamera);

    QHBoxLayout *bindBtnRow = new QHBoxLayout();
    QPushButton *openGlobalCfgBtn = new QPushButton(QStringLiteral("\u6253\u5F00\u300CMVS\u76F8\u673A\u914D\u7F6E\u300D\u2026"));
    openGlobalCfgBtn->setToolTip(QStringLiteral("\u4E0E\u83DC\u5355 \u65B9\u6848\u2192MVS\u76F8\u673A\u914D\u7F6E \u76F8\u540C\u5BF9\u8BDD\u6846"));
    QPushButton *refreshGlobalCamerasButton = new QPushButton(QStringLiteral("\u5237\u65B0\u5217\u8868"));
    bindBtnRow->addWidget(openGlobalCfgBtn);
    bindBtnRow->addWidget(refreshGlobalCamerasButton);

    bindLay->addWidget(hint);
    bindLay->addWidget(globalCameraLabel);
    bindLay->addWidget(globalCameraCombo);
    bindLay->addLayout(bindBtnRow);
    layout->addWidget(bindGroup);
    
    // 曝光时间
    QLabel *exposureLabel = new QLabel("\u66DD\u5149\u65F6\u95F4 (\u03BCs):");
    QLineEdit *exposureEdit = new QLineEdit(QString::number(getParam("exposureTime").toDouble()));
    exposureEdit->setObjectName("exposureEdit");
    layout->addWidget(exposureLabel);
    layout->addWidget(exposureEdit);
    
    // 增益
    QLabel *gainLabel = new QLabel("\u589E\u76CA:");
    QLineEdit *gainEdit = new QLineEdit(QString::number(getParam("gain").toDouble()));
    gainEdit->setObjectName("gainEdit");
    layout->addWidget(gainLabel);
    layout->addWidget(gainEdit);
    
    // 帧率
    QLabel *frameRateLabel = new QLabel("\u5E27\u7387:");
    QLineEdit *frameRateEdit = new QLineEdit(QString::number(getParam("frameRate").toDouble()));
    frameRateEdit->setObjectName("frameRateEdit");
    layout->addWidget(frameRateLabel);
    layout->addWidget(frameRateEdit);
    
    // 像素格式
    QLabel *pixelFormatLabel = new QLabel("\u50CF\u7D20\u683C\u5F0F:");
    QComboBox *pixelFormatCombo = new QComboBox();
    pixelFormatCombo->setObjectName("pixelFormatCombo");
    m_pixelFormatCombo = pixelFormatCombo;
    refreshPixelFormatCombo(); // 根据当前绑定的全局相机填充
    pixelFormatCombo->setCurrentText(getParam("pixelFormat").toString());
    layout->addWidget(pixelFormatLabel);
    layout->addWidget(pixelFormatCombo);
    
    QPushButton *applyParamsButton = new QPushButton(QStringLiteral("\u5E94\u7528\u53C2\u6570\u5230\u76F8\u673A\uFF08\u7ECF\u5168\u5C40\u7BA1\u7406\u5668\uFF09"));
    applyParamsButton->setStyleSheet(VisionWorkbenchStyle::buttonSuccessStyle());
    layout->addWidget(applyParamsButton);

    // 参数应用状态提示
    QLabel *applyStatusLabel = new QLabel();
    applyStatusLabel->setWordWrap(true);
    applyStatusLabel->setVisible(false);
    layout->addWidget(applyStatusLabel);

    // 脏标记追踪
    auto clearDirty = [exposureEdit, gainEdit, frameRateEdit, pixelFormatCombo]() {
        VisionWorkbenchStyle::markInputDirty(exposureEdit, false);
        VisionWorkbenchStyle::markInputDirty(gainEdit, false);
        VisionWorkbenchStyle::markInputDirty(frameRateEdit, false);
        VisionWorkbenchStyle::markComboDirty(pixelFormatCombo, false);
    };

    // 反向联动
    connect(this, &MvsImageSourceNode::paramChanged, panel,
            [clearDirty, exposureEdit, gainEdit, frameRateEdit, pixelFormatCombo](
                const QString &name, const QVariant &value) {
        QSignalBlocker b1(exposureEdit);
        QSignalBlocker b2(gainEdit);
        QSignalBlocker b3(frameRateEdit);
        QSignalBlocker b4(pixelFormatCombo);
        if (name == "exposureTime")
            exposureEdit->setText(QString::number(value.toDouble()));
        else if (name == "gain")
            gainEdit->setText(QString::number(value.toDouble()));
        else if (name == "frameRate")
            frameRateEdit->setText(QString::number(value.toDouble()));
        else if (name == "pixelFormat") {
            const int idx = pixelFormatCombo->findText(value.toString());
            if (idx >= 0) pixelFormatCombo->setCurrentIndex(idx);
        }
        clearDirty();
    }, Qt::QueuedConnection);

    // 应用成功 → 清除脏标记 + 绿色提示 3 秒后隐藏
    connect(this, &MvsImageSourceNode::paramsApplied, panel, [=]() {
        clearDirty();
        applyStatusLabel->setStyleSheet("color: #4CAF50; font-weight: bold;");
        applyStatusLabel->setText(QStringLiteral("\u2714 \u53C2\u6570\u5E94\u7528\u6210\u529F"));
        applyStatusLabel->setVisible(true);
        QTimer::singleShot(3000, applyStatusLabel, [=]() {
            applyStatusLabel->setVisible(false);
        });
    });

    // 应用失败 → 显示红色错误
    connect(this, &MvsImageSourceNode::paramsApplyFailed, panel, [=](const QString &reason) {
        applyStatusLabel->setStyleSheet("color: #F44336; font-weight: bold;");
        applyStatusLabel->setText(QStringLiteral("\u2716 ") + reason);
        applyStatusLabel->setVisible(true);
    });

    // 各项输入的文本修改 → 标记脏
    connect(exposureEdit, &QLineEdit::textChanged, panel, [exposureEdit]() {
        VisionWorkbenchStyle::markInputDirty(exposureEdit, true);
    });
    connect(gainEdit, &QLineEdit::textChanged, panel, [gainEdit]() {
        VisionWorkbenchStyle::markInputDirty(gainEdit, true);
    });
    connect(frameRateEdit, &QLineEdit::textChanged, panel, [frameRateEdit]() {
        VisionWorkbenchStyle::markInputDirty(frameRateEdit, true);
    });
    connect(pixelFormatCombo, &QComboBox::currentTextChanged, panel, [pixelFormatCombo]() {
        VisionWorkbenchStyle::markComboDirty(pixelFormatCombo, true);
    });

    // 应用按钮 → 收集参数并写入
    connect(applyParamsButton, &QPushButton::clicked, panel, [=]() {
        {
            QMutexLocker locker(&m_mutex);
            m_exposureTime = exposureEdit->text().toDouble();
            m_gain = gainEdit->text().toDouble();
            m_frameRate = frameRateEdit->text().toDouble();
            m_pixelFormat = pixelFormatCombo->currentText();
        }
        applyCameraParams();
    });

    // 全局相机选择变更
    connect(globalCameraCombo, &QComboBox::currentTextChanged, panel, [=](const QString &text) {
        QString camName = (text == QStringLiteral("\u65E0")) ? QString() : text;
        setGlobalCameraName(camName);
        refreshPixelFormatCombo();
    });

    // 全局相机刷新
    connect(refreshGlobalCamerasButton, &QPushButton::clicked, panel, [=]() {
        QString oldCamera = getGlobalCameraName();
        refillGlobalCombo(globalCameraCombo, oldCamera);
    });

    // 打开 MVS 相机配置对话框
    connect(openGlobalCfgBtn, &QPushButton::clicked, panel, [=]() {
        GlobalCameraDialog dialog;
        if (dialog.exec() == QDialog::Accepted) {
            refillGlobalCombo(globalCameraCombo, getGlobalCameraName());
        }
    });

    // 打开 MVS 相机配置对话框（参数面板内的按钮）
    connect(openCfgButton, &QPushButton::clicked, panel, [=]() {
        GlobalCameraDialog dialog;
        if (dialog.exec() == QDialog::Accepted) {
            refillGlobalCombo(globalCameraCombo, getGlobalCameraName());
        }
    });

    // 类型切换
    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), panel, [=](int index) {
        setParam("sourceType", index);
        cameraCombo->setVisible(index == CAMERA);
        cameraLabel->setVisible(index == CAMERA);
        detectButton->setVisible(index == CAMERA);
        fileLabel->setVisible(index == LOCAL_FILE);
        fileEdit->setVisible(index == LOCAL_FILE);
        fileLayout->parentWidget() ? void() : void();
        browseButton->setVisible(index == LOCAL_FILE);
    });

    // 本地相机选择
    connect(cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), panel, [=](int index) {
        QString cam = cameraCombo->itemData(index).toString();
        if (!cam.isEmpty()) {
            setSelectedCamera(cam);
        }
    });

    // 刷新相机检测
    connect(detectButton, &QPushButton::clicked, panel, [=]() {
        detectButton->setEnabled(false);
        detectButton->setText(QStringLiteral("\u6B63\u5728\u68C0\u6D4B\u2026"));
        QThreadPool::globalInstance()->start(new CameraOperationTask(this, CameraOperationTask::DetectCameras));
        QTimer::singleShot(3000, detectButton, [=]() {
            detectButton->setEnabled(true);
            detectButton->setText(QStringLiteral("\u5237\u65B0\u76F8\u673A\u5217\u8868"));
        });
    });

    // 浏览文件
    connect(browseButton, &QPushButton::clicked, panel, [=]() {
        QString path = QFileDialog::getExistingDirectory(panel,
            QStringLiteral("\u9009\u62E9\u56FE\u50CF\u6587\u4EF6\u5939"));
        if (!path.isEmpty()) {
            fileEdit->setText(path);
            setFilePath(path);
        }
    });

    // 初始可见性
    cameraCombo->setVisible(m_sourceType == CAMERA);
    cameraLabel->setVisible(m_sourceType == CAMERA);
    detectButton->setVisible(m_sourceType == CAMERA);
    fileLabel->setVisible(m_sourceType == LOCAL_FILE);
    fileEdit->setVisible(m_sourceType == LOCAL_FILE);
    browseButton->setVisible(m_sourceType == LOCAL_FILE);

    // 初始填充本地相机
    refillCameraCombo(m_cameraName);

    // 初始检测相机
    QTimer::singleShot(500, panel, [=]() {
        QStringList cameras = getAvailableCameras();
        if (!cameras.isEmpty()) {
            refillCameraCombo(m_cameraName);
        }
    });

    // 响应全局相机刷新结果
    connect(this, &MvsImageSourceNode::globalCamerasUpdated, panel, [=](const QStringList &cameraNames) {
        refillGlobalCombo(globalCameraCombo, getGlobalCameraName());
    });

    refreshPixelFormatEnabled();
    layout->addStretch();
    return panel;
}

void MvsImageSourceNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;

    QLineEdit *exposureEdit = panel->findChild<QLineEdit*>("exposureEdit");
    QLineEdit *gainEdit = panel->findChild<QLineEdit*>("gainEdit");
    QLineEdit *frameRateEdit = panel->findChild<QLineEdit*>("frameRateEdit");
    QComboBox *pixelFormatCombo = panel->findChild<QComboBox*>("pixelFormatCombo");
    QComboBox *cameraCombo = panel->findChild<QComboBox*>("cameraCombo");

    if (exposureEdit) exposureEdit->setText(QString::number(m_exposureTime));
    if (gainEdit) gainEdit->setText(QString::number(m_gain));
    if (frameRateEdit) frameRateEdit->setText(QString::number(m_frameRate));
    if (pixelFormatCombo) {
        QSignalBlocker blocker(pixelFormatCombo);
        int idx = pixelFormatCombo->findText(m_pixelFormat);
        if (idx >= 0) pixelFormatCombo->setCurrentIndex(idx);
    }

    refreshPixelFormatEnabled();
}

bool MvsImageSourceNode::tryAutoReconnect()
{
    ++m_consecutiveGrabFailures;

    // 节流：距上次重连不足 3 秒则等待（避免断开时高频重试风暴）
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    constexpr qint64 kReconnectIntervalMs = 3000;
    if (now - m_lastReconnectMs < kReconnectIntervalMs) {
        return false;
    }
    // 最多连续重连 3 次；若 3 次仍失败，重置节流等待下次触发
    if (m_consecutiveGrabFailures > 3) {
        m_consecutiveGrabFailures = 1;
        m_lastReconnectMs = now;
        return false;
    }

    m_lastReconnectMs = now;
    VFP_DEBUG << "相机取图失败，尝试自动重连 (第" << m_consecutiveGrabFailures << "次)...";

    // 仅在未使用全局相机时自动重连本地相机
    if (!m_globalCameraName.isEmpty()) {
        // 全局相机：请求 GlobalCameraManager 重新打开
        GlobalCameraManager *gcm = GlobalCameraManager::instance();
        const QString cam = m_globalCameraName;
        if (gcm->isCameraOpen(cam)) {
            gcm->closeCamera(cam);
            QThread::msleep(300);
        }
        gcm->openCamera(cam);
        void *handle = gcm->getCameraHandle(cam);
        {
            QMutexLocker locker(&m_mutex);
            m_hCamera = handle;
            m_cameraOpened = (handle != nullptr);
        }
        return m_cameraOpened;
    }

    // 本地相机：销毁句柄后重新打开
    {
        QMutexLocker locker(&m_mutex);
        if (m_hCamera) {
            MV_CC_StopGrabbing(m_hCamera);
            MV_CC_CloseDevice(m_hCamera);
            MV_CC_DestroyHandle(m_hCamera);
            m_hCamera = nullptr;
            m_cameraOpened = false;
        }
    }
    // 打开流程走后台线程（避免阻塞流程执行线程）
    QThreadPool::globalInstance()->start(new CameraOperationTask(this, CameraOperationTask::OpenCamera));
    QThread::msleep(500);
    return m_cameraOpened;
}
