#include "HalconImageSourceNode.h"
#include "DataObject.h"
#include <QDebug>
#include <QDir>
#include <QThread>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QThreadPool>
#include <QPointer>
#include <HalconCpp.h>
#include "AppLog.h"
#include "VisionWorkbenchStyle.h"

using namespace HalconCpp;

// 后台线程类，用于执行相机操作
// 注意：线程对象在 run() 结束后由 deleteLater() 释放，避免对象泄漏；
//       m_node 用 QPointer 保护，节点销毁后 run() 安全退出，避免悬垂指针
class CameraOperationThread : public QThread
{
public:
    enum OperationType {
        OpenCamera,
        ApplyParams,
        DetectCameras
    };

    CameraOperationThread(HalconImageSourceNode *node, OperationType operation)
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
        }
    }

private:
    QPointer<HalconImageSourceNode> m_node;
    OperationType m_operation;
};

HalconImageSourceNode::HalconImageSourceNode(QObject *parent) : HalconNode(parent)
    , m_sourceType(CAMERA)
    , m_isDirectory(false)
    , m_currentImageIndex(0)
    , m_triggerMode(FREE_RUN)
    , m_exposureTime(10000.0)
    , m_gain(0.0)
    , m_frameRate(30.0)
    , m_cameraOpened(false)
    , m_imageWidth(0)
    , m_imageHeight(0)
    , m_inputTrigger(false)
    , m_inputExposure(0.0)
    , m_inputGain(0.0)
    , m_isDetectingCameras(false)
{
    setName(QStringLiteral("图像源"));
    m_type = IMAGE_ACQUISITION;
    addOutputPort(QStringLiteral("输出图像"));
}

HalconImageSourceNode::~HalconImageSourceNode()
{
    closeCamera();
}

void HalconImageSourceNode::init()
{
    // 初始化节点
    updateOutputInfo();
}

void HalconImageSourceNode::run(bool autoSwitch)
{
    process();
}

bool HalconImageSourceNode::process()
{
    try {
        if (m_sourceType == CAMERA) {
            if (!m_cameraOpened) {
                if (!openCamera()) {
                    VFP_DEBUG << "Failed to open camera";
                    return false;
                }
            }
            return grabImage();
        } else {
            return loadImageFromFile();
        }
    } catch (HException &e) {
        VFP_DEBUG << "Failed to process image source:" << e.ErrorMessage().Text();
        return false;
    } catch (...) {
        VFP_DEBUG << "Failed to process image source: Unknown error";
        return false;
    }
}

void HalconImageSourceNode::setParam(const QString &name, const QVariant &value)
{
    // cameraName 分支会走 setSelectedCamera（其内部加 m_mutex 并关闭/重开相机），
    // 必须在获取本类锁之前处理，否则非递归锁自死锁。
    if (name == QStringLiteral("cameraName")) {
        setSelectedCamera(value.toString());
        return;
    }

    QMutexLocker locker(&m_mutex);

    if (name == "sourceType") {
        m_sourceType = static_cast<SourceType>(value.toInt());
    } else if (name == "filePath") {
        m_filePath = value.toString();
        QFileInfo fileInfo(m_filePath);
        m_isDirectory = fileInfo.isDir();
        if (m_isDirectory) {
            updateImageFiles();
        }
    } else if (name == "isDirectory") {
        m_isDirectory = value.toBool();
    } else if (name == "triggerMode") {
        m_triggerMode = static_cast<TriggerMode>(value.toInt());
    } else if (name == "exposureTime") {
        m_exposureTime = value.toDouble();
    } else if (name == "gain") {
        m_gain = value.toDouble();
    } else if (name == "frameRate") {
        m_frameRate = value.toDouble();
    } else if (name == "inputTrigger") {
        m_inputTrigger = value.toBool();
    } else if (name == "inputExposure") {
        m_inputExposure = value.toDouble();
    } else if (name == "inputGain") {
        m_inputGain = value.toDouble();
    } else {
        // 基类实现可能间接触发回调（预览等），先释放本类锁再调用
        locker.unlock();
        HalconNode::setParam(name, value);
    }
}

QVariant HalconImageSourceNode::getParam(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    
    if (name == "sourceType") {
        return static_cast<int>(m_sourceType);
    } else if (name == "filePath") {
        return m_filePath;
    } else if (name == "isDirectory") {
        return m_isDirectory;
    } else if (name == "cameraName") {
        return m_cameraName;
    } else if (name == "triggerMode") {
        return static_cast<int>(m_triggerMode);
    } else if (name == "exposureTime") {
        return m_exposureTime;
    } else if (name == "gain") {
        return m_gain;
    } else if (name == "frameRate") {
        return m_frameRate;
    } else if (name == "inputTrigger") {
        return m_inputTrigger;
    } else if (name == "inputExposure") {
        return m_inputExposure;
    } else if (name == "inputGain") {
        return m_inputGain;
    } else {
        return HalconNode::getParam(name);
    }
}

