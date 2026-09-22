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

    // S1 残留收口：deviceName / suffix 的唯一来源是参数表（默认值在 init() 写入），不再保留无锁成员镜像。
    // 顺带删除死成员 m_connected：全仓检索确认只有声明、无任何使用（本节点不缓存连接态，
    // 一律问 CommunicationManager）。
    /// 最近一次回写是否成功：process() 以此上报执行结果，避免"PLC 没收到却显示成功"
    bool m_lastSendOk = false;

    QComboBox *m_deviceCombo = nullptr;
};
