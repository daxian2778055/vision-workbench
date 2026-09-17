#ifndef GLOBALCAMERAMANAGER_H
#define GLOBALCAMERAMANAGER_H

#include <QObject>
#include <QHash>
#include <QMap>
#include <QMutex>
#include <QSet>
#include <QString>

// 相机参数结构体
struct CameraParams {
    double exposureTime;       // 曝光时间 (微秒)
    double gain;               // 增益 (dB)
    double frameRate;          // 帧率 (fps)
    int width;                 // 图像宽度
    int height;                // 图像高度
    QString pixelFormat;       // 像素格式
    QString triggerMode;       // 触发模式
    QString triggerSource;     // 触发源
    
    CameraParams()
        : exposureTime(10000.0)
        , gain(0.0)
        , frameRate(30.0)
        , width(2448)
        , height(2048)
        , pixelFormat("Mono8")
        , triggerMode("Off")
        , triggerSource("Line0")
    {}
};

class GlobalCameraManager : public QObject
{
    Q_OBJECT

public:
    struct Camera {
        QString name;           // 全局相机名称
        QString deviceName;     // 绑定的设备名称
        QString description;    // 相机描述
        CameraParams params;    // 相机参数
    };

    static GlobalCameraManager *instance();

    // 获取所有全局相机
    QMap<QString, Camera> cameras() const;
    
    // 获取全局相机数量
    int cameraCount() const;

    // 设置相机设备
    bool setCameraDevice(const QString &cameraName, const QString &deviceName);

    // 移除相机设备绑定
    bool removeCameraDevice(const QString &cameraName);

    // 设置相机参数
    bool setCameraParams(const QString &cameraName, const CameraParams &params);
    
    // 设置相机参数（重载方法）
    bool setCameraParams(const QString &cameraName, double exposureTime, double gain, double frameRate, const QString &pixelFormat);

    // 检查相机是否存在
    bool cameraExists(const QString &name) const;

    // 检查设备是否已被其他相机绑定
    bool isDeviceUsedByOtherCamera(const QString &deviceName, const QString &excludeCameraName = "") const;

    // 保存全局相机到文件
    bool saveToFile(const QString &fileName) const;

    // 从文件加载全局相机
    bool loadFromFile(const QString &fileName);

    // 序列化到 JSON（供项目文件整体保存）
    QJsonObject toJson() const;
    // 从 JSON 恢复（供项目文件整体加载）
    void fromJson(const QJsonObject &json);

    // 获取可用的相机设备列表
    QStringList getAvailableDevices() const;

    // 打开相机
    bool openCamera(const QString &cameraName);

    // 关闭相机
    bool closeCamera(const QString &cameraName);

    // 读取相机参数
    bool readCameraParams(const QString &cameraName);

    // 获取相机句柄
    void* getCameraHandle(const QString &cameraName);

    /// 相机数字 IO：设置某条 IO 线的方向（mode: 0=输入, 1=输出）。需相机已打开。
    /// 走 MVS GenICam 特性 LineSelector / LineMode，返回是否成功。
    bool setCameraLineMode(const QString &cameraName, int lineIndex, int mode);

    /// 相机数字 IO：写入输出线状态（value=true/false）。内部确保该线为输出方向，
    /// 走 LineSelector / LineMode(Output) / LineStatus。返回是否成功。
    bool setCameraLineValue(const QString &cameraName, int lineIndex, bool value);

    /// 相机数字 IO：读取某条 IO 线状态（输入或输出回读），走 LineSelector / LineStatus。
    /// 成功时 *value 被填充，返回是否成功（失败时不修改 *value）。
    bool getCameraLineValue(const QString &cameraName, int lineIndex, bool &value);

    // 获取相机信息
    Camera getCamera(const QString &cameraName) const;

    // 检查相机是否打开
    bool isCameraOpen(const QString &cameraName) const;

    // 检查相机是否绑定了设备
    bool isCameraBound(const QString &cameraName) const;

    /// 仅更新内存缓存（不写入硬件），避免重复 StopGrabbing/StartGrabbing
    void updateCameraCache(const QString &cameraName, const CameraParams &params);

    /// 写入像素格式到硬件（使用整数枚举值，最可靠）
    /// 已在内部处理 StopGrabbing/StartGrabbing
    static bool writePixelFormatValue(void *hCamera, unsigned int enumValue);

    /// 读取当前像素格式的整数枚举值
    static unsigned int readPixelFormatValue(void *hCamera);

    /// 获取相机支持的像素格式列表（名称 + 枚举值）
    static QList<QPair<QString, unsigned int>> getSupportedPixelFormats(void *hCamera = nullptr);

    /// 将像素格式整数枚举值转换为显示名称（如 0x02180014 → "RGB8"）
    static QString pixelFormatEnumToName(unsigned int enumValue);

    /// 将像素格式显示名称转换为整数枚举值（如 "RGB8" → 0x02180014）
    static unsigned int pixelFormatNameToEnum(const QString &name);

signals:
    // 当全局相机发生变化时发出
    void cameraChanged(const QString &name, const QString &deviceName);

    // 当相机状态发生变化时发出
    void cameraStatusChanged(const QString &name, bool isOpen);

public:
    /// 是否存在至少一个算子占用该全局相机（任何 consumer）
    bool isCameraUsed(const QString &cameraName) const;

    /// 多台 MVS 图像源可共用同一全局相机名：每台算子传入稳定唯一的 consumerInstanceId
    bool markCameraUsed(const QString &cameraName, const QString &consumerInstanceId);

    /// 移除该算子对该全局相机名的登记（同名其它算子不受影响）
    void markCameraUnused(const QString &cameraName, const QString &consumerInstanceId);

    QStringList consumersForCamera(const QString &cameraName) const;

private:
    GlobalCameraManager(QObject *parent = nullptr);
    ~GlobalCameraManager();

    QMap<QString, Camera> m_cameras;
    QMap<QString, void *> m_cameraHandles;
    /// 全局相机名 -> 使用该名的算子实例 ID（可多个）
    QHash<QString, QSet<QString>> m_cameraConsumers;
    mutable QMutex m_cameraConsumerMutex;

    /// 保护 m_cameras / m_cameraHandles 的互斥锁（跨线程安全）
    mutable QMutex m_mutex;

    void initializeDefaultCameras();
};

#endif // GLOBALCAMERAMANAGER_H