QJsonObject HalconImageSourceNode::toJson() const
{
    // 先取基类字段（name/position/size/params/掩膜等），再补本类字段。
    // 原实现自建空 JSON 且不调用基类，导致该节点存盘后位置/大小/参数全部丢失，
    // 重载时全部堆到 (0,0)。
    QJsonObject json = HalconNode::toJson();

    QMutexLocker locker(&m_mutex);

    json["sourceType"] = static_cast<int>(m_sourceType);
    json["filePath"] = m_filePath;
    json["isDirectory"] = m_isDirectory;
    json["cameraName"] = m_cameraName;
    json["triggerMode"] = static_cast<int>(m_triggerMode);
    json["exposureTime"] = m_exposureTime;
    json["gain"] = m_gain;
    json["frameRate"] = m_frameRate;
    
    return json;
}

void HalconImageSourceNode::fromJson(const QJsonObject &json)
{
    // 先恢复基类字段（位置/大小/参数/掩膜）。
    // 基类 fromJson 会回调虚函数 setParam，必须在持有本类锁之前完成调用。
    HalconNode::fromJson(json);

    const bool hasFilePath = json.contains("filePath");
    const QString filePath = hasFilePath ? json["filePath"].toString() : QString();
    const bool hasCameraName = json.contains("cameraName");
    const QString cameraName = hasCameraName ? json["cameraName"].toString() : QString();

    {
        QMutexLocker locker(&m_mutex);

        if (json.contains("sourceType")) {
            m_sourceType = static_cast<SourceType>(json["sourceType"].toInt());
        }
        if (hasFilePath) {
            m_filePath = filePath;
            QFileInfo fileInfo(m_filePath);
            m_isDirectory = fileInfo.isDir();
            if (m_isDirectory) {
                updateImageFiles();   // 只操作本类成员，可持锁
            }
        }
        if (json.contains("isDirectory")) {
            m_isDirectory = json["isDirectory"].toBool();
        }
        if (json.contains("triggerMode")) {
            m_triggerMode = static_cast<TriggerMode>(json["triggerMode"].toInt());
        }
        if (json.contains("exposureTime")) {
            m_exposureTime = json["exposureTime"].toDouble();
        }
        if (json.contains("gain")) {
            m_gain = json["gain"].toDouble();
        }
        if (json.contains("frameRate")) {
            m_frameRate = json["frameRate"].toDouble();
        }
    }

    // setSelectedCamera 内部会关闭相机并再取 m_mutex，必须放在锁外调用。
    // 原实现在持锁状态下调用它（其内部 closeCamera 又加同一把锁）→ 加载方案即自死锁。
    // 相机参数已在上面恢复完毕，此处再开相机可拿到正确参数。
    if (hasCameraName) {
        setSelectedCamera(cameraName);
    }
}

QStringList HalconImageSourceNode::getAvailableCameras()
{
    // 直接在当前线程中执行相机检测
    return detectCamerasInThread();
}

