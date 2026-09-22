#ifndef MVSIMAGESOURCENODE_H
#define MVSIMAGESOURCENODE_H

// 工业相机取图：使用真实海康 MVS SDK（MvCameraControl.h + CameraParams.h）
#include "VisionIntegrationPolicy.h"
#include "HalconNode.h"
#include <QStringList>
#include <QMutex>
#include <QRecursiveMutex>

class QComboBox;

// MVS SDK 真实头文件（CMake 已添加 D:/MVS/Development/Includes 为包含路径）
#include "MvCameraControl.h"

class MvsImageSourceNode : public HalconNode
{
    Q_OBJECT

public:
    enum SourceType {
        LOCAL_FILE = 0,
        CAMERA = 1
    };

    enum TriggerMode {
        SOFTWARE_TRIGGER = 0,
        HARDWARE_TRIGGER = 1,
        FREE_RUN = 2
    };

    explicit MvsImageSourceNode(QObject *parent = nullptr);
    ~MvsImageSourceNode();

    // --- 基类虚函数重写 ---
    void init() override;
    void run(bool autoSwitch = true) override;
    bool process() override;
    void drawResult() override;
    void setParam(const QString &key, const QVariant &value) override;
    QVariant getParam(const QString &key) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    void displayImage() override;

    // 名/类型/执行（隐藏基类同名非虚函数，用于返回特定信息）
    QString name() const;
    NodeBase::NodeType type() const;
    bool execute();

    /// 工业相机图像源：硬触发模式下作为流程的触发帧门控
    bool isCameraSource() const override { return true; }

    // 相机相关方法
    QStringList detectCamerasInThread();
    QString getSelectedCamera();
    void setSelectedCamera(const QString &camera);
    bool isCameraOpen();
    void openCameraInThread();
    void applyCameraParams();
    void applyCameraParamsInThread();
    /// 按本节点触发模式配置相机寄存器（TriggerMode/TriggerSource），本地相机打开后调用
    void applyTriggerConfig();
    void readGlobalCamerasInThread();

    // 文件路径设置
    void setFilePath(const QString &path);
    QString getFilePath() const;

    // 全局相机
    QStringList getAvailableCameras();
    QStringList getGlobalCameras();
    QString getGlobalCameraName();
    void setGlobalCameraName(const QString &name);
    Q_INVOKABLE void updateGlobalCameras(const QStringList &cameraNames);

    /// 刷新像素格式控件的可用状态（连续模式/运行中时禁用）
    void refreshPixelFormatEnabled();

    /// 关闭相机（同时释放 MVS SDK 资源）
    void closeCamera();

signals:
    void imageAcquired(); // 移除了 Halcon 图像参数
    void cameraStatusChanged(bool opened);
    void globalCamerasUpdated(const QStringList &cameraNames);
    void paramChanged(const QString &name, const QVariant &value);
    void logMessage(const QString &message);
    /// 参数已通过后台线程成功应用到相机，UI 可清除脏标记
    void paramsApplied();
    /// 参数应用失败，附带失败原因
    void paramsApplyFailed(const QString &reason);

private:
    // 关于"单源收口"（S1 残留专项）：本节点**刻意不做**参数表单源化，理由已核实（非省略）：
    //  · 全部成员由本类的 QRecursiveMutex m_mutex 保护——setParam/getParam/toJson/fromJson 及内部
    //    方法一律先加锁（本文件 34 处锁操作；同族 HalconImageSourceNode 23 处）→ 不存在已收口的
    //    11 类"无锁成员镜像"那种跨线程竞态，本次专项要消灭的隐患在这里本来就不存在；
    //  · 这些成员同时是**硬件寄存器缓存**：readCameraParams() 把设备实际值写回 exposure/gain/
    //    frameRate/pixelFormat，applyCameraParamsInThread() 又据此写设备，openCameraInThread()/
    //    applyTriggerConfig()/grabImage() 直接读成员。改成"参数表唯一来源"等于重写相机 SDK
    //    交互路径，而 CI 无相机、无法验证 → 收益为负、风险不可测；
    //  · 若确需统一，应作为独立立项（要求硬件在环测试），不要顺手改。
    // 图像源参数
    SourceType m_sourceType;
    QString m_filePath;
    bool m_isDirectory;
    int m_currentImageIndex;
    QStringList m_imageFiles;

    // 相机参数
    QString m_cameraName;
    QString m_globalCameraName; // 全局相机名称（Camera1～Camera8 等）
    /// 本算子实例在 GlobalCameraManager 中的占用 ID（支持多实例共用一个全局相机名）
    QString m_cameraConsumerId;
    TriggerMode m_triggerMode;
    double m_exposureTime;
    double m_gain;
    double m_frameRate;
    QString m_pixelFormat; // 像素格式（输入参数）

    // 相机状态
    bool m_cameraOpened;
    void* m_hCamera;
    /// 单帧取图缓冲大小（按相机 PayloadSize 动态分配，避免高分辨率相机缓冲不足）
    unsigned int m_payloadSize = 10 * 1024 * 1024;
    bool m_isDetectingCameras;
    /// 连续取图失败次数（用于自动重连判断）
    int m_consecutiveGrabFailures = 0;
    /// 上次自动重连时间戳（节流，避免频繁重连）
    qint64 m_lastReconnectMs = 0;

    // 图像信息
    int m_imageWidth; // 图像宽度（输入参数）
    int m_imageHeight; // 图像高度（输入参数）
    HalconCpp::HImage m_outputImage;
    
    // 输出参数
    int m_outputImageWidth; // 输出图像宽度
    int m_outputImageHeight; // 输出图像高度
    QString m_outputPixelFormat; // 输出像素格式

    // 输入参数
    bool m_inputTrigger;
    double m_inputExposure;
    double m_inputGain;

    // 线程安全 — 用可重入互斥锁，防止同一线程嵌套持锁导致死锁
    mutable QRecursiveMutex m_mutex;

    // 像素格式控件指针（仅UI线程访问）
    QComboBox *m_pixelFormatCombo = nullptr;

    // 刷新像素格式下拉列表（基于当前绑定的全局相机）
    void refreshPixelFormatCombo();

    // 私有方法
    bool openCamera();
    bool grabImage();
    bool loadImageFromFile();
    void updateOutputInfo();
    void updateImageFiles();
    void readCameraParams();
    /// 取图失败时尝试自动重连（节流 3s，最多连续重连 3 次）
    bool tryAutoReconnect();
};

#endif // MVSIMAGESOURCENODE_H
