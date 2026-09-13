#pragma once

#include "HalconNode.h"
#include <QMutex>

class HalconImageSourceNode : public HalconNode
{
    Q_OBJECT

public:
    enum SourceType {
        LOCAL_FILE,
        CAMERA
    };



    enum TriggerMode {
        SOFTWARE_TRIGGER,
        HARDWARE_TRIGGER,
        FREE_RUN
    };

    explicit HalconImageSourceNode(QObject *parent = nullptr);
    ~HalconImageSourceNode() override;

    void init() override;
    void run(bool autoSwitch = true) override;
    bool process() override;

    void setParam(const QString &name, const QVariant &value) override;
    QVariant getParam(const QString &name) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    void displayImage() override;

    QStringList getAvailableCameras();
    QString getSelectedCamera() const;
    void setSelectedCamera(const QString &cameraName);

    int getImageWidth() const { return m_imageWidth; }
    int getImageHeight() const { return m_imageHeight; }
    QString getPixelFormat() const { return m_pixelFormat; }
    QString getCameraName() const { return m_cameraName; }

    bool isCameraOpen() const;
    void setFilePath(const QString &path);
    QString getFilePath() const;

signals:
    void imageAcquired(const HalconCpp::HImage &image);
    void cameraStatusChanged(bool opened);
    /// 参数已通过后台线程成功应用到相机，UI 可清除脏标记
    void paramsApplied();

public:
    void applyCameraParams();
    void readCameraParams();
    bool openCameraInThread();
    void applyCameraParamsInThread();
    QStringList detectCamerasInThread();
    
private:
    bool openCamera();
    void closeCamera();
    bool grabImage();
    bool loadImageFromFile();
    void updateOutputInfo();
    void updateImageFiles();

    SourceType m_sourceType;
    bool m_isDirectory;
    QString m_filePath;
    QStringList m_imageFiles;
    int m_currentImageIndex;

    TriggerMode m_triggerMode;
    double m_exposureTime;
    double m_gain;
    double m_frameRate;
    QString m_cameraName;
    int m_cameraHandle;
    bool m_cameraOpened;

    HalconCpp::HImage m_outputImage;
    int m_imageWidth;
    int m_imageHeight;
    QString m_pixelFormat;

    // 输入参数
    bool m_inputTrigger;
    double m_inputExposure;
    double m_inputGain;
    

    
    // 线程安全
    mutable QMutex m_mutex;
    
    // 防重复检测
    bool m_isDetectingCameras;
};
