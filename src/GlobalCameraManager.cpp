#include "GlobalCameraManager.h"
#include <QMutex>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "AppLog.h"
#include "MvCameraControl.h"
#include "PixelType.h"

GlobalCameraManager *GlobalCameraManager::instance()
{
    // C++11 起函数内静态变量的初始化由编译器保证线程安全（一次性初始化保护）。
    // 原实现是「非原子裸指针 + 双重检查锁」（DCLP）：第一次无锁读与写指针之间存在
    // 数据竞争，属未定义行为，可能返回未完全构造的对象。
    // 仍保持"常驻不析构"语义：句柄由 MainWindow::cleanupCameras 显式关闭，
    // 避免单例析构与 QApplication 析构顺序冲突。
    static GlobalCameraManager *s_instance = new GlobalCameraManager();
    return s_instance;
}

GlobalCameraManager::GlobalCameraManager(QObject *parent)
    : QObject(parent)
{
    initializeDefaultCameras();
}

GlobalCameraManager::~GlobalCameraManager()
{
    for (auto it = m_cameraHandles.begin(); it != m_cameraHandles.end(); ++it) {
        void* hCamera = it.value();
        if (hCamera) {
            MV_CC_CloseDevice(hCamera);
            MV_CC_DestroyHandle(hCamera);
        }
    }
    m_cameraHandles.clear();
    QMutexLocker lck(&m_cameraConsumerMutex);
    m_cameraConsumers.clear();
}

void GlobalCameraManager::initializeDefaultCameras()
{
    for (int i = 1; i <= 8; i++) {
        QString cameraName = QString("Camera%1").arg(i);
        Camera camera;
        camera.name = cameraName;
        camera.deviceName = "";
        camera.description = "";
        camera.params = CameraParams();
        m_cameras[cameraName] = camera;
        m_cameraHandles[cameraName] = nullptr;
    }
}

// ──────────────────────────────────────────────────────
// Pixel Format helpers — 单一数据源，消除多处重复映射
// ──────────────────────────────────────────────────────

// 已知像素格式的枚举值 → 显示名称映射表
static const QHash<unsigned int, QString> &s_pixelFormatMap()
{
    static const QHash<unsigned int, QString> map = {
        {PixelType_Gvsp_Mono8,        QStringLiteral("Mono8")},
        {PixelType_Gvsp_Mono10,       QStringLiteral("Mono10")},
        {PixelType_Gvsp_Mono12,       QStringLiteral("Mono12")},
        {PixelType_Gvsp_BayerGR8,     QStringLiteral("BayerGR8")},
        {PixelType_Gvsp_BayerRG8,     QStringLiteral("BayerRG8")},
        {PixelType_Gvsp_BayerGB8,     QStringLiteral("BayerGB8")},
        {PixelType_Gvsp_BayerBG8,     QStringLiteral("BayerBG8")},
        {PixelType_Gvsp_RGB8_Packed,  QStringLiteral("RGB8")},
        {PixelType_Gvsp_BGR8_Packed,  QStringLiteral("BGR8")},
    };
    return map;
}

QList<QPair<QString, unsigned int>> GlobalCameraManager::getSupportedPixelFormats(void *hCamera)
{
    const auto &knownMap = s_pixelFormatMap();

    if (!hCamera) {
        QList<QPair<QString, unsigned int>> result;
        for (auto it = knownMap.cbegin(); it != knownMap.cend(); ++it) {
            result.append({it.value(), it.key()});
        }
        return result;
    }

    QList<QPair<QString, unsigned int>> formats;

    MVCC_ENUMVALUE stEnum;
    memset(&stEnum, 0, sizeof(MVCC_ENUMVALUE));
    int nRet = MV_CC_GetEnumValue(hCamera, "PixelFormat", &stEnum);
    if (nRet != MV_OK || stEnum.nSupportedNum == 0) {
        VFP_DEBUG << "无法获取相机支持的像素格式列表，使用默认列表";
        return {
            {QStringLiteral("Mono8"),  PixelType_Gvsp_Mono8},
            {QStringLiteral("RGB8"),   PixelType_Gvsp_RGB8_Packed},
            {QStringLiteral("BGR8"),   PixelType_Gvsp_BGR8_Packed},
        };
    }

    for (unsigned int i = 0; i < stEnum.nSupportedNum; ++i) {
        unsigned int nVal = stEnum.nSupportValue[i];
        QString displayName = knownMap.value(nVal);
        if (displayName.isEmpty()) {
            // 未知枚举值 → 从相机获取符号名
            MVCC_ENUMENTRY stEntry;
            memset(&stEntry, 0, sizeof(MVCC_ENUMENTRY));
            stEntry.nValue = nVal;
            if (MV_CC_GetEnumEntrySymbolic(hCamera, "PixelFormat", &stEntry) == MV_OK) {
                displayName = QString::fromUtf8(stEntry.chSymbolic);
            }
        }
        if (displayName.isEmpty()) continue;
        bool dup = false;
        for (const auto &pair : formats) {
            if (pair.first == displayName || pair.second == nVal) { dup = true; break; }
        }
        if (!dup) formats.append({displayName, nVal});
    }

    VFP_DEBUG << "相机支持" << formats.size() << "种像素格式";
    return formats;
}