QStringList HalconImageSourceNode::detectCamerasInThread()
{
    // 检查是否正在检测相机，避免重复检测
    {
        QMutexLocker locker(&m_mutex);
        if (m_isDetectingCameras) {
            VFP_DEBUG << "Camera detection already in progress, skipping...";
            return QStringList() << "关闭相机 (Close)";
        }
        m_isDetectingCameras = true;
    }
    
    QStringList cameras;
    
    try {
        // 尝试 GigEVision2 接口（网口相机）
        try {
            VFP_DEBUG << "Trying to detect GigEVision2 cameras...";
            HTuple cameraList, result;
            try {
                InfoFramegrabber(HTuple("GigEVision2"), HTuple("device"), &cameraList, &result);
                
                VFP_DEBUG << "GigEVision2 cameraList length:" << cameraList.Length();
                
                if (cameraList.Length() > 0) {
                    for (int i = 0; i < cameraList.Length(); i++) {
                        try {
                            // 尝试打开相机获取详细信息
                            HFramegrabber fg;
                            fg.OpenFramegrabber("GigEVision2", 0, 0, 0, 0, 0, 0, "default", -1, "default", -1, "false", "default", QString::number(i).toUtf8().constData(), 0, -1);
                            
                            // 获取相机信息
                            QString cameraName = "GigEVision2 Camera";
                            QString serialNumber = "Unknown";
                            
                            // 尝试获取相机参数
                            try {
                                // 尝试不同的参数名
                                try {
                                    HTuple modelName = fg.GetFramegrabberParam("DeviceModelName");
                                    if (modelName.Length() > 0) {
                                        cameraName = QString::fromStdString(modelName[0].S().Text());
                                    }
                                } catch (...) {
                                    try {
                                        HTuple modelName = fg.GetFramegrabberParam("device_model");
                                        if (modelName.Length() > 0) {
                                            cameraName = QString::fromStdString(modelName[0].S().Text());
                                        }
                                    } catch (...) {
                                        VFP_DEBUG << "Failed to get camera model name";
                                    }
                                }
                                
                                try {
                                    HTuple serial = fg.GetFramegrabberParam("DeviceSerialNumber");
                                    if (serial.Length() > 0) {
                                        serialNumber = QString::fromStdString(serial[0].S().Text());
                                    }
                                } catch (...) {
                                    try {
                                        HTuple serial = fg.GetFramegrabberParam("device_serial_number");
                                        if (serial.Length() > 0) {
                                            serialNumber = QString::fromStdString(serial[0].S().Text());
                                        }
                                    } catch (...) {
                                        VFP_DEBUG << "Failed to get camera serial number";
                                    }
                                }
                            } catch (...) {
                                VFP_DEBUG << "Failed to get camera parameters, using default values";
                            }
                            
                            // 以"相机名 (SN: 序列号)"的格式显示
                            QString cameraItem = QString("%1 (SN: %2)").arg(cameraName).arg(serialNumber);
                            cameras << cameraItem;
                            VFP_DEBUG << "Found GigEVision2 camera:" << cameraItem;
                            
                            // 关闭相机
                            fg.CloseFramegrabber();
                        } catch (HException &e) {
                            VFP_DEBUG << "Failed to open GigEVision2 camera " << i << ":" << e.ErrorMessage().Text();
                        } catch (...) {
                            VFP_DEBUG << "Failed to open GigEVision2 camera " << i;
                        }
                    }
                }
            } catch (HException &e) {
                VFP_DEBUG << "No GigEVision2 cameras found:" << e.ErrorMessage().Text();
            } catch (...) {
                VFP_DEBUG << "Error detecting GigEVision2 cameras";
            }
        } catch (...) {
            VFP_DEBUG << "Error in GigEVision2 detection";
        }
        
        // 尝试 USB3Vision 接口（USB相机）
        try {
            VFP_DEBUG << "Trying to detect USB3Vision cameras...";
            HTuple cameraList, result;
            try {
                InfoFramegrabber(HTuple("USB3Vision"), HTuple("device"), &cameraList, &result);
                
                VFP_DEBUG << "USB3Vision cameraList length:" << cameraList.Length();
                
                if (cameraList.Length() > 0) {
                    for (int i = 0; i < cameraList.Length(); i++) {
                        try {
                            // 尝试打开相机获取详细信息
                            HFramegrabber fg;
                            fg.OpenFramegrabber("USB3Vision", 0, 0, 0, 0, 0, 0, "default", -1, "default", -1, "false", "default", QString::number(i).toUtf8().constData(), 0, -1);
                            
                            // 获取相机信息
                            QString cameraName = "USB3Vision Camera";
                            QString serialNumber = "Unknown";
                            
                            // 尝试获取相机参数
                            try {
                                // 尝试不同的参数名
                                try {
                                    HTuple modelName = fg.GetFramegrabberParam("DeviceModelName");
                                    if (modelName.Length() > 0) {
                                        cameraName = QString::fromStdString(modelName[0].S().Text());
                                    }
                                } catch (...) {
                                    try {
                                        HTuple modelName = fg.GetFramegrabberParam("device_model");
                                        if (modelName.Length() > 0) {
                                            cameraName = QString::fromStdString(modelName[0].S().Text());
                                        }
                                    } catch (...) {
                                        VFP_DEBUG << "Failed to get camera model name";
                                    }
                                }
                                
                                try {
                                    HTuple serial = fg.GetFramegrabberParam("DeviceSerialNumber");
                                    if (serial.Length() > 0) {
                                        serialNumber = QString::fromStdString(serial[0].S().Text());
                                    }
                                } catch (...) {
                                    try {
                                        HTuple serial = fg.GetFramegrabberParam("device_serial_number");
                                        if (serial.Length() > 0) {
                                            serialNumber = QString::fromStdString(serial[0].S().Text());
                                        }
                                    } catch (...) {
                                        VFP_DEBUG << "Failed to get camera serial number";
                                    }
                                }
                            } catch (...) {
                                VFP_DEBUG << "Failed to get camera parameters, using default values";
                            }
                            
                            // 以"相机名 (SN: 序列号)"的格式显示
                            QString cameraItem = QString("%1 (SN: %2)").arg(cameraName).arg(serialNumber);
                            cameras << cameraItem;
                            VFP_DEBUG << "Found USB3Vision camera:" << cameraItem;
                            
                            // 关闭相机
                            fg.CloseFramegrabber();
                        } catch (HException &e) {
                            VFP_DEBUG << "Failed to open USB3Vision camera " << i << ":" << e.ErrorMessage().Text();
                        } catch (...) {
                            VFP_DEBUG << "Failed to open USB3Vision camera " << i;
                        }
                    }
                }
            } catch (HException &e) {
                VFP_DEBUG << "No USB3Vision cameras found:" << e.ErrorMessage().Text();
            } catch (...) {
                VFP_DEBUG << "Error detecting USB3Vision cameras";
            }
        } catch (...) {
            VFP_DEBUG << "Error in USB3Vision detection";
        }
        
        // 尝试 GenICamTL 接口（工业相机）
        try {
            VFP_DEBUG << "Trying to detect GenICamTL cameras...";
            HTuple cameraList, result;
            try {
                InfoFramegrabber(HTuple("GenICamTL"), HTuple("device"), &cameraList, &result);
                
                VFP_DEBUG << "GenICamTL cameraList length:" << cameraList.Length();
                
                if (cameraList.Length() > 0) {
                    for (int i = 0; i < cameraList.Length(); i++) {
                        try {
                            // 尝试打开相机获取详细信息
                            HFramegrabber fg;
                            fg.OpenFramegrabber("GenICamTL", 0, 0, 0, 0, 0, 0, "default", -1, "default", -1, "false", "default", QString::number(i).toUtf8().constData(), 0, -1);
                            
                            // 获取相机信息
                            QString cameraName = "GenICamTL Camera";
                            QString serialNumber = "Unknown";
                            
                            // 尝试获取相机参数
                            try {
                                // 尝试不同的参数名
                                try {
                                    HTuple modelName = fg.GetFramegrabberParam("DeviceModelName");
                                    if (modelName.Length() > 0) {
                                        cameraName = QString::fromStdString(modelName[0].S().Text());
                                    }
                                } catch (...) {
                                    try {
                                        HTuple modelName = fg.GetFramegrabberParam("device_model");
                                        if (modelName.Length() > 0) {
                                            cameraName = QString::fromStdString(modelName[0].S().Text());
                                        }
                                    } catch (...) {
                                        VFP_DEBUG << "Failed to get camera model name";
                                    }
                                }
                                
                                try {
                                    HTuple serial = fg.GetFramegrabberParam("DeviceSerialNumber");
                                    if (serial.Length() > 0) {
                                        serialNumber = QString::fromStdString(serial[0].S().Text());
                                    }
                                } catch (...) {
                                    try {
                                        HTuple serial = fg.GetFramegrabberParam("device_serial_number");
                                        if (serial.Length() > 0) {
                                            serialNumber = QString::fromStdString(serial[0].S().Text());
                                        }
                                    } catch (...) {
                                        VFP_DEBUG << "Failed to get camera serial number";
                                    }
                                }
                            } catch (...) {
                                VFP_DEBUG << "Failed to get camera parameters, using default values";
                            }
                            
                            // 以"相机名 (SN: 序列号)"的格式显示
                            QString cameraItem = QString("%1 (SN: %2)").arg(cameraName).arg(serialNumber);
                            cameras << cameraItem;
                            VFP_DEBUG << "Found GenICamTL camera:" << cameraItem;
                            
                            // 关闭相机
                            fg.CloseFramegrabber();
                        } catch (HException &e) {
                            VFP_DEBUG << "Failed to open GenICamTL camera " << i << ":" << e.ErrorMessage().Text();
                        } catch (...) {
                            VFP_DEBUG << "Failed to open GenICamTL camera " << i;
                        }
                    }
                }
            } catch (HException &e) {
                VFP_DEBUG << "No GenICamTL cameras found:" << e.ErrorMessage().Text();
            } catch (...) {
                VFP_DEBUG << "Error detecting GenICamTL cameras";
            }
        } catch (...) {
            VFP_DEBUG << "Error in GenICamTL detection";
        }
        
    } catch (...) {
        VFP_DEBUG << "Unknown exception in detectCamerasInThread";
    }
    
    // 添加关闭相机选项
    cameras.insert(0, "关闭相机 (Close)");
    
    VFP_DEBUG << "Found " << (cameras.size() - 1) << " cameras total";
    
    // 检测完成，重置标志
    { 
        QMutexLocker locker(&m_mutex);
        m_isDetectingCameras = false;
    }
    
    return cameras;
}

