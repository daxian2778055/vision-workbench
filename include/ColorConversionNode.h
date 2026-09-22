#pragma once

#include "HalconNode.h"

class ColorConversionNode : public HalconNode
{
    Q_OBJECT

public:
    enum ConversionType {
        RGB_TO_GRAY = 0,
        RGB_TO_HSV,
        RGB_TO_HSL,
        GRAY_TO_RGB,
        HSV_TO_RGB,
        HSL_TO_RGB
    };

    ColorConversionNode(QObject *parent = nullptr);
    ~ColorConversionNode();

    virtual void init() override;
    virtual bool process() override;
    bool execute();
    QString name() const;
    NodeBase::NodeType type() const;
    virtual void setParam(const QString &name, const QVariant &value) override;
    // 原 getParam 重写已删除（旁路：对 conversionType 返回成员、对其它键吞掉基类结果）⇒ 直接走基类
    virtual QJsonObject toJson() const override;
    virtual void fromJson(const QJsonObject &json) override;
    virtual QWidget *createParamPanel() override;
    virtual void updateParamPanel(QWidget *panel) override;
    virtual void displayImage() override;

    QStringList getAvailableConversions() const;

signals:
    void imageConverted(const HalconCpp::HImage &image);

private:
    // S1 残留收口（第二批补漏 · 第 12 类）：conversionType 的唯一来源是参数表（默认值在 init() 写入），
    // 不再保留无锁成员镜像——process() 在执行线程读它，界面线程会写。
    // 同时纠正两处旁路（它们让"参数表 = 唯一来源"对本类失效）：
    //  · setParam 只处理 conversionType、**完全不调基类** ⇒ 其它键的写入被静默丢弃；
    //  · getParam 对其它键返回空 QVariant ⇒ **吞掉**基类结果。
    // 两处已改为直接走基类（见 .cpp / 提交说明）。
};