QString GlobalCameraManager::pixelFormatEnumToName(unsigned int enumValue)
{
    const auto &map = s_pixelFormatMap();
    return map.value(enumValue);
}

unsigned int GlobalCameraManager::pixelFormatNameToEnum(const QString &name)
{
    const auto &map = s_pixelFormatMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.value() == name)
            return it.key();
    }
    return 0;
}

unsigned int GlobalCameraManager::readPixelFormatValue(void *hCamera)
{
    if (!hCamera) return 0;
    MVCC_ENUMVALUE stVal;
    memset(&stVal, 0, sizeof(MVCC_ENUMVALUE));
    if (MV_CC_GetEnumValue(hCamera, "PixelFormat", &stVal) != MV_OK)
        return 0;
    return stVal.nCurValue;
}

bool GlobalCameraManager::writePixelFormatValue(void *hCamera, unsigned int enumValue)
{
    if (!hCamera || enumValue == 0) return false;
    MV_CC_StopGrabbing(hCamera);
    int nRet = MV_CC_SetEnumValue(hCamera, "PixelFormat", enumValue);
    MV_CC_StartGrabbing(hCamera);
    return nRet == MV_OK;
}

// ──────────────────────────────────────────────────────
// 现有接口
// ──────────────────────────────────────────────────────

bool GlobalCameraManager::setCameraParams(const QString &cameraName, const CameraParams &params)
{
    void* hCamera = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_cameras.contains(cameraName)) {
            return false;
        }
        m_cameras[cameraName].params = params;
        hCamera = m_cameraHandles.value(cameraName, nullptr);
    }
    if (hCamera) {
        int nRet = 0;
        nRet = MV_CC_SetFloatValue(hCamera, "ExposureTime", static_cast<float>(params.exposureTime));
        if (nRet != MV_OK) VFP_DEBUG << "设置曝光时间到设备失败，错误码:" << nRet;
        nRet = MV_CC_SetFloatValue(hCamera, "Gain", static_cast<float>(params.gain));
        if (nRet != MV_OK) VFP_DEBUG << "设置增益到设备失败，错误码:" << nRet;
        nRet = MV_CC_SetFloatValue(hCamera, "AcquisitionFrameRate", static_cast<float>(params.frameRate));
        if (nRet != MV_OK) VFP_DEBUG << "设置帧率到设备失败（部分相机不支持）:" << nRet;

        VFP_DEBUG << "相机参数已写入物理设备:" << cameraName;
    }
    return true;
}

bool GlobalCameraManager::setCameraParams(const QString &cameraName, double exposureTime, double gain, double frameRate, const QString &pixelFormat)
{
    void* hCamera = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_cameras.contains(cameraName)) {
            return false;
        }
        m_cameras[cameraName].params.exposureTime = exposureTime;
        m_cameras[cameraName].params.gain = gain;
        m_cameras[cameraName].params.frameRate = frameRate;
        m_cameras[cameraName].params.pixelFormat = pixelFormat;
        hCamera = m_cameraHandles.value(cameraName, nullptr);
    }
    if (hCamera) {
        int nRet = 0;
        nRet = MV_CC_SetFloatValue(hCamera, "ExposureTime", static_cast<float>(exposureTime));
        if (nRet != MV_OK) VFP_DEBUG << "设置曝光时间到设备失败，错误码:" << nRet;
        nRet = MV_CC_SetFloatValue(hCamera, "Gain", static_cast<float>(gain));
        if (nRet != MV_OK) VFP_DEBUG << "设置增益到设备失败，错误码:" << nRet;
        nRet = MV_CC_SetFloatValue(hCamera, "AcquisitionFrameRate", static_cast<float>(frameRate));
        if (nRet != MV_OK) VFP_DEBUG << "设置帧率到设备失败（部分相机不支持）:" << nRet;

        VFP_DEBUG << "相机参数已成功写入物理设备:" << cameraName;
        return true;
    }
    VFP_DEBUG << "相机未打开，参数仅保存到内存:" << cameraName;
    return false;
}

