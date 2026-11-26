#include "MvsImageSourceNode.h"
#include "DataObject.h"
#include "GlobalCameraManager.h"
#include "GlobalCameraDialog.h"
#include "CameraTaskLedger.h"   // P2-3：相机线程池任务的在飞账
#include "ProjectPaths.h"   // 179 行：目录快照位执行锚定方案目录
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
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>

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
        : m_node(node), m_operation(operation)
    {
        // P2-3（2026-10-25 复核残留）：**入队即记账**，而不是等 run() 开始。
        // 只数"已进入 run()"的任务时，QThreadPool 里排队的任务不计入账：waitDrained 看到 0
        // 返回后、拆句柄之前，一个排队任务开始并复制到非空句柄 → 仍是 UAF 窗口。
        CameraTaskLedger::enter();
    }

    ~CameraOperationTask() override
    {
        CameraTaskLedger::leave();   // 与构造成对（QRunnable autoDelete，池子负责析构）
    }

    void run() override
    {
        if (!m_node) {
            return;
        }
        // 关闭闸（P2-3 残留）：closeCamera 在等待排空前先置 closing；此后才启动的任务
        // 一律不碰句柄（它已入账，等它返回即可，绝不与拆句柄并发）。
        if (m_node->isClosing()) {
            VFP_DEBUG << "相机正在关闭，跳过本任务:" << int(m_operation);
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
    // 切全局相机要先把旧句柄关掉（r19 P2）：原来只置 nullptr，旧句柄既不 StopGrabbing
    // 也不销毁，SDK 侧成了"没人拥有却还在采集"的幽灵。closeCamera() 自己会取 m_mutex，
    // 所以必须在下面这把锁**之前**做，否则自锁。
    if (key == QStringLiteral("globalCameraName")) {
        const QString next = value.toString().trimmed();
        QString oldGlobal;
        bool wasOpen = false;
        void *oldHandle = nullptr;
        {
            QMutexLocker locker(&m_mutex);
            oldGlobal = m_globalCameraName;
            wasOpen = m_cameraOpened;
            oldHandle = m_hCamera;
        }
        // K/M 复核：原先在锁外读这三个成员并与执行线程取图并发，构成数据竞争；
        // closeCamera() 自己取 m_mutex，必须先在锁内快照、解锁后再调用。
        if (next != oldGlobal && (wasOpen || oldHandle))
            closeCamera();
    }
    QMutexLocker locker(&m_mutex);
    VFP_DEBUG << "MvsImageSourceNode::setParam key=" << key << "value=" << value;
    
    if (key == "sourceType") {
        const SourceType next = static_cast<SourceType>(value.toInt());
        if (next != m_sourceType) {
            m_sourceType = next;
            // P10 复核：切源必须清理上一源的派生缓存。原只置 m_isDirectory，切到相机再切回
            // 本地且 filePath 为空时，updateImageFiles 早退、旧 m_imageFiles 残留被继续读取。
            m_imageFiles.clear();
            m_filePath.clear();
            m_isDirectory = (m_sourceType == LOCAL_FILE);
            m_currentImageIndex = 0;
            updateImageFiles();
        }
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
        // P10 补齐：写文件路径即视为选择"本地文件"图像源（与 ImageReadNode 行为等价——
        // 它的 filePath 就是输入），否则默认 CAMERA 会忽略路径去等相机，流程读不到图。
        m_sourceType = LOCAL_FILE;
        m_isDirectory = true;
        m_currentImageIndex = 0;
        updateImageFiles();
    } else if (key == "imageWidth") {
        m_imageWidth = value.toInt();
    } else if (key == "imageHeight") {
        m_imageHeight = value.toInt();
    } else if (key == "autoSwitch") {
        m_autoSwitch = value.toBool();
    } else if (key == "mono8Mode") {
        m_mono8Mode = value.toBool();
    } else {
        // P10 修复：lastError/moduleStatus/结果字段等公共键不在本类的硬件缓存分支内，
        // 必须落进基类 m_params——否则 NodeBase::execute() 的异常兜底写入、
        // FlowExecutor 的原因键枚举都取不到。基类 setParam 只写表不发信号，不重复 emit。
        HalconNode::setParam(key, value);
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
    if (key == "autoSwitch")
        return m_autoSwitch;
    if (key == "mono8Mode")
        return m_mono8Mode;
    // P10 补齐：结果字段回读（对齐 ImageReadNode，结果表/变量面板可引用）
    if (key == "outputImageWidth")
        return m_outputImageWidth;
    if (key == "outputImageHeight")
        return m_outputImageHeight;
    if (key == "outputPixelFormat")
        return m_outputPixelFormat;
    if (key == "imageName")
        return m_imageName;
    if (key == "localImageCount")
        return m_imageFiles.size();
    if (key == "currentLocalImageIndex")
        return m_currentImageIndex;
    
    // P10 修复：公共结果/原因键（lastError、moduleStatus 等）写入基类 m_params 后，
    // 本覆写此前一律 return QVariant()，导致 FlowExecutor 枚举 getAllParamNames() 取不到原因、
    // NodeBase::execute() 的 moduleStatus 判定与异常兜底同样读不到——回退基类让这些键可见。
    return HalconNode::getParam(key);
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
    json["autoSwitch"] = m_autoSwitch;
    json["mono8Mode"] = m_mono8Mode;
    // P10 补齐：本地多图清单/索引单独持久化（重开不丢选中项）
    if (m_sourceType == LOCAL_FILE && !m_imageFiles.isEmpty()) {
        QJsonArray files;
        for (const QString &f : m_imageFiles) {
            // P10 修复：方案目录内的图写相对形态，搬动方案目录后仍按新锚解析；
            // 目录外的绝对路径原样保留（既有 makeSchemeRelativeIfInside 口径）。
            // fromJson 已逐项 resolveExecutionPath，往返闭合。
            files.append(ProjectPaths::makeSchemeRelativeIfInside(
                ProjectPaths::currentSchemeFilePath(), f));
        }
        json["localImageFiles"] = files;
        json["localImageIndex"] = m_currentImageIndex;
    }
    return json;
}

void MvsImageSourceNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    QMutexLocker locker(&m_mutex);
    // P10：没有显式 sourceType 但有 filePath ⇒ 视为本地文件模式（ImageReadNode 等价语义）
    m_sourceType = json.contains("sourceType")
                       ? static_cast<SourceType>(json["sourceType"].toInt(static_cast<int>(CAMERA)))
                       : (json.contains("filePath") && !json["filePath"].toString().isEmpty()
                              ? LOCAL_FILE : CAMERA);
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
    m_autoSwitch = json["autoSwitch"].toBool(false);
    m_mono8Mode = json["mono8Mode"].toBool(false);
    /// 179 行：目录/相机形态随 sourceType 还原，随后刷新快照列表——
    /// 相对目录形态在此经 ProjectPaths 按方案目录解析（updateImageFiles 内）。
    m_isDirectory = (m_sourceType != CAMERA);
    updateImageFiles();
    // P10 补齐：优先恢复持久化的多图清单与索引（来自 addLocalImageFiles 多选），
    // 没有清单时退回上面的目录快照。
    if (json.contains("localImageFiles") && json["localImageFiles"].isArray()) {
        QJsonArray arr = json["localImageFiles"].toArray();
        QStringList restored;
        for (const QJsonValue &v : arr) {
            const QString f = v.toString();
            if (!f.isEmpty())
                restored.append(ProjectPaths::resolveExecutionPath(f));
        }
        if (!restored.isEmpty())
            m_imageFiles = restored;
    }
    m_currentImageIndex = json["localImageIndex"].toInt(0);
    if (!m_imageFiles.isEmpty())
        m_currentImageIndex = qBound(0, m_currentImageIndex, m_imageFiles.size() - 1);

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
        // P10 补齐：run(false) 用于缩略图点选/运行选中，本轮不自动前进
        {
            QMutexLocker locker(&m_mutex);
            m_allowAutoAdvance = autoSwitch;
        }
        
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

        // 帧缓冲**复用成员**（r19 P2）：原来每帧 vector(payload) 会把 10MB 先清零再被 SDK
        // 整个覆盖（满速约 300MB/s 的无用 memset）。只在容量不足时扩容——扩容那一次清零，
        // 摊薄到"换分辨率/首次取图"，稳态零 memset。
        if (m_frameBuffer.size() < payload)
            m_frameBuffer.resize(payload);
        unsigned char *imgBuf = m_frameBuffer.data();
        unsigned int nDataSize = payload;

        // 软触发模式：取图前下发软件触发命令
        {
            // 同一句柄的 SDK 调用必须串行（r19 P1-4）：两个节点共用一个全局相机时，
            // 并发调 MVS SDK 是 UB。取图/软触发都进这把按句柄的锁。
            QMutexLocker sdkLocker(&GlobalCameraManager::sdkMutexFor(m_hCamera));
            QMutexLocker locker(&m_mutex);
            if (m_triggerMode == SOFTWARE_TRIGGER) {
                MV_CC_SetCommandValue(m_hCamera, "TriggerSoftware");
            }
        }

        // 硬触发模式：一直等待相机触发源生效产生帧（期间每 1s 检查一次流程是否停止）
        auto ctx = ownerContext() ? ownerContext() : FlowExecutor::currentContext();
        bool hardwareWait = false;
        if (ctx) {
            hardwareWait = (ctx->flowMode() == FlowMode::HardwareTrigger);
        }

        int nRet = MV_OK;
        {
            QMutexLocker sdkLocker(&GlobalCameraManager::sdkMutexFor(m_hCamera));   // r19 P1-4
            nRet = MV_CC_GetOneFrameTimeout(m_hCamera, imgBuf, nDataSize, &frameInfo, 1000);
        }
        if (nRet != MV_OK && hardwareWait) {
            // 超时未取到帧：若流程仍在运行则继续等待下一次硬件触发。
            // 单次等待用 200ms（而非 1s）：FlowExecutor::stopExecution 只能在节点边界生效，
            // 缩短单次阻塞可让“停止/退出”在 ~200ms 内响应，避免线程未退出就被销毁。
            while (nRet != MV_OK) {
                bool stillRunning = ctx
                                    && ctx->state() == ExecutionState::Running;
                if (!stillRunning) {
                    break;
                }
                {
                    QMutexLocker sdkLocker(&GlobalCameraManager::sdkMutexFor(m_hCamera));
                    nRet = MV_CC_GetOneFrameTimeout(m_hCamera, imgBuf, nDataSize, &frameInfo, 200);
                }
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
    // P10 补齐：结果与状态回读、索引保护、mono8、自动切换——对齐 ImageReadNode 以替代之
    QMutexLocker locker(&m_mutex);
    const auto fail = [this](const QString &why) {
        m_executionSuccess = false;
        m_params[QStringLiteral("moduleStatus")] = false;
        m_params[QStringLiteral("lastError")] = why;
        VFP_DEBUG << "MVS 本地读图失败:" << why;
        return false;
    };

    if (m_imageFiles.isEmpty())
        return fail(QStringLiteral("没有可读取的本地图像（请选图片或文件夹）"));
    if (m_currentImageIndex < 0 || m_currentImageIndex >= m_imageFiles.size())
        return fail(QStringLiteral("当前图像索引越界"));

    const QString filePath = m_imageFiles[m_currentImageIndex];
    m_displayImageIndex = m_currentImageIndex;
    try {
        HalconCpp::HImage image;
        image.ReadImage(filePath.toLocal8Bit().data());
        if (!image.IsInitialized())
            return fail(QStringLiteral("图像解码失败：") + filePath);

        if (m_mono8Mode) {
            HTuple channels;
            CountChannels(image, &channels);
            if (channels.I() > 1) {
                try {
                    cv::Mat src = OpencvUtil::himageToMat(image);
                    cv::Mat gray;
                    if (src.channels() == 3)
                        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
                    else
                        gray = src;
                    if (!gray.empty())
                        image = OpencvUtil::matToHimage(gray);
                } catch (...) {
                    // 转换失败用原图，与 ImageReadNode 一致
                }
            }
        }

        m_outputImage = image;
        m_outputImageWidth = image.Width();
        m_outputImageHeight = image.Height();
        HTuple channels;
        CountChannels(image, &channels);
        m_outputPixelFormat = (m_mono8Mode || channels.I() == 1)
                                  ? QStringLiteral("mono8")
                                  : (channels.I() == 3 ? QStringLiteral("RGB8") : QStringLiteral("unknown"));
        m_imageName = QFileInfo(filePath).fileName();
        m_imageWidth = m_outputImageWidth;
        m_imageHeight = m_outputImageHeight;

        QSharedPointer<DataObject> output(new DataObject(DataObject::DataType::Image, QVariant()));
        output->setHImage(m_outputImage);
        setOutputData(0, output);

        m_executionSuccess = true;
        m_params[QStringLiteral("moduleStatus")] = true;
        m_params.remove(QStringLiteral("lastError"));

        // 自动切换：本轮换下一张，取模循环（与 ImageReadNode 同口径）
        if (m_autoSwitch && m_allowAutoAdvance && !m_imageFiles.isEmpty())
            m_currentImageIndex = (m_currentImageIndex + 1) % m_imageFiles.size();
        // P10 修复：m_allowAutoAdvance 是一次性"本轮不前进"标志（本地图区指定某张时置 false）。
        // 读完即恢复允许，否则流程后续轮会永远停在同一张；run(bool) 每轮仍可显式改写它。
        m_allowAutoAdvance = true;
        emit thumbnailUpdated();
        return true;
    } catch (const HException &e) {
        return fail(QStringLiteral("图像读取异常：")
                    + QString::fromStdString(e.ErrorMessage().TextA()));
    } catch (const std::exception &e) {
        return fail(QStringLiteral("图像读取异常：") + QString::fromUtf8(e.what()));
    } catch (...) {
        return fail(QStringLiteral("图像读取未知异常"));
    }
}

void MvsImageSourceNode::updateImageFiles()
{
    if (m_isDirectory && !m_filePath.isEmpty()) {
        /// 179 行：相对目录形态执行锚定方案目录（目录内拾取本就换相对形态存参）
        const QString resolved = ProjectPaths::resolveExecutionPath(m_filePath);
        QFileInfo info(resolved);
        if (info.isFile()) {
            // P10 补齐：单文件形态（原实现把单文件也当目录，entryList 得空表）
            m_imageFiles = QStringList{resolved};
        } else if (info.isDir()) {
            QDir dir(resolved);
            // P10 补齐：补 *.jpeg（与 ImageReadNode 的过滤口径一致）
            m_imageFiles = dir.entryList({"*.bmp", "*.jpg", "*.jpeg", "*.png", "*.tif", "*.tiff"},
                                         QDir::Files, QDir::Name);
            for (int i = 0; i < m_imageFiles.size(); i++)
                m_imageFiles[i] = dir.absoluteFilePath(m_imageFiles[i]);
        } else {
            m_imageFiles.clear();
        }
        if (!m_imageFiles.isEmpty())
            m_currentImageIndex = qBound(0, m_currentImageIndex, m_imageFiles.size() - 1);
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
            // 采用句柄也要查 PayloadSize（r19 P1-4）：默认 10MB 缓冲装不下高分辨率帧
            //（2448x2048 RGB8 约 15MB），现场表现为每帧取图失败 + 重连风暴。
            {
                MVCC_INTVALUE stIntParam;
                memset(&stIntParam, 0, sizeof(MVCC_INTVALUE));
                if (MV_CC_GetIntValue(cameraHandle, "PayloadSize", &stIntParam) == MV_OK
                    && stIntParam.nCurValue > 0) {
                    m_payloadSize = static_cast<unsigned int>(stIntParam.nCurValue);
                    VFP_DEBUG << "采用句柄的 PayloadSize:" << m_payloadSize;
                }
            }
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
    // 注意（2026-10-25 复核 P3-3）：m_closing 的语义是"关闭进行中"，由 closeCamera 成对
    // 置/清，打开动作**不碰**它——否则"关闭排空期间"一个 OpenCamera 任务会把闸清回 false，
    // 新到的 Apply 任务又踩着拆句柄窗口。打开失败/成功都不影响闸。
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
            // 失败就别报"已打开"（r19 P2 同族）：后续每帧取图必失败，还会触发重连风暴
            VFP_DEBUG << "开始采集失败，错误码:" << nRet;
            MV_CC_CloseDevice(hCamera);
            MV_CC_DestroyHandle(hCamera);
            emit cameraStatusChanged(false);
            return;
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
    // P2-3（2026-10-25 复核 + 同日残留修复）：先关闸、再等相机线程池任务排空，最后拆句柄。
    // 三个要点：① 先置 closing——此后才启动/排队的任务在 run() 入口就退让，不会拿到句柄；
    //          ② 必须**在取 m_mutex 之前**等——任务开头也要取同一把锁，持锁等待会死锁；
    //          ③ 超时未排空就跳过关闭（句柄留给进程退出时回收），并撤闸让相机继续可用。
    m_closing.store(true, std::memory_order_relaxed);
    if (!CameraTaskLedger::waitDrained(CameraTaskLedger::drainTimeoutMs())) {
        VFP_DEBUG << "相机线程池任务未排空，跳过本次关闭（句柄留给进程退出时回收）";
        m_closing.store(false, std::memory_order_relaxed);   // 没关成：相机仍可用，撤闸
        return;
    }

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
    
    // 关闭完成（成功拆句柄）→ 撤闸（P3-3）：m_closing 的语义是"关闭进行中"，
    // 由 closeCamera 成对置/清；打开动作不碰它。这样"关闭后再打开"在结构上可达。
    m_closing.store(false, std::memory_order_relaxed);
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
    if (auto ctx = ownerContext() ? ownerContext() : FlowExecutor::currentContext()) {
        FlowMode mode = ctx->flowMode();
        ExecutionState state = ctx->state();
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

                // 更新全局相机缓存：只改 pixelFormat（P5：原实现用默认 CameraParams 整体覆盖，
                // 会把曝光/增益/帧率/触发源全部冲成默认值）
                {
                    QMutexLocker locker(&m_mutex);
                    QString globalName = m_globalCameraName;
                    if (!globalName.isEmpty()) {
                        GlobalCameraManager::instance()->updateCameraPixelFormatCache(
                            globalName, appliedPixelFormat);
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
    // K 复核：面板下拉原来直接置 m_cameraOpened=false/m_hCamera=nullptr 并摘 consumer，
    // 既不 closeCamera 也不等 CameraTaskLedger 排空；最后一个 consumer 一摘，对话框
    // 就能拆掉仍有在飞任务的 SDK 句柄。统一走 setParam（先 closeCamera + 排空 + 改登）。
    setParam(QStringLiteral("globalCameraName"), name);

    // M2 复核：原实现紧接着调 readCameraParams()，但 m_hCamera 已被 setParam 置空 ⇒
    // 恒在入口早退，新绑相机的曝光/增益/帧率还是旧缓存。先取新句柄再读。
    if (!name.isEmpty()) {
        void *handle = GlobalCameraManager::instance()->getCameraHandle(name);
        if (handle) {
            {
                QMutexLocker locker(&m_mutex);
                m_hCamera = handle;
                m_cameraOpened = true;
            }
            QMutexLocker sdkLock(&GlobalCameraManager::sdkMutexFor(handle));
            readCameraParams();
        }
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
    if (auto ctx = ownerContext() ? ownerContext() : FlowExecutor::currentContext()) {
        FlowMode mode = ctx->flowMode();
        ExecutionState state = ctx->state();
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
    // P10 复核：公开接口此前只写 m_filePath、不切模式，外部脚本/API 设路径后节点仍停在
    // CAMERA 静默读不出图。统一委托 setParam（切 LOCAL_FILE + 刷列表），与面板同义。
    setParam(QStringLiteral("filePath"), path);
}

QString MvsImageSourceNode::getFilePath() const
{
    return m_filePath;
}

QStringList MvsImageSourceNode::localImageFiles() const
{
    QMutexLocker locker(&m_mutex);
    return m_imageFiles;
}

bool MvsImageSourceNode::isLocalFolderMode() const
{
    QMutexLocker locker(&m_mutex);
    if (m_sourceType != LOCAL_FILE)
        return false;
    QFileInfo info(ProjectPaths::resolveExecutionPath(m_filePath));
    return info.isDir();
}

int MvsImageSourceNode::currentLocalImageIndex() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentImageIndex;
}

void MvsImageSourceNode::setAdvanceThisRound(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_allowAutoAdvance = enabled;
}

void MvsImageSourceNode::setLocalImageIndex(int index)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_imageFiles.isEmpty())
            return;
        m_currentImageIndex = qBound(0, index, m_imageFiles.size() - 1);
        m_displayImageIndex = m_currentImageIndex;
    }
    emit thumbnailUpdated();
}

void MvsImageSourceNode::clearLocalImages()
{
    {
        QMutexLocker locker(&m_mutex);
        m_imageFiles.clear();
        m_currentImageIndex = 0;
        m_filePath.clear();
        // P10 复核：清空后回到相机源，避免仍停在本地模式却无图可
        m_sourceType = CAMERA;
        m_isDirectory = false;
    }
    emit thumbnailUpdated();
    emit paramChanged(QStringLiteral("filePath"), QString());
}

bool MvsImageSourceNode::addLocalImageFiles(const QStringList &files)
{
    {
        QMutexLocker locker(&m_mutex);
        // 与 ImageReadNode 同口径：显式清单直接成为本地序列（多图/单选文件都走这里）
        m_sourceType = LOCAL_FILE;
        m_isDirectory = false;
        QStringList valid;
        for (const QString &f : files) {
            if (!f.isEmpty() && QFileInfo::exists(f))
                valid.append(ProjectPaths::resolveExecutionPath(f));
        }
        if (valid.isEmpty())
            return false;
        m_imageFiles = valid;
        m_currentImageIndex = 0;
    }
    emit thumbnailUpdated();
    return true;
}

bool MvsImageSourceNode::isLocalFileSource() const
{
    QMutexLocker locker(&m_mutex);
    return m_sourceType == LOCAL_FILE;
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

    // P5：连接状态指示——断线/开关相机此前在面板上零反馈，用户只能从日志猜。
    QLabel *cameraStateLabel = new QLabel();
    cameraStateLabel->setObjectName(QStringLiteral("cameraStateLabel"));
    auto refreshCameraStateLabel = [this, cameraStateLabel]() {
        const bool opened = m_cameraOpened && m_hCamera;
        cameraStateLabel->setText(opened ? QStringLiteral("\u25CF \u76F8\u673A\u5DF2\u8FDE\u63A5")
                                         : QStringLiteral("\u25CB \u76F8\u673A\u672A\u8FDE\u63A5"));
        cameraStateLabel->setStyleSheet(opened ? QStringLiteral("color: #4CAF50;")
                                               : QStringLiteral("color: #F44336;"));
    };
    refreshCameraStateLabel();
    layout->addWidget(cameraStateLabel);
    // cameraChanged 是 manager 的信号（全局相机名/设备绑定变化）；本节点还有自己的
    // cameraStatusChanged（connect/disconnect 结果）。两者都刷标签。
    QObject::connect(GlobalCameraManager::instance(), &GlobalCameraManager::cameraStatusChanged,
                     panel, [this, refreshCameraStateLabel](const QString &name, bool) {
        if (name == m_globalCameraName) refreshCameraStateLabel();
    });
    QObject::connect(this, &MvsImageSourceNode::cameraStatusChanged, panel,
                     [refreshCameraStateLabel]() { refreshCameraStateLabel(); });

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

    // 刷新相机检测（P5：旧实现起了后台任务却丢弃结果，3 秒后只把按钮文字改回去，
    // 列表根本不更新。用户主动点刷新，同步枚举 + 立即回填，短暂忙碌是可接受的）
    connect(detectButton, &QPushButton::clicked, panel, [=]() {
        detectButton->setEnabled(false);
        detectButton->setText(QStringLiteral("\u6B63\u5728\u68C0\u6D4B\u2026"));
        QPointer<MvsImageSourceNode> selfGuard(this);
        QTimer::singleShot(0, panel, [=]() {
            if (!selfGuard) return;
            refillCameraCombo(m_cameraName);   // 真正重新枚举并回填下拉
            detectButton->setEnabled(true);
            detectButton->setText(QStringLiteral("\u5237\u65B0\u76F8\u673A\u5217\u8868"));
        });
    });

    // 浏览文件
    connect(browseButton, &QPushButton::clicked, panel, [=]() {
        QString path = QFileDialog::getExistingDirectory(panel,
            QStringLiteral("\u9009\u62E9\u56FE\u50CF\u6587\u4EF6\u5939"));
        if (!path.isEmpty()) {
            /// 179 行：方案目录内拾取换相对形态（列表刷新按执行锚定解析）
            path = ProjectPaths::makeSchemeRelativeIfInside(ProjectPaths::currentSchemeFilePath(), path);
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
    // U-20 补强（2026-11-10）：context=panel 只保护面板，节点可能先被 removeNode 销毁；
    // 本文件后台任务早已用 QPointer 保护节点，此处漏了守卫。
    QPointer<MvsImageSourceNode> selfGuard(this);
    QTimer::singleShot(500, panel, [=]() {
        if (!selfGuard) return;
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
            // P2-4（2026-10-25 复核）：只有"我是唯一消费者"时才允许拆重开。别的节点也在用
            // 这台全局相机时，拆句柄等于在它的取图调用里 UAF——宁可这次不重连，等它自己恢复。
            const QStringList consumers = gcm->consumersForCamera(cam);
            // 必须确认"唯一的那个消费者就是我"（2026-10-25 复核 P2-4 残留）：只按数量判，
            // 在登记缺失/竞态下可能把别人的句柄 force 拆掉（我自己的登记恰好没写进去时数量也是 1）。
            const bool soleConsumerIsMe =
                (consumers.size() == 1 && consumers.first() == m_cameraConsumerId);
            if (!soleConsumerIsMe) {
                VFP_DEBUG << "全局相机不是本节点独占，跳过自动重连（不拆共享句柄）:"
                          << cam << consumers << "me:" << m_cameraConsumerId;
                return false;
            }
            gcm->closeCamera(cam, /*force*/ true);   // 我是唯一消费者：强制拆（默认会被消费者闸拒）
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