QString HalconImageSourceNode::getSelectedCamera() const
{
    QMutexLocker locker(&m_mutex);
    return m_cameraName;
}

void HalconImageSourceNode::setSelectedCamera(const QString &cameraName)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_cameraName == cameraName)
            return;
        // 先更新名称再关相机：closeCamera() 内部会加同一把 m_mutex
        m_cameraName = cameraName;
    }

    // 关闭当前相机。必须在**锁外**调用：closeCamera() 自身会 QMutexLocker(&m_mutex)，
    // 原实现在持锁状态下调用它，属于非递归锁自死锁（界面上切换相机会直接卡死）。
    closeCamera();

    // 如果选择的不是关闭相机选项，在后台线程中打开相机
    if (cameraName != QStringLiteral("关闭相机 (Close)")) {
        CameraOperationThread *thread = new CameraOperationThread(this, CameraOperationThread::OpenCamera);
        connect(thread, &QThread::finished, thread, &QThread::deleteLater);
        thread->start();
    }
}

bool HalconImageSourceNode::isCameraOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_cameraOpened;
}

void HalconImageSourceNode::setFilePath(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    
    m_filePath = path;
    QFileInfo fileInfo(path);
    m_isDirectory = fileInfo.isDir();
    
    if (m_isDirectory) {
        updateImageFiles();
    }
}

QString HalconImageSourceNode::getFilePath() const
{
    QMutexLocker locker(&m_mutex);
    return m_filePath;
}