// ──────────────────────────────────────────────────────
// 其余方法保持不变
// ──────────────────────────────────────────────────────

bool GlobalCameraManager::cameraExists(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    return m_cameras.contains(name);
}

QMap<QString, GlobalCameraManager::Camera> GlobalCameraManager::cameras() const
{
    QMutexLocker locker(&m_mutex);
    return m_cameras;
}

int GlobalCameraManager::cameraCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_cameras.size();
}

GlobalCameraManager::Camera GlobalCameraManager::getCamera(const QString &cameraName) const
{
    QMutexLocker locker(&m_mutex);
    if (m_cameras.contains(cameraName)) {
        return m_cameras[cameraName];
    }
    return Camera();
}

bool GlobalCameraManager::setCameraDevice(const QString &cameraName, const QString &deviceName)
{
    {
        QMutexLocker locker(&m_mutex);
        if (!m_cameras.contains(cameraName)) {
            return false;
        }
        m_cameras[cameraName].deviceName = deviceName;
    }
    emit cameraChanged(cameraName, deviceName);
    return true;
}

bool GlobalCameraManager::removeCameraDevice(const QString &cameraName)
{
    {
        QMutexLocker locker(&m_mutex);
        if (!m_cameras.contains(cameraName)) {
            return false;
        }
    }
    if (isCameraOpen(cameraName)) {
        closeCamera(cameraName);
    }
    {
        QMutexLocker locker(&m_mutex);
        if (!m_cameras.contains(cameraName)) {
            return false;
        }
        m_cameras[cameraName].deviceName = "";
    }
    emit cameraChanged(cameraName, "");
    return true;
}

bool GlobalCameraManager::isDeviceUsedByOtherCamera(const QString &deviceName, const QString &excludeCameraName) const
{
    if (deviceName.isEmpty()) return false;
    QMutexLocker locker(&m_mutex);
    for (auto it = m_cameras.begin(); it != m_cameras.end(); ++it) {
        if (it.key() != excludeCameraName && it.value().deviceName == deviceName)
            return true;
    }
    return false;
}

bool GlobalCameraManager::isCameraOpen(const QString &cameraName) const
{
    QMutexLocker locker(&m_mutex);
    return m_cameraHandles.contains(cameraName) && m_cameraHandles[cameraName] != nullptr;
}

bool GlobalCameraManager::isCameraBound(const QString &cameraName) const
{
    QMutexLocker locker(&m_mutex);
    if (!m_cameras.contains(cameraName)) return false;
    return !m_cameras[cameraName].deviceName.isEmpty();
}

void GlobalCameraManager::updateCameraCache(const QString &cameraName, const CameraParams &params)
{
    QMutexLocker locker(&m_mutex);
    if (m_cameras.contains(cameraName)) {
        m_cameras[cameraName].params = params;
        VFP_DEBUG << "更新相机缓存:" << cameraName << "pixelFormat =" << params.pixelFormat;
    }
}

// ──────────────────────────────────────────────────────

// 帮助函数：从枚举值查找显示名称
// 优先用 fallback 表（显示名如 "RGB8"），仅在不匹配时才用相机的原生符号名
static QString enumValueToDisplayName(unsigned int val, void *hCamera)
{
    const auto &map = s_pixelFormatMap();
    QString name = map.value(val);
    if (!name.isEmpty()) return name;

    // 不在映射表中的值 → 尝试从相机获取符号名
    if (hCamera) {
        MVCC_ENUMENTRY stEntry;
        memset(&stEntry, 0, sizeof(MVCC_ENUMENTRY));
        stEntry.nValue = val;
        if (MV_CC_GetEnumEntrySymbolic(hCamera, "PixelFormat", &stEntry) == MV_OK) {
            return QString::fromUtf8(stEntry.chSymbolic);
        }
    }
    return QStringLiteral("Mono8");
}

