#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QPoint>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QAbstractButton;
class QMouseEvent;

/// VM4.4「设备管理 → 新建设备」单窗口形态（2026-11-13，依用户截图 2 / 5）。
///
/// 官方点「+」只弹这一个窗口：协议类型 + 设备名称 + 通信参数 + 右下「创建」。
/// 本仓旧流程是「两个连续 QInputDialog（先类型后名称）+ 一个只含参数的配置窗」
/// 三段式，且按类型分 4 个对话框类；这里把前两段与常用参数收进同一窗口。
///
/// 旧的 CommDeviceConfigDialog / FinsConfigDialog / PlcConfigDialog 全部保留，
/// 仍供「配置」入口编辑高级参数（寄存器表等）与既有回归使用。
class VmDeviceCreateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VmDeviceCreateDialog(QWidget *parent = nullptr);

    /// 内部类型名（与 CommunicationManager::addDevice 的 type 一致）：
    /// TCP / 串口 / Modbus / PLC / UDP / FINS TCP / FINS UDP
    QString deviceType() const;
    QString deviceName() const;
    QJsonObject config() const;

    /// 只读测试钩子：让腿能断言「协议类型与设备名称在同一个窗口内」
    QWidget *protocolGroupForTest() const;
    QWidget *paramGroupForTest() const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onTypeChanged(int index);
    void updateCreateButton();

private:
    void buildUi();
    void applyTypeVisibility();
    QString ipString() const;

    QComboBox *m_typeCombo = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_ipEdits[4] = { nullptr, nullptr, nullptr, nullptr };
    QSpinBox *m_port = nullptr;
    QSpinBox *m_pollInterval = nullptr;
    QComboBox *m_commMode = nullptr;
    QComboBox *m_serialPort = nullptr;
    QSpinBox *m_baud = nullptr;
    QAbstractButton *m_dataUpload = nullptr;
    QAbstractButton *m_autoReconnect = nullptr;
    QAbstractButton *m_rxTerm = nullptr;
    QPushButton *m_createBtn = nullptr;
    QLabel *m_hint = nullptr;
    QWidget *m_protocolGroup = nullptr;
    QWidget *m_paramGroup = nullptr;

    /// 每行一个容器，按协议类型整行显隐（避免 QGridLayout 留下空行）
    QVector<QWidget *> m_paramRows;
    QWidget *m_titleBar = nullptr;
    QPoint m_dragPos;
    QString m_lastAutoName;   ///< 自动填过的默认名，用户手改后不再覆盖
};
