#pragma once

#include <QDialog>
#include <QJsonObject>

class QComboBox;
class QLineEdit;
class QSpinBox;

/// 通信设备配置对话框（单页表单，替代"连续弹窗"）：
/// 支持 TCP（客户端/服务端）、串口、UDP 三类；
/// Modbus / PLC 仍使用各自带寄存器表格的专用配置对话框。
class CommDeviceConfigDialog : public QDialog
{
    Q_OBJECT
public:
    /// type: "TCP" / "串口"(或 "Serial") / "UDP"；initial 为现有配置（可为空）
    CommDeviceConfigDialog(const QString &type, const QJsonObject &initial,
                           QWidget *parent = nullptr);

    /// 确定后的配置（可直接交给 CommunicationManager::addDevice / 节点 setParam）
    QJsonObject config() const { return m_config; }

private slots:
    void onAccept();

private:
    void buildTcpForm(const QJsonObject &initial);
    void buildSerialForm(const QJsonObject &initial);
    void buildUdpForm(const QJsonObject &initial);

    QString m_type;
    QJsonObject m_config;

    QComboBox *m_tcpModeCombo = nullptr;
    QLineEdit *m_tcpIpEdit = nullptr;
    QSpinBox *m_tcpPortSpin = nullptr;

    QComboBox *m_serialPortCombo = nullptr;
    QComboBox *m_serialBaudCombo = nullptr;

    QSpinBox *m_udpLocalPortSpin = nullptr;
    QLineEdit *m_udpRemoteIpEdit = nullptr;
    QSpinBox *m_udpRemotePortSpin = nullptr;
};
