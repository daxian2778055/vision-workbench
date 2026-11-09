#pragma once

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
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
class CommMonitorDialog; // 收发监视页内嵌实例

/// 通信管理对话框 — 左侧导航 + 堆叠页：
/// 通信设备 / 控制器列表 / 接收事件 / 发送事件 / 心跳 / 收发监视
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
    void setupDeviceTab(QStackedWidget *stack);
    void setupControllerTab(QStackedWidget *stack);
    void setupReceiveEventTab(QStackedWidget *stack);
    void setupSendEventTab(QStackedWidget *stack);
    void setupHeartbeatTab(QStackedWidget *stack);
    void setupMonitorTab(QStackedWidget *stack);
    /// 控制器列表由设备数据派生（独立控制器模型尚未接入）；表未建时安全返回
    void refreshControllerTable();
    /// 合并式异步刷新设备表：deviceConnected/deviceDisconnected 只挂起一次重建，
    /// 等当前调用栈展开后再执行（避免在 openDevice/closeDevice 栈内同步重建导致的悬空指针）
    void scheduleDeviceTableRefresh();

    // 左侧导航 + 右侧页栈
    QListWidget *m_navList = nullptr;
    QStackedWidget *m_pageStack = nullptr;

    // Device tab
    QTableWidget *m_deviceTable;

    // 控制器列表页（由通信设备派生）
    QTableWidget *m_controllerTable = nullptr;

    // 收发监视页（内嵌 CommMonitorDialog）
    CommMonitorDialog *m_monitorDialog = nullptr;
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
