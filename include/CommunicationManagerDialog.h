#pragma once

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>

class ModbusNode; // 前向声明
class GlobalTriggerDialog; // 触发配置页内嵌（全局触发）

/// 通信管理对话框 — 左侧导航 + 堆叠页（VM 5.2 官方六项）：
/// 设备管理（含控制器列表）/ 接收事件 / 发送事件 / 触发配置（全局触发）/
/// 响应配置（方案加载·流程控制·相机响应）/ 心跳管理。
/// 收发监视不在导航内（官方为独立窗口，主菜单入口仍可用）。
class CommunicationManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CommunicationManagerDialog(QWidget *parent = nullptr);
    ~CommunicationManagerDialog() override;

    /// 响应配置页「应用」：把控件值写成 CommunicationManager::responseConfig()（随方案保存）。
    /// 修改任一控件自动调用；回归测试按名直接驱动。
    void applyResponseConfig();

private slots:
    // 设备管理 Tab
    void onAddDevice();
    void onRemoveDevice();
    void onToggleConnection(int row);
    void refreshDeviceTable();
    void onConfigDevice();
    void onReadRegisters();

    // VM 5.2.1 右键菜单（设备/控制器/接收事件/发送事件四张表：重命名 + 删除）
    void onDeviceContextMenu(const QPoint &pos);
    void onControllerContextMenu(const QPoint &pos);
    void onReceiveEventContextMenu(const QPoint &pos);
    void onSendEventContextMenu(const QPoint &pos);
    /// 无模态应用入口（供右键菜单与回归测试直接驱动）；失败弹提示并返回 false
    bool applyDeviceRename(const QString &oldName, const QString &newName);
    bool applyReceiveEventRename(const QString &oldId, const QString &newId);
    bool applySendEventRename(const QString &oldId, const QString &newId);

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
    /// 控制器列表并进设备管理页（官方：控制器列表在设备管理内）；由设备数据派生
    void buildControllerSection(QVBoxLayout *into);
    void setupReceiveEventTab(QStackedWidget *stack);
    void setupSendEventTab(QStackedWidget *stack);
    /// 触发配置页：内嵌既有 GlobalTriggerDialog（全局触发）
    void setupTriggerTab(QStackedWidget *stack);
    /// 响应配置页（VM 5.2.5）：方案加载 / 流程控制 / 相机响应
    void setupResponseTab(QStackedWidget *stack);
    void setupHeartbeatTab(QStackedWidget *stack);
    /// 控制器列表由设备数据派生（独立控制器模型尚未接入）；表未建时安全返回
    void refreshControllerTable();
    /// 响应配置页设备下拉按当前设备表重建（保留已选设备）
    void refreshResponseDeviceCombos();
    /// 合并式异步刷新设备表：deviceConnected/deviceDisconnected 只挂起一次重建，
    /// 等当前调用栈展开后再执行（避免在 openDevice/closeDevice 栈内同步重建导致的悬空指针）
    void scheduleDeviceTableRefresh();
    /// 右键菜单公共件：重命名/删除两个动作（objectName 供测试定位）；
    /// 返回 0=未选/取消，1=重命名，2=删除（不返回 QAction 指针：菜单出栈即销毁）
    int execRowContextMenu(QTableWidget *table, const QPoint &pos);
    /// 弹输入框收集新名并应用；取消返回 false
    bool promptAndApplyDeviceRename(const QString &oldName);
    bool promptAndApplyReceiveEventRename(const QString &oldId);
    bool promptAndApplySendEventRename(const QString &oldId);
    /// 在设备表中按名选中行（控制器页的删除/重命名复用设备页入口）
    void selectDeviceRowByName(const QString &name);

    // 左侧导航 + 右侧页栈
    QListWidget *m_navList = nullptr;
    QStackedWidget *m_pageStack = nullptr;

    // Device tab
    QTableWidget *m_deviceTable;

    // 控制器列表（并进设备管理页；由通信设备派生）
    QTableWidget *m_controllerTable = nullptr;

    // 触发配置页（内嵌全局触发对话框）
    GlobalTriggerDialog *m_triggerDialog = nullptr;

    // 响应配置页（VM 5.2.5）
    QCheckBox *m_respSchemeEnable = nullptr;
    QComboBox *m_respSchemeDevice = nullptr;
    QSpinBox *m_respSchemeDelay = nullptr;
    QComboBox *m_respSchemeTerm = nullptr;
    QLineEdit *m_respSchemeContent = nullptr;
    QCheckBox *m_respFlowEnable = nullptr;
    QComboBox *m_respFlowDevice = nullptr;
    QComboBox *m_respFlowTerm = nullptr;
    QComboBox *m_respFlowSep = nullptr;
    QLineEdit *m_respFlowIdle = nullptr;
    QLineEdit *m_respFlowBusy = nullptr;
    QCheckBox *m_respCameraEnable = nullptr;
    QComboBox *m_respCameraDevice = nullptr;
    QComboBox *m_respCameraSep = nullptr;
    QLineEdit *m_respCameraOn = nullptr;
    QLineEdit *m_respCameraOff = nullptr;

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