bool GlobalCameraManager::readCameraParams(const QString &cameraName)
{
    void* hCamera = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_cameras.contains(cameraName)) {
            return false;
        }
        hCamera = m_cameraHandles.value(cameraName);
    }
    if (!hCamera) {
        VFP_DEBUG << "相机句柄无效:" << cameraName;
        return false;
    }
    
    try {
        int nRet = 0;
        CameraParams params;
        
        // 读取曝光时间
        MVCC_FLOATVALUE stExposure;
        memset(&stExposure, 0, sizeof(MVCC_FLOATVALUE));
        nRet = MV_CC_GetFloatValue(hCamera, "ExposureTime", &stExposure);
        if (nRet == MV_OK) params.exposureTime = stExposure.fCurValue;
        else params.exposureTime = 10000.0;
        
        // 读取增益
        MVCC_FLOATVALUE stGain;
        memset(&stGain, 0, sizeof(MVCC_FLOATVALUE));
        nRet = MV_CC_GetFloatValue(hCamera, "Gain", &stGain);
        if (nRet == MV_OK) params.gain = stGain.fCurValue;
        else params.gain = 0.0;
        
        // 读取帧率
        MVCC_FLOATVALUE stFrameRate;
        memset(&stFrameRate, 0, sizeof(MVCC_FLOATVALUE));
        nRet = MV_CC_GetFloatValue(hCamera, "AcquisitionFrameRate", &stFrameRate);
        if (nRet == MV_OK) params.frameRate = stFrameRate.fCurValue;
        else params.frameRate = 30.0;
        
        // 读取图像宽度
        MVCC_INTVALUE stWidth;
        memset(&stWidth, 0, sizeof(MVCC_INTVALUE));
        nRet = MV_CC_GetIntValue(hCamera, "Width", &stWidth);
        if (nRet == MV_OK) params.width = stWidth.nCurValue;
        else params.width = 2448;
        
        // 读取图像高度
        MVCC_INTVALUE stHeight;
        memset(&stHeight, 0, sizeof(MVCC_INTVALUE));
        nRet = MV_CC_GetIntValue(hCamera, "Height", &stHeight);
        if (nRet == MV_OK) params.height = stHeight.nCurValue;
        else params.height = 2048;
        
        // 读取像素格式 — 整数枚举值 → 显示名称
        {
            unsigned int curVal = readPixelFormatValue(hCamera);
            params.pixelFormat = enumValueToDisplayName(curVal, hCamera);
            VFP_DEBUG << "读取像素格式: enum=" << curVal << "name=" << params.pixelFormat;
        }
        
        // 读取触发模式
        MVCC_ENUMVALUE stTriggerMode;
        memset(&stTriggerMode, 0, sizeof(MVCC_ENUMVALUE));
        nRet = MV_CC_GetEnumValue(hCamera, "TriggerMode", &stTriggerMode);
        params.triggerMode = (nRet == MV_OK && stTriggerMode.nCurValue == 0) ? "Off" : "On";
        
        // 读取触发源
        MVCC_ENUMVALUE stTriggerSource;
        memset(&stTriggerSource, 0, sizeof(MVCC_ENUMVALUE));
        nRet = MV_CC_GetEnumValue(hCamera, "TriggerSource", &stTriggerSource);
        if (nRet == MV_OK) {
            switch (stTriggerSource.nCurValue) {
            case 0: params.triggerSource = "Line0"; break;
            case 1: params.triggerSource = "Line1"; break;
            case 2: params.triggerSource = "Line2"; break;
            case 3: params.triggerSource = "Line3"; break;
            case 4: case 7: params.triggerSource = "Software"; break;
            default: params.triggerSource = "Line0"; break;
            }
        } else {
            params.triggerSource = "Line0";
        }
        
        {
            QMutexLocker locker(&m_mutex);
            if (m_cameras.contains(cameraName)) {
                m_cameras[cameraName].params = params;
            }
        }
        VFP_DEBUG << "相机参数读取成功:" << cameraName;
        return true;
    } catch (...) {
        VFP_DEBUG << "读取相机参数时发生异常";
        return false;
    }
}

void* GlobalCameraManager::getCameraHandle(const QString &cameraName)
{
    QMutexLocker locker(&m_mutex);
    if (m_cameraHandles.contains(cameraName)) {
        return m_cameraHandles[cameraName];
    }
    return nullptr;
}

