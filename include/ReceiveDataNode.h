#pragma once

#include "HalconNode.h"
#include <QComboBox>

/// 接收数据算子 — 从 CommunicationManager 的指定设备接收数据，通过 String 端口输出
class ReceiveDataNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ReceiveDataNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool process() override;
    void setParam(const QString &name, const QVariant &value) override;
    QVariant getParam(const QString &name) const override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private slots:
    void onDataReceived(const QString &deviceName, const QByteArray &data);

private:
    QString m_deviceName;     /// 绑定的通信设备名
    QString m_filterPattern;  /// 可选过滤前缀（空则不过滤）
    bool m_connected = false;

    QComboBox *m_deviceCombo = nullptr;
    QComboBox *m_deviceSelect = nullptr; // alias for m_deviceCombo in param panel
};
