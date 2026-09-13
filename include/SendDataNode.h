#pragma once

#include "HalconNode.h"
#include <QComboBox>

/// 发送数据算子 — 将输入数据通过 CommunicationManager 的指定设备发送出去
class SendDataNode : public HalconNode
{
    Q_OBJECT
public:
    explicit SendDataNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool process() override;
    void setParam(const QString &name, const QVariant &value) override;
    QVariant getParam(const QString &name) const override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private:
    QString m_deviceName;      /// 绑定的通信设备名
    QString m_suffix;          /// 行尾追加（默认 \r\n）
    bool m_connected = false;

    QComboBox *m_deviceCombo = nullptr;
};
