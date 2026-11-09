#pragma once

#include <QDialog>
#include <QJsonObject>

class QCheckBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;

/// FINS（Omron）设备配置对话框 — VM 4.4 B5-2：把已经实现的 FINS/TCP 与 FINS/UDP 参数
/// 与寄存器表接到通信管理界面上（B5-1 只能靠 JSON/代码建设备）。
///
/// 两个传输面共用同一套字段，键与 CommunicationManager 工厂分支读取的完全一致：
/// targetIp / targetPort / pollInterval / timeoutMs / autoReconnect / reconnectInterval /
/// localNode / remoteNode / registers[]。本对话框不做任何网络调用。
class FinsConfigDialog : public QDialog
{
    Q_OBJECT
public:
    /// @param deviceType "FINS TCP" 或 "FINS UDP"（只用于标题，不改变字段集合）
    /// @param initial    现有配置（空对象 = 全部取默认值）
    explicit FinsConfigDialog(const QString &deviceType, const QJsonObject &initial,
                              QWidget *parent = nullptr);

    /// 重灌配置：先回默认值再套 initial 中出现的键；寄存器表整表重建。
    void setConfig(const QJsonObject &initial);

    /// 按工厂期望的键回读；地址控件缺失的行整行跳过（不产出半截寄存器项）。
    QJsonObject config() const;

private slots:
    void onAccept();
    void onAddRegister();
    void onRemoveRegister();

private:
    void setupUI();
    /// 追加一行寄存器；reg 为空对象 = 默认行（DM/0/int16/Read/启用）
    void appendRegisterRow(const QJsonObject &reg);

    QLineEdit *m_targetIp = nullptr;
    QSpinBox *m_targetPort = nullptr;
    QSpinBox *m_pollInterval = nullptr;
    QSpinBox *m_timeoutMs = nullptr;
    QCheckBox *m_autoReconnect = nullptr;
    QSpinBox *m_reconnectInterval = nullptr;
    QSpinBox *m_localNode = nullptr;
    QSpinBox *m_remoteNode = nullptr;
    QTableWidget *m_registerTable = nullptr;
};