bool GlobalCameraManager::setCameraLineMode(const QString &cameraName, int lineIndex, int mode)
{
    void *hCamera = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (m_cameraHandles.contains(cameraName))
            hCamera = m_cameraHandles[cameraName];
    }
    if (!hCamera) {
        VFP_DEBUG << "setCameraLineMode 失败：相机未打开/未找到" << cameraName;
        return false;
    }
    // 线方向：GenICam LineMode 枚举 Input=0 / Output=1
    int nRet = MV_CC_SetEnumValue(hCamera, "LineSelector", static_cast<unsigned int>(lineIndex));
    if (nRet != MV_OK) {
        VFP_DEBUG << "setCameraLineMode LineSelector 失败，错误码:" << nRet;
        return false;
    }
    nRet = MV_CC_SetEnumValue(hCamera, "LineMode", static_cast<unsigned int>(mode));
    if (nRet != MV_OK) {
        VFP_DEBUG << "setCameraLineMode LineMode 失败，错误码:" << nRet;
        return false;
    }
    return true;
}

bool GlobalCameraManager::setCameraLineValue(const QString &cameraName, int lineIndex, bool value)
{
    void *hCamera = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (m_cameraHandles.contains(cameraName))
            hCamera = m_cameraHandles[cameraName];
    }
    if (!hCamera) {
        VFP_DEBUG << "setCameraLineValue 失败：相机未打开/未找到" << cameraName;
        return false;
    }
    int nRet = MV_CC_SetEnumValue(hCamera, "LineSelector", static_cast<unsigned int>(lineIndex));
    if (nRet != MV_OK) {
        VFP_DEBUG << "setCameraLineValue LineSelector 失败，错误码:" << nRet;
        return false;
    }
    // 写入前确保该线为输出方向，避免对输入线误写
    nRet = MV_CC_SetEnumValue(hCamera, "LineMode", 1);
    if (nRet != MV_OK) {
        VFP_DEBUG << "setCameraLineValue LineMode 失败，错误码:" << nRet;
        return false;
    }
    nRet = MV_CC_SetBoolValue(hCamera, "LineStatus", value ? true : false);
    if (nRet != MV_OK) {
        VFP_DEBUG << "setCameraLineValue LineStatus 失败，错误码:" << nRet;
        return false;
    }
    return true;
}

bool GlobalCameraManager::getCameraLineValue(const QString &cameraName, int lineIndex, bool &value)
{
    void *hCamera = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (m_cameraHandles.contains(cameraName))
            hCamera = m_cameraHandles[cameraName];
    }
    if (!hCamera) {
        VFP_DEBUG << "getCameraLineValue 失败：相机未打开/未找到" << cameraName;
        return false;
    }
    int nRet = MV_CC_SetEnumValue(hCamera, "LineSelector", static_cast<unsigned int>(lineIndex));
    if (nRet != MV_OK) {
        VFP_DEBUG << "getCameraLineValue LineSelector 失败，错误码:" << nRet;
        return false;
    }
    bool b = false;
    nRet = MV_CC_GetBoolValue(hCamera, "LineStatus", &b);
    if (nRet != MV_OK) {
        VFP_DEBUG << "getCameraLineValue LineStatus 失败，错误码:" << nRet;
        return false;
    }
    value = b;
    return true;
}