void HalconImageSourceNode::applyCameraParams()
{
    // 在后台线程中执行相机参数应用
    CameraOperationThread *thread = new CameraOperationThread(this, CameraOperationThread::ApplyParams);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void HalconImageSourceNode::applyCameraParamsInThread()
{
    QMutexLocker locker(&m_mutex);
    
    try {
        if (m_cameraOpened) {
            // 这里添加设置相机参数的逻辑
            VFP_DEBUG << "Applying camera parameters";
            
            // 示例：设置曝光时间
            if (m_exposureTime > 0) {
                VFP_DEBUG << "Setting exposure time:" << m_exposureTime;
                // 实际项目中这里应该调用相机SDK的API设置参数
            }
            
            // 示例：设置增益
            if (m_gain > 0) {
                VFP_DEBUG << "Setting gain:" << m_gain;
                // 实际项目中这里应该调用相机SDK的API设置参数
            }
            
            // 示例：设置帧率
            if (m_frameRate > 0) {
                VFP_DEBUG << "Setting frame rate:" << m_frameRate;
                // 实际项目中这里应该调用相机SDK的API设置参数
            }
            
            emit paramsApplied();
        }
    } catch (HException &e) {
        VFP_DEBUG << "Failed to apply camera parameters:" << e.ErrorMessage().Text();
    } catch (...) {
        VFP_DEBUG << "Failed to apply camera parameters: Unknown error";
    }
}

bool HalconImageSourceNode::openCamera()
{
    // 在后台线程中执行相机打开操作
    CameraOperationThread *thread = new CameraOperationThread(this, CameraOperationThread::OpenCamera);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
    return true;
}

bool HalconImageSourceNode::openCameraInThread()
{
    try {
        QString cameraName;
        {
            QMutexLocker locker(&m_mutex);
            cameraName = m_cameraName;
        }
        
        VFP_DEBUG << "Opening camera:" << cameraName;
        
        // 尝试打开GigEVision2相机
        try {
            // 尝试使用设备索引打开相机
            for (int i = 0; i < 3; i++) {
                try {
                    VFP_DEBUG << "Trying to open GigEVision2 camera with index:" << i;
                    HFramegrabber fg;
                    fg.OpenFramegrabber("GigEVision2", 0, 0, 0, 0, 0, 0, "default", -1, "default", -1, "false", "default", QString::number(i).toUtf8().constData(), 0, -1);
                    
                    // 保存相机句柄
                    {
                        QMutexLocker locker(&m_mutex);
                        m_cameraHandle = 1; // 模拟句柄
                        m_cameraOpened = true;
                    }
                    
                    VFP_DEBUG << "GigEVision2 camera opened successfully";
                    emit cameraStatusChanged(true);
                    
                    // 读取相机参数
                    readCameraParams();
                    
                    return true;
                } catch (HException &e) {
                    VFP_DEBUG << "Failed to open GigEVision2 camera " << i << ":" << e.ErrorMessage().Text();
                } catch (...) {
                    VFP_DEBUG << "Failed to open GigEVision2 camera " << i;
                }
            }
        } catch (...) {
            VFP_DEBUG << "Error in GigEVision2 camera opening";
        }
        
        // 尝试打开USB3Vision相机
        try {
            // 尝试使用设备索引打开相机
            for (int i = 0; i < 3; i++) {
                try {
                    VFP_DEBUG << "Trying to open USB3Vision camera with index:" << i;
                    HFramegrabber fg;
                    fg.OpenFramegrabber("USB3Vision", 0, 0, 0, 0, 0, 0, "default", -1, "default", -1, "false", "default", QString::number(i).toUtf8().constData(), 0, -1);
                    
                    // 保存相机句柄
                    {
                        QMutexLocker locker(&m_mutex);
                        m_cameraHandle = 1; // 模拟句柄
                        m_cameraOpened = true;
                    }
                    
                    VFP_DEBUG << "USB3Vision camera opened successfully";
                    emit cameraStatusChanged(true);
                    
                    // 读取相机参数
                    readCameraParams();
                    
                    return true;
                } catch (HException &e) {
                    VFP_DEBUG << "Failed to open USB3Vision camera " << i << ":" << e.ErrorMessage().Text();
                } catch (...) {
                    VFP_DEBUG << "Failed to open USB3Vision camera " << i;
                }
            }
        } catch (...) {
            VFP_DEBUG << "Error in USB3Vision camera opening";
        }
        
        // 尝试打开GenICamTL相机
        try {
            // 尝试使用设备索引打开相机
            for (int i = 0; i < 3; i++) {
                try {
                    VFP_DEBUG << "Trying to open GenICamTL camera with index:" << i;
                    HFramegrabber fg;
                    fg.OpenFramegrabber("GenICamTL", 0, 0, 0, 0, 0, 0, "default", -1, "default", -1, "false", "default", QString::number(i).toUtf8().constData(), 0, -1);
                    
                    // 保存相机句柄
                    {
                        QMutexLocker locker(&m_mutex);
                        m_cameraHandle = 1; // 模拟句柄
                        m_cameraOpened = true;
                    }
                    
                    VFP_DEBUG << "GenICamTL camera opened successfully";
                    emit cameraStatusChanged(true);
                    
                    // 读取相机参数
                    readCameraParams();
                    
                    return true;
                } catch (HException &e) {
                    VFP_DEBUG << "Failed to open GenICamTL camera " << i << ":" << e.ErrorMessage().Text();
                } catch (...) {
                    VFP_DEBUG << "Failed to open GenICamTL camera " << i;
                }
            }
        } catch (...) {
            VFP_DEBUG << "Error in GenICamTL camera opening";
        }
        
        // 如果所有尝试都失败
        VFP_DEBUG << "Failed to open any camera";
        {
            QMutexLocker locker(&m_mutex);
            m_cameraHandle = -1;
            m_cameraOpened = false;
        }
        emit cameraStatusChanged(false);
        return false;
    } catch (HException &e) {
        VFP_DEBUG << "Failed to open camera:" << e.ErrorMessage().Text();
        {
            QMutexLocker locker(&m_mutex);
            m_cameraHandle = -1;
            m_cameraOpened = false;
        }
        emit cameraStatusChanged(false);
        return false;
    } catch (...) {
        VFP_DEBUG << "Failed to open camera: Unknown error";
        {
            QMutexLocker locker(&m_mutex);
            m_cameraHandle = -1;
            m_cameraOpened = false;
        }
        emit cameraStatusChanged(false);
        return false;
    }
}

void HalconImageSourceNode::closeCamera()
{
    QMutexLocker locker(&m_mutex);
    
    try {
        if (m_cameraOpened) {
            VFP_DEBUG << "Closing camera";
            // 重置相机状态
            m_cameraHandle = -1;
            m_cameraOpened = false;
            VFP_DEBUG << "Camera closed successfully";
            emit cameraStatusChanged(false);
        }
    } catch (HException &e) {
        VFP_DEBUG << "Failed to close camera:" << e.ErrorMessage().Text();
        m_cameraHandle = -1;
        m_cameraOpened = false;
        emit cameraStatusChanged(false);
    } catch (...) {
        VFP_DEBUG << "Failed to close camera: Unknown error";
        m_cameraHandle = -1;
        m_cameraOpened = false;
        emit cameraStatusChanged(false);
    }
}

bool HalconImageSourceNode::grabImage()
{
    QMutexLocker locker(&m_mutex);
    
    try {
        if (!m_cameraOpened) {
            return false;
        }
        
        // 模拟相机采集
        VFP_DEBUG << "Grabbing image from camera";
        
        // 创建一个测试图像
        HImage testImage;
        GenImageConst(&testImage, "byte", 640, 480);
        
        m_outputImage = testImage;
        m_imageWidth = 640;
        m_imageHeight = 480;
        m_pixelFormat = "Mono8";
        
        // 更新输出信息
        updateOutputInfo();
        
        // 发送图像获取信号
        emit imageAcquired(m_outputImage);
        
        return true;
        
    } catch (HException &e) {
        VFP_DEBUG << "Failed to grab image:" << e.ErrorMessage().Text();
        return false;
    } catch (...) {
        VFP_DEBUG << "Failed to grab image: Unknown error";
        return false;
    }
}

bool HalconImageSourceNode::loadImageFromFile()
{
    QMutexLocker locker(&m_mutex);
    
    try {
        if (m_isDirectory) {
            if (m_imageFiles.isEmpty()) {
                VFP_DEBUG << "No image files in directory";
                return false;
            }
            
            // 循环加载目录中的图像
            if (m_currentImageIndex >= m_imageFiles.size()) {
                m_currentImageIndex = 0;
            }
            
            QString imagePath = m_imageFiles[m_currentImageIndex];
            m_currentImageIndex++;
            
            VFP_DEBUG << "Loading image from file:" << imagePath;
            
            HImage image;
            ReadImage(&image, imagePath.toUtf8().constData());
            
            m_outputImage = image;
            m_imageWidth = image.Width();
            m_imageHeight = image.Height();
            m_pixelFormat = "RGB";
            
            // 更新输出信息
            updateOutputInfo();
            
            // 发送图像获取信号
            emit imageAcquired(m_outputImage);
            
            return true;
        } else {
            // 加载单个图像文件
            VFP_DEBUG << "Loading image from file:" << m_filePath;
            
            HImage image;
            ReadImage(&image, m_filePath.toUtf8().constData());
            
            m_outputImage = image;
            m_imageWidth = image.Width();
            m_imageHeight = image.Height();
            m_pixelFormat = "RGB";
            
            // 更新输出信息
            updateOutputInfo();
            
            // 发送图像获取信号
            emit imageAcquired(m_outputImage);
            
            return true;
        }
    } catch (HException &e) {
        VFP_DEBUG << "Failed to load image from file:" << e.ErrorMessage().Text();
        return false;
    } catch (...) {
        VFP_DEBUG << "Failed to load image from file: Unknown error";
        return false;
    }
}

void HalconImageSourceNode::updateOutputInfo()
{
    // 更新输出端口信息
    if (!m_outputImage.IsInitialized()) {
        return;
    }
    
    QSharedPointer<DataObject> output(new DataObject(DataObject::DataType::Image, QVariant()));
    output->setHImage(m_outputImage);
    setOutputData(0, output);
}

void HalconImageSourceNode::updateImageFiles()
{
    m_imageFiles.clear();
    
    QDir dir(m_filePath);
    QStringList filters;
    filters << "*.bmp" << "*.jpg" << "*.jpeg" << "*.png" << "*.tiff" << "*.tif";
    dir.setNameFilters(filters);
    
    m_imageFiles = dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (int i = 0; i < m_imageFiles.size(); i++) {
        m_imageFiles[i] = dir.absoluteFilePath(m_imageFiles[i]);
    }
    
    m_currentImageIndex = 0;
}

void HalconImageSourceNode::readCameraParams()
{
    try {
        VFP_DEBUG << "Reading camera parameters";
        // 这里添加读取相机参数的逻辑
        // 实际项目中这里应该调用相机SDK的API读取参数
    } catch (HException &e) {
        VFP_DEBUG << "Failed to read camera parameters:" << e.ErrorMessage().Text();
    } catch (...) {
        VFP_DEBUG << "Failed to read camera parameters: Unknown error";
    }
}

QWidget *HalconImageSourceNode::createParamPanel()
{
    QWidget *panel = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(panel);
    
    // 图像源类型选择
    QLabel *sourceTypeLabel = new QLabel("图像源类型:");
    QComboBox *sourceTypeCombo = new QComboBox();
    sourceTypeCombo->addItem("本地文件", 0);
    sourceTypeCombo->addItem("相机", 1);
    sourceTypeCombo->setCurrentIndex(getParam("sourceType").toInt());
    layout->addWidget(sourceTypeLabel);
    layout->addWidget(sourceTypeCombo);
    
    // 本地文件路径
    QLabel *filePathLabel = new QLabel("文件路径:");
    QLineEdit *filePathEdit = new QLineEdit(getParam("filePath").toString());
    QPushButton *browseButton = new QPushButton("浏览");
    QHBoxLayout *fileLayout = new QHBoxLayout();
    fileLayout->addWidget(filePathEdit);
    fileLayout->addWidget(browseButton);
    layout->addWidget(filePathLabel);
    layout->addLayout(fileLayout);
    
    // 本地文件模式选择
    QLabel *fileModeLabel = new QLabel("模式:");
    QComboBox *fileModeCombo = new QComboBox();
    fileModeCombo->addItem("单文件", false);
    fileModeCombo->addItem("多文件(目录)", true);
    fileModeCombo->setCurrentIndex(fileModeCombo->findData(getParam("isDirectory").toBool()));
    layout->addWidget(fileModeLabel);
    layout->addWidget(fileModeCombo);
    
    // 相机参数
    QLabel *cameraLabel = new QLabel("相机参数:");
    layout->addWidget(cameraLabel);
    
    // 相机选择
    QLabel *cameraSelectLabel = new QLabel("选择相机:");
    QComboBox *cameraSelectCombo = new QComboBox();
    cameraSelectCombo->addItem("正在检测相机...");
    cameraSelectCombo->setEnabled(false);
    layout->addWidget(cameraSelectLabel);
    layout->addWidget(cameraSelectCombo);
    
    // 刷新相机列表按钮
    QPushButton *refreshCameraButton = new QPushButton("刷新相机列表");
    layout->addWidget(refreshCameraButton);
    
    // 触发模式
    QLabel *triggerModeLabel = new QLabel("触发模式:");
    QComboBox *triggerModeCombo = new QComboBox();
    triggerModeCombo->addItem("软件触发", 0);
    triggerModeCombo->addItem("硬件触发", 1);
    triggerModeCombo->addItem("自由运行", 2);
    triggerModeCombo->setCurrentIndex(getParam("triggerMode").toInt());
    layout->addWidget(triggerModeLabel);
    layout->addWidget(triggerModeCombo);
    
    // 曝光时间
    QLabel *exposureLabel = new QLabel("曝光时间 (μs):");
    QLineEdit *exposureEdit = new QLineEdit(QString::number(getParam("exposureTime").toDouble()));
    layout->addWidget(exposureLabel);
    layout->addWidget(exposureEdit);
    
    // 增益
    QLabel *gainLabel = new QLabel("增益:");
    QLineEdit *gainEdit = new QLineEdit(QString::number(getParam("gain").toDouble()));
    layout->addWidget(gainLabel);
    layout->addWidget(gainEdit);
    
    // 帧率
    QLabel *frameRateLabel = new QLabel("帧率:");
    QLineEdit *frameRateEdit = new QLineEdit(QString::number(getParam("frameRate").toDouble()));
    layout->addWidget(frameRateLabel);
    layout->addWidget(frameRateEdit);
    
    // 像素格式
    QLabel *pixelFormatLabel = new QLabel("像素格式:");
    QComboBox *pixelFormatCombo = new QComboBox();
    pixelFormatCombo->addItem("默认");
    pixelFormatCombo->addItem("Mono8");
    pixelFormatCombo->addItem("RGB8");
    pixelFormatCombo->addItem("BayerRG8");
    pixelFormatCombo->setCurrentText(getParam("pixelFormat").toString());
    layout->addWidget(pixelFormatLabel);
    layout->addWidget(pixelFormatCombo);
    
    // 添加参数确认按钮
    QPushButton *applyParamsButton = new QPushButton("应用参数到相机");
    applyParamsButton->setStyleSheet(VisionWorkbenchStyle::buttonSuccessStyle());
    layout->addWidget(applyParamsButton);

    // ── 脏标记追踪：参数被用户修改后底色变红，应用成功恢复 ──
    auto clearDirty = [exposureEdit, gainEdit, frameRateEdit]() {
        VisionWorkbenchStyle::markInputDirty(exposureEdit, false);
        VisionWorkbenchStyle::markInputDirty(gainEdit, false);
        VisionWorkbenchStyle::markInputDirty(frameRateEdit, false);
    };
    
    // 连接信号
    connect(sourceTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
        int type = sourceTypeCombo->currentData().toInt();
        setParam("sourceType", type);
    });
    
    connect(filePathEdit, &QLineEdit::textChanged, [=](const QString &text) {
        setParam("filePath", text);
    });
    
    connect(browseButton, &QPushButton::clicked, [=]() {
        QFileDialog dialog(nullptr);
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setNameFilter("Image files (*.bmp *.jpg *.jpeg *.png *.tiff *.tif)");
        if (dialog.exec()) {
            QStringList fileNames = dialog.selectedFiles();
            if (!fileNames.isEmpty()) {
                filePathEdit->setText(fileNames.first());
                setParam("filePath", fileNames.first());
            }
        }
    });
    
    connect(fileModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
        bool isDir = fileModeCombo->currentData().toBool();
        setParam("isDirectory", isDir);
    });
    
    connect(cameraSelectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
        QString cameraName = cameraSelectCombo->currentText();
        setSelectedCamera(cameraName);
    });
    
    // 相机检测和刷新逻辑
    auto refreshCameras = [=]() {
        cameraSelectCombo->clear();
        cameraSelectCombo->addItem("正在检测相机...");
        cameraSelectCombo->setEnabled(false);
        
        QThreadPool::globalInstance()->start([=]() {
            try {
                QStringList detectedCameras = detectCamerasInThread();
                
                QMetaObject::invokeMethod(cameraSelectCombo, [=]() {
                    QSignalBlocker blocker(cameraSelectCombo);
                    cameraSelectCombo->clear();
                    
                    if (detectedCameras.isEmpty()) {
                        cameraSelectCombo->addItem("未检测到相机");
                        cameraSelectCombo->setEnabled(false);
                    } else {
                        cameraSelectCombo->setEnabled(true);
                        for (const QString &camera : detectedCameras) {
                            cameraSelectCombo->addItem(camera);
                        }
                        
                        // 恢复当前选中的相机
                        QString currentCamera = getSelectedCamera();
                        int index = cameraSelectCombo->findText(currentCamera);
                        if (index >= 0) {
                            cameraSelectCombo->setCurrentIndex(index);
                        }
                    }
                }, Qt::QueuedConnection);
            } catch (std::exception &e) {
                VFP_DEBUG << "Exception in refresh camera list:" << e.what();
            } catch (...) {
                VFP_DEBUG << "Unknown exception in refresh camera list";
            }
        });
    };
    
    // 连接刷新相机按钮
    connect(refreshCameraButton, &QPushButton::clicked, refreshCameras);
    
    connect(triggerModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
        int mode = triggerModeCombo->currentData().toInt();
        setParam("triggerMode", mode);
    });
    
    connect(exposureEdit, &QLineEdit::textChanged, [=](const QString &text) {
        bool ok;
        double value = text.toDouble(&ok);
        if (ok) {
            setParam("exposureTime", value);
            VisionWorkbenchStyle::markInputDirty(exposureEdit, true);
        }
    });
    
    connect(gainEdit, &QLineEdit::textChanged, [=](const QString &text) {
        bool ok;
        double value = text.toDouble(&ok);
        if (ok) {
            setParam("gain", value);
            VisionWorkbenchStyle::markInputDirty(gainEdit, true);
        }
    });
    
    connect(frameRateEdit, &QLineEdit::textChanged, [=](const QString &text) {
        bool ok;
        double value = text.toDouble(&ok);
        if (ok) {
            setParam("frameRate", value);
            VisionWorkbenchStyle::markInputDirty(frameRateEdit, true);
        }
    });
    
    // 连接参数确认按钮信号
    connect(applyParamsButton, &QPushButton::clicked, [=]() {
        applyCameraParams();
    });
    
    // 后台线程成功应用参数后清除脏标记
    connect(this, &HalconImageSourceNode::paramsApplied, panel, clearDirty);
    
    return panel;
}

