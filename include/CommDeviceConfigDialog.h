#pragma once

#include <QDialog>
#include <QJsonObject>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QFormLayout;

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
    /// 组帧两行（TCP/串口/UDP 通用）：治理粘包/半包；0/空 = 不启用
    void addFrameRows(QFormLayout *form, const QJsonObject &initial);

    QString m_type;
    QJsonObject m_config;

    QComboBox *m_tcpModeCombo = nullptr;
    QLineEdit *m_tcpIpEdit = nullptr;
    QSpinBox *m_tcpPortSpin = nullptr;
    /// U-202 收紧：空＝未配置＝拒启动；显式 IP 字面量原样绑定。两处配置页必须能填。
    QLineEdit *m_tcpBindEdit = nullptr;

    QComboBox *m_serialPortCombo = nullptr;
    QComboBox *m_serialBaudCombo = nullptr;
    QComboBox *m_dataBitsCombo = nullptr;
    QComboBox *m_stopBitsCombo = nullptr;
    QComboBox *m_parityCombo = nullptr;

    QSpinBox *m_udpLocalPortSpin = nullptr;
    QLineEdit *m_udpRemoteIpEdit = nullptr;
    QSpinBox *m_udpRemotePortSpin = nullptr;
    QLineEdit *m_udpBindEdit = nullptr;

    // 组帧（三类通用）
    QSpinBox *m_frameTimeoutSpin = nullptr;
    QLineEdit *m_frameTermEdit = nullptr;
    // 官方「完全一致的参数」（手册 5.2.1）：数据上传 / 自动重连 / 接收结束符(开关+\r/\n/\r\n/自定义)
    QCheckBox *m_dataUploadCheck = nullptr;
    QCheckBox *m_autoReconnectCheck = nullptr;
    QCheckBox *m_rxTermCheck = nullptr;
    QComboBox *m_rxTermCombo = nullptr;
};
