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
    /// 执行一次回写；返回是否**真的投递成功**（设备存在且已连接）
    bool doSend();

    QString m_deviceName;      /// 绑定的通信设备名
    QString m_suffix;          /// 行尾追加（默认 \r\n）
    bool m_connected = false;
    /// 最近一次回写是否成功：process() 以此上报执行结果，避免"PLC 没收到却显示成功"
    bool m_lastSendOk = false;

    QComboBox *m_deviceCombo = nullptr;
};
