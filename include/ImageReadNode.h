#pragma once

#include "HalconNode.h"

class ImageReadNode : public HalconNode
{
    Q_OBJECT

public:
    ImageReadNode(QObject *parent = nullptr);
    ~ImageReadNode();

    virtual void init() override;
    virtual void run(bool autoSwitch = true) override;
    virtual void setParam(const QString &name, const QVariant &value) override;
    virtual QVariant getParam(const QString &name) const override;
    virtual QJsonObject toJson() const override;
    virtual void fromJson(const QJsonObject &json) override;
    virtual bool process() override;
    virtual QWidget *createParamPanel() override;
    virtual void updateParamPanel(QWidget *panel) override;
    virtual void displayImage() override;

signals:
    void imageRead(const HImage &image);
    void thumbnailUpdated();

public:
    enum class Mode {
        SingleImage,
        MultiImage
    };

private:
    QString m_filePath;
    bool m_isDirectory;
    QStringList m_imageFiles;
    int m_currentImageIndex;
    int m_displayImageIndex; // 当前显示的图像索引
    bool m_mono8Mode;
    bool m_autoSwitch;
    Mode m_mode;
    int m_imageWidth;
    int m_imageHeight;
    QString m_pixelFormat;
    QString m_imageName;
    bool m_thumbnailNeedsUpdate;
    void updateImageFiles();
    void selectMultipleFiles();
    
public:
    void updateThumbnails(QHBoxLayout *thumbnailLayout);
};
