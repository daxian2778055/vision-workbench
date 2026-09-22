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
    // S1 残留收口：deviceName / filterPattern 的唯一来源是参数表（默认值在 init() 写入），
    // 不再保留无锁成员镜像——onDataReceived 在主线程读它们，而参数可能被执行线程写
    // （参数引用写回 setParam），原先属无保护跨线程读；参数表自带锁。
    // 顺带删除两个死成员：m_connected（cpp 中 0 处使用，本节点不缓存连接态）与
    // m_deviceSelect（全仓仅此一处声明，注释所称的"alias"从未落地）。
    QComboBox *m_deviceCombo = nullptr;
};
