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
    // S1 残留收口：4 个**真参数**（filePath / mono8Mode / autoSwitch / mode）的唯一来源改为参数表
    // （默认值在 init() 写入），不再保留无锁成员镜像——run() 在执行线程读它们、界面线程会写，
    // 原先属无保护跨线程读（filePath 是 QString，撕裂代价最高）。
    // 以下成员**刻意保留**，它们不是"参数镜像"，属另一类状态：
    //  · m_isDirectory / m_imageFiles —— 由 filePath 派生的缓存（在 setParam 里重建）；
    //  · m_currentImageIndex / m_displayImageIndex —— 运行期轮换与显示状态（不入参数表，
    //    否则会被 toJson 持久化，重开方案后会"接着轮换"，属静默语义变化）；
    //  · m_imageWidth/Height、m_pixelFormat、m_imageName —— 每轮执行产出的结果（经 getParam 的
    //    合成分支回读，本来就不在参数表里）；
    //  · m_thumbnailNeedsUpdate —— 界面缩略图刷新标志。
    bool m_isDirectory;
    QStringList m_imageFiles;
    int m_currentImageIndex;
    int m_displayImageIndex; // 当前显示的图像索引
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
