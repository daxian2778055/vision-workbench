#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QHBoxLayout>

class ModbusNode; // 前向声明

/// 通信管理对话框 — 集成设备管理 + 接收事件 + 发送事件 + 心跳
class CommunicationManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CommunicationManagerDialog(QWidget *parent = nullptr);
    ~CommunicationManagerDialog() override;

private slots:
    // 设备管理 Tab
    void onAddDevice();
    void onRemoveDevice();
    void onToggleConnection(int row);
    void refreshDeviceTable();
    void onConfigDevice();
    void onReadRegisters();

    // 接收事件 Tab
    void onAddReceiveEvent();
    void onRemoveReceiveEvent();
    void onEditReceiveEvent();
    void refreshReceiveEventTable();

    // 发送事件 Tab
    void onAddSendEvent();
    void onRemoveSendEvent();
    void onEditSendEvent();
    void refreshSendEventTable();

    // 心跳 Tab
    void onAddHeartbeat();
    void onRemoveHeartbeat();
    void refreshHeartbeatTable();

private:
    void setupUI();
    void setupDeviceTab(QTabWidget *tabs);
    void setupReceiveEventTab(QTabWidget *tabs);
    void setupSendEventTab(QTabWidget *tabs);
    void setupHeartbeatTab(QTabWidget *tabs);
    /// 合并式异步刷新设备表：deviceConnected/deviceDisconnected 只挂起一次重建，
    /// 等当前调用栈展开后再执行（避免在 openDevice/closeDevice 栈内同步重建导致的悬空指针）
    void scheduleDeviceTableRefresh();

    // Device tab
    QTableWidget *m_deviceTable;
    QPushButton *m_addDeviceBtn, *m_removeDeviceBtn, *m_configBtn, *m_readBtn;

    // Receive event tab
    QTableWidget *m_receiveEventTable;
    QPushButton *m_addReceiveEventBtn, *m_removeReceiveEventBtn, *m_editReceiveEventBtn;

    // Send event tab
    QTableWidget *m_sendEventTable;
    QPushButton *m_addSendEventBtn, *m_removeSendEventBtn, *m_editSendEventBtn;

    // Heartbeat tab
    QTableWidget *m_heartbeatTable;
    QPushButton *m_addHeartbeatBtn, *m_removeHeartbeatBtn;

    QPushButton *m_closeBtn;

    /// 设备表异步刷新挂起标志：把连续多个状态信号合并为一次重建
    bool m_deviceTableRefreshPending = false;
};
