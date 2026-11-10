#pragma once

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QPushButton>
#include <QJsonObject>

class PlcCommNode; /// 前向声明，用于实时数据通信
class RegisterSpaceBar; /// VM 5.2.1 寄存器工具条（首地址/个数/同步/发送测试/最大化/空白行/清空）

/// PLC 设备配置对话框 — 品牌、连接参数、自动重连、轮询、寄存器表格
class PlcConfigDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PlcConfigDialog(const QString &title,
                             PlcCommNode *node = nullptr,
                             QWidget *parent = nullptr);

    void setConfig(const QJsonObject &config);
    QJsonObject config() const;

    /// 更新指定寄存器的显示值
    void updateRegisterValue(int address, double value, const QString &displayText);

private slots:
    void onAddRegister();
    void onRemoveRegister();
    void onAccept();
    void onCellDoubleClicked(int row, int column);
    void onWriteValueToRegister(int row);
    void onToggleConnection();

private:
    void setupUI();
    QJsonObject buildConfigFromForm() const;
    void loadConfigToForm(const QJsonObject &config);
    /// 空白地址行（工具条生成、仅地址列）就地补配置控件；已配置行不动
    void configureRow(int row);
    int registerAddressForRow(int row) const;
    void connectToNodeLiveUpdates();
    void refreshToggleSwitch();

    QComboBox *m_brandCombo = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QSpinBox *m_slaveAddress = nullptr;
    QCheckBox *m_autoReconnect = nullptr;
    /// S4「回写三段确认」的开关（默认关闭）：①发出写 → ②从站回执 OK → ③**回读同地址比对**，
    /// 不一致才经 communicationError 报警（与 ModbusConfigDialog 同款，见其头文件注释）。
    QCheckBox *m_writeVerify = nullptr;
    QSpinBox *m_reconnectInterval = nullptr;
    QSpinBox *m_pollInterval = nullptr;
    QTableWidget *m_registerTable = nullptr;
    RegisterSpaceBar *m_spaceBar = nullptr;
    QPushButton *m_writeBtn = nullptr;
    QPushButton *m_toggleSwitch = nullptr;

    PlcCommNode *m_plcNode = nullptr; ///< 用于实时显示和写入
};
