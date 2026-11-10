#pragma once

#include <QWidget>

class QTableWidget;
class QSpinBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QDialog;
class QVBoxLayout;

/// VM 5.2.1 寄存器地址空间工具条（Modbus / PLC 共用）：
/// 首地址 / 寄存器个数 / 添加地址 / 数据同步 / 发送测试 / 最大化(Esc 退出) /
/// 隐藏空白行 / 清空。空白行判定 = 「数据类型」列没有配置控件（仅有地址的行）。
/// 工具条只操作表格与地址空间容器；真正的读写由宿主对话框接信号完成。
class RegisterSpaceBar : public QWidget
{
    Q_OBJECT
public:
    /// table = 寄存器表；space = 可最大化的地址空间容器（通常为所属分组框）；
    /// spaceHome = space 原来的父控件（最大化后还原用，必须非空）
    RegisterSpaceBar(QTableWidget *table, QWidget *space, QWidget *spaceHome,
                     QWidget *parent = nullptr);

    // ---- 供回归测试直接驱动（按钮槽同样走这些入口）----
    /// 按首地址/个数补齐「只有地址」的空白行（已存在的地址不重复加），返回新增行数
    int addAddressesInSpace();
    void clearRows();
    /// 隐藏/显示空白行；返回因此被隐藏的行数（hidden=true 时）
    int setBlankRowsHidden(bool hidden);
    bool isMaximized() const { return m_maxWindow != nullptr; }
    void setStatus(const QString &text);
    /// 发送测试地址下拉按当前表格重建（保留已选地址）
    void refreshAddressChoices();

signals:
    void syncRequested();
    void testSendRequested(const QString &address, const QString &value);
    void rowsChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void maximizeSpace();
    void restoreSpace();

    QTableWidget *m_table;
    QWidget *m_space;
    QWidget *m_spaceHome;
    QVBoxLayout *m_homeLayout = nullptr;
    QSpinBox *m_startSpin = nullptr;
    QSpinBox *m_countSpin = nullptr;
    QComboBox *m_testAddrCombo = nullptr;
    QLineEdit *m_testValueEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    /// 最大化：主窗口全屏 + 仅保留地址空间可见（不新开顶层窗——真桌面下
    /// 新建全屏窗口在窗口模态父窗旁会卡住事件循环）
    QWidget *m_maxWindow = nullptr;
    QList<QPair<QWidget *, bool>> m_savedVisibility;
    bool m_blankHidden = false;
};