void HalconImageSourceNode::updateParamPanel(QWidget *panel)
{
    // 更新参数面板
    if (!panel) return;
    
    QComboBox *sourceTypeCombo = panel->findChild<QComboBox *>();
    if (sourceTypeCombo) {
        sourceTypeCombo->setCurrentIndex(getParam("sourceType").toInt());
    }
    
    QLineEdit *filePathEdit = panel->findChild<QLineEdit *>();
    if (filePathEdit) {
        filePathEdit->setText(getParam("filePath").toString());
    }
    
    QComboBox *fileModeCombo = panel->findChild<QComboBox *>();
    if (fileModeCombo) {
        fileModeCombo->setCurrentIndex(fileModeCombo->findData(getParam("isDirectory").toBool()));
    }
    
    QComboBox *triggerModeCombo = panel->findChild<QComboBox *>();
    if (triggerModeCombo) {
        triggerModeCombo->setCurrentIndex(getParam("triggerMode").toInt());
    }
    
    QLineEdit *exposureEdit = panel->findChild<QLineEdit *>();
    if (exposureEdit) {
        exposureEdit->setText(QString::number(getParam("exposureTime").toDouble()));
    }
    
    QLineEdit *gainEdit = panel->findChild<QLineEdit *>();
    if (gainEdit) {
        gainEdit->setText(QString::number(getParam("gain").toDouble()));
    }
    
    QLineEdit *frameRateEdit = panel->findChild<QLineEdit *>();
    if (frameRateEdit) {
        frameRateEdit->setText(QString::number(getParam("frameRate").toDouble()));
    }
    
    QComboBox *pixelFormatCombo = panel->findChild<QComboBox *>();
    if (pixelFormatCombo) {
        pixelFormatCombo->setCurrentText(getParam("pixelFormat").toString());
    }
}

void HalconImageSourceNode::displayImage()
{
    // 显示图像
    QSharedPointer<DataObject> outputData = getOutputData(0);
    if (outputData) {
        HImage image = outputData->getHImage();
        if (image.IsInitialized()) {
            // 这里可以添加图像显示逻辑
            VFP_DEBUG << "Displaying image from HalconImageSourceNode";
        }
    }
}
