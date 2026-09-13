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
    virtual QVariant getParam(const QString &name) const override;
    virtual QJsonObject toJson() const override;
    virtual void fromJson(const QJsonObject &json) override;
    virtual QWidget *createParamPanel() override;
    virtual void updateParamPanel(QWidget *panel) override;
    virtual void displayImage() override;

    QStringList getAvailableConversions() const;

signals:
    void imageConverted(const HalconCpp::HImage &image);

private:
    ConversionType m_conversionType;
};