bool GlobalCameraManager::openCamera(const QString &cameraName)
{
    QString deviceName;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_cameras.contains(cameraName)) {
            VFP_DEBUG << "相机不存在:" << cameraName;
            return false;
        }
        deviceName = m_cameras[cameraName].deviceName;
        if (deviceName.isEmpty()) {
            VFP_DEBUG << "相机未绑定设备:" << cameraName;
            return false;
        }
        if (m_cameraHandles.contains(cameraName) && m_cameraHandles[cameraName] != nullptr) {
            VFP_DEBUG << "相机已打开:" << cameraName;
            return true;
        }
    }
    
    VFP_DEBUG << "打开相机:" << cameraName;
    
    try {
        // 枚举设备
        MV_CC_DEVICE_INFO_LIST stDeviceList;
        memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
        int nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
        if (nRet != MV_OK) {
            VFP_DEBUG << "枚举设备失败，错误码:" << nRet;
            return false;
        }
        
        // 根据设备名称查找对应设备
        int deviceIndex = -1;
        int cameraNumber = -1;
        if (deviceName.startsWith("Camera ")) {
            cameraNumber = deviceName.mid(7).toInt() - 1;
        } else if (deviceName.startsWith("GigE: Camera ")) {
            int pos = deviceName.indexOf("Camera ") + 7;
            int endPos = deviceName.indexOf(" ", pos);
            cameraNumber = deviceName.mid(pos, endPos - pos).toInt() - 1;
        } else if (deviceName.startsWith("USB: Camera ")) {
            cameraNumber = deviceName.mid(11).toInt() - 1;
        }
        
        // 尝试通过完整名称匹配
        for (unsigned int i = 0; i < stDeviceList.nDeviceNum; i++) {
            MV_CC_DEVICE_INFO* pDeviceInfo = stDeviceList.pDeviceInfo[i];
            if (!pDeviceInfo) continue;
            
            if (pDeviceInfo->nTLayerType == MV_GIGE_DEVICE) {
                const auto& gigEInfo = pDeviceInfo->SpecialInfo.stGigEInfo;
                QString name = QString("GigE: Camera %1 (IP: %2.%3.%4.%5)")
                    .arg(i + 1)
                    .arg((gigEInfo.nCurrentIp >> 24) & 0xFF)
                    .arg((gigEInfo.nCurrentIp >> 16) & 0xFF)
                    .arg((gigEInfo.nCurrentIp >> 8) & 0xFF)
                    .arg(gigEInfo.nCurrentIp & 0xFF);
                if (name == deviceName) {
                    deviceIndex = static_cast<int>(i);
                    break;
                }
            } else if (pDeviceInfo->nTLayerType == MV_USB_DEVICE) {
                QString name = QString("USB: Camera %1").arg(i + 1);
                if (name == deviceName) {
                    deviceIndex = static_cast<int>(i);
                    break;
                }
            }
        }
        
        // 如果没找到，使用相机编号
        if (deviceIndex < 0 && cameraNumber >= 0 && static_cast<unsigned int>(cameraNumber) < stDeviceList.nDeviceNum) {
            deviceIndex = cameraNumber;
        }
        
        if (deviceIndex < 0) {
            VFP_DEBUG << "未找到匹配的设备:" << deviceName;
            return false;
        }
        
        MV_CC_DEVICE_INFO* pDeviceInfo = stDeviceList.pDeviceInfo[deviceIndex];
        
        // 创建句柄
        void* hCamera = nullptr;
        nRet = MV_CC_CreateHandle(&hCamera, pDeviceInfo);
        if (nRet != MV_OK || !hCamera) {
            VFP_DEBUG << "创建相机句柄失败，错误码:" << nRet;
            return false;
        }
        
        // 打开设备
        nRet = MV_CC_OpenDevice(hCamera);
        if (nRet != MV_OK) {
            VFP_DEBUG << "打开设备失败，错误码:" << nRet;
            MV_CC_DestroyHandle(hCamera);
            return false;
        }

        {
            QMutexLocker locker(&m_mutex);
            m_cameraHandles[cameraName] = hCamera;
        }
        
        // 开始采集
        nRet = MV_CC_StartGrabbing(hCamera);
        if (nRet != MV_OK) {
            VFP_DEBUG << "开始采集失败，错误码:" << nRet;
        }
        
        VFP_DEBUG << "相机打开成功:" << cameraName;
        emit cameraStatusChanged(cameraName, true);
        
        readCameraParams(cameraName);
        return true;
    } catch (const std::exception& e) {
        VFP_DEBUG << "打开相机时发生异常:" << e.what();
        return false;
    } catch (...) {
        VFP_DEBUG << "打开相机时发生未知异常";
        return false;
    }
}

bool GlobalCameraManager::closeCamera(const QString &cameraName)
{
    void* hCamera = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_cameras.contains(cameraName)) return false;
        if (!m_cameraHandles.contains(cameraName) || m_cameraHandles[cameraName] == nullptr) {
            VFP_DEBUG << "相机未打开:" << cameraName;
            return true;
        }
        hCamera = m_cameraHandles[cameraName];
        m_cameraHandles[cameraName] = nullptr;
    }
    
    VFP_DEBUG << "开始关闭相机:" << cameraName;
    try {
        MV_CC_StopGrabbing(hCamera);
        MV_CC_CloseDevice(hCamera);
        MV_CC_DestroyHandle(hCamera);
        VFP_DEBUG << "相机关闭成功:" << cameraName;
        emit cameraStatusChanged(cameraName, false);
        return true;
    } catch (const std::exception& e) {
        VFP_DEBUG << "关闭相机时发生异常:" << e.what();
        return false;
    } catch (...) {
        VFP_DEBUG << "关闭相机时发生未知异常";
        return false;
    }
}

