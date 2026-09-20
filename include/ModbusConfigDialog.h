#pragma once

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QPushButton>
#include <QJsonObject>

class ModbusNode; /// 前向声明，用于实时数据通信

/// Modbus 设备配置对话框 — 完整配置（角色切换、自动重连、轮询周期、寄存器表格、实时值、访问模式）
class ModbusConfigDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ModbusConfigDialog(const QString &title,
                                ModbusNode *node = nullptr, ///< 关联的 Modbus 节点（用于实时数据）
                                QWidget *parent = nullptr);

    void setConfig(const QJsonObject &config);
    QJsonObject config() const;

    /// 更新指定寄存器的显示值（由外部调用，例如来自 ModbusNode 的 registerCurrentValueChanged 信号）
    void updateRegisterValue(int address, double value, const QString &displayText);

private slots:
    void onAddRegister();
    void onRemoveRegister();
    void onAccept();
    void onCellDoubleClicked(int row, int column);
    void onWriteValueToRegister(int row);
    void onRoleChanged(int index);

private:
    void setupUI();
    QJsonObject buildConfigFromForm() const;
    void loadConfigToForm(const QJsonObject &config);
    int registerAddressForRow(int row) const;
    void connectToNodeLiveUpdates();
    /// 切换角色后刷新控件可见性（服务器模式隐藏主机地址）
    void refreshRoleUi();
    /// 按连接类型（TCP/RTU）切换"主机地址/端口"与"串口参数"的可见性
    void refreshConnTypeUi();
    /// 刷新连接切换开关文字/样式
    void refreshToggleSwitch();

    QComboBox *m_roleCombo = nullptr;      ///< 客户端 / 服务器
    QComboBox *m_connType = nullptr;
    QLineEdit *m_host = nullptr;
    QLabel *m_hostLabel = nullptr;
    QSpinBox *m_port = nullptr;
    QLabel *m_portLabel = nullptr;
    // RTU（RS485）串口参数：连接类型选 RTU 时显示（历史缺陷：有 RTU 选项但无任何串口字段）
    QComboBox *m_serialPortName = nullptr;
    QLabel *m_serialPortLabel = nullptr;
    QComboBox *m_serialBaudRate = nullptr;
    QLabel *m_serialBaudLabel = nullptr;
    // 数据位/停止位/校验：此前 UI 不产出、工厂不投递，节点虽读这几个键却永远吃默认 8/1/None，
    // 8E1/8O1（大量 Modbus RTU 设备的出厂设置）永远连不上且查不出原因。
    // 注意 stopBits 的取值直通 QSerialPort::StopBits 枚举（1=OneStop / 2=OneAndHalfStop / 3=TwoStop），
    // 与 SerialCommNode 的 1/2/3 自定义约定不同——QModbus 直接把该值当枚举用。
    QComboBox *m_serialDataBits = nullptr;
    QLabel *m_serialDataBitsLabel = nullptr;
    QComboBox *m_serialStopBits = nullptr;
    QLabel *m_serialStopBitsLabel = nullptr;
    QComboBox *m_serialParity = nullptr;
    QLabel *m_serialParityLabel = nullptr;
    QSpinBox *m_slaveAddress = nullptr;
    QCheckBox *m_autoReconnect = nullptr;
    QSpinBox *m_reconnectInterval = nullptr;
    QSpinBox *m_pollInterval = nullptr;
    QTableWidget *m_registerTable = nullptr;
    QPushButton *m_writeBtn = nullptr;
    QPushButton *m_toggleSwitch = nullptr; ///< 连接切换开关（服务器：开启服务器 / 客户端：连接服务器）

    ModbusNode *m_modbusNode = nullptr; ///< 用于实时显示和写入
};
