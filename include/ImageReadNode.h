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
    /// 当前只有 0/1 个文件时，输出是路径的确定性函数（同一路径 → 同一图像），可被局部执行复用；
    /// 多个文件时会随轮次切换取不同图，必须重跑。
    virtual bool reusesCachedOutput() const override;

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