QStringList GlobalCameraManager::getAvailableDevices() const
{
    QStringList deviceNames;
    MV_CC_DEVICE_INFO_LIST stDeviceList;
    memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    int nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
    if (nRet != MV_OK) {
        VFP_DEBUG << "枚举设备失败，错误码:" << nRet;
        return deviceNames;
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
        if (!deviceName.isEmpty()) deviceNames.append(deviceName);
    }
    return deviceNames;
}

QJsonObject GlobalCameraManager::toJson() const
{
    QJsonObject root;
    QJsonArray camerasArray;
    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_cameras.begin(); it != m_cameras.end(); ++it) {
            QJsonObject cameraObj;
            cameraObj["name"] = it.key();
            cameraObj["deviceName"] = it.value().deviceName;
            cameraObj["description"] = it.value().description;

            const auto& params = it.value().params;
            QJsonObject paramsObj;
            paramsObj["exposureTime"] = params.exposureTime;
            paramsObj["gain"] = params.gain;
            paramsObj["frameRate"] = params.frameRate;
            paramsObj["width"] = params.width;
            paramsObj["height"] = params.height;
            paramsObj["pixelFormat"] = params.pixelFormat;
            paramsObj["triggerMode"] = params.triggerMode;
            paramsObj["triggerSource"] = params.triggerSource;
            cameraObj["params"] = paramsObj;
            camerasArray.append(cameraObj);
        }
    }
    root["cameras"] = camerasArray;
    return root;
}

void GlobalCameraManager::fromJson(const QJsonObject &json)
{
    QJsonArray camerasArray = json["cameras"].toArray();
    {
        QMutexLocker locker(&m_mutex);
        for (const QJsonValue &val : camerasArray) {
            QJsonObject cameraObj = val.toObject();
            QString name = cameraObj["name"].toString();
            if (!m_cameras.contains(name)) continue;
            m_cameras[name].deviceName = cameraObj["deviceName"].toString();
            m_cameras[name].description = cameraObj["description"].toString();

            QJsonObject paramsObj = cameraObj["params"].toObject();
            m_cameras[name].params.exposureTime = paramsObj["exposureTime"].toDouble();
            m_cameras[name].params.gain = paramsObj["gain"].toDouble();
            m_cameras[name].params.frameRate = paramsObj["frameRate"].toDouble();
            m_cameras[name].params.width = paramsObj["width"].toInt();
            m_cameras[name].params.height = paramsObj["height"].toInt();
            m_cameras[name].params.pixelFormat = paramsObj["pixelFormat"].toString();
            m_cameras[name].params.triggerMode = paramsObj["triggerMode"].toString();
            m_cameras[name].params.triggerSource = paramsObj["triggerSource"].toString();
        }
    }
}

bool GlobalCameraManager::saveToFile(const QString &fileName) const
{
    QJsonDocument doc(toJson());
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
        return true;
    }
    return false;
}

bool GlobalCameraManager::loadFromFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return false;

    fromJson(doc.object());
    return true;
}

bool GlobalCameraManager::isCameraUsed(const QString &cameraName) const
{
    QMutexLocker lck(&m_cameraConsumerMutex);
    return m_cameraConsumers.contains(cameraName) && !m_cameraConsumers[cameraName].isEmpty();
}

bool GlobalCameraManager::markCameraUsed(const QString &cameraName, const QString &consumerInstanceId)
{
    QMutexLocker lck(&m_cameraConsumerMutex);
    m_cameraConsumers[cameraName].insert(consumerInstanceId);
    return true;
}

void GlobalCameraManager::markCameraUnused(const QString &cameraName, const QString &consumerInstanceId)
{
    QMutexLocker lck(&m_cameraConsumerMutex);
    if (m_cameraConsumers.contains(cameraName)) {
        m_cameraConsumers[cameraName].remove(consumerInstanceId);
        if (m_cameraConsumers[cameraName].isEmpty()) {
            m_cameraConsumers.remove(cameraName);
        }
    }
}

QStringList GlobalCameraManager::consumersForCamera(const QString &cameraName) const
{
    QMutexLocker lck(&m_cameraConsumerMutex);
    if (m_cameraConsumers.contains(cameraName)) {
        return QStringList(m_cameraConsumers[cameraName].begin(), m_cameraConsumers[cameraName].end());
    }
    return {};
}
