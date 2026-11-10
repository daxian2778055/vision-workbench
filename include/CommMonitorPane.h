#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;

/// 通信管理主窗口内嵌的收发数据区（官方 5.2 设备管理页右下形态）：
/// Tab「接收数据 / 发送数据」+ 大数据区 +「16 进制显示」+「清空」+ 发送输入与按钮。
///
/// 与独立的 CommMonitorDialog（带设备统计的增强视图）职责不同：这里只做官方形态的
/// 原始收发数据查看与手动发送，不重复统计表那套逻辑。两者订阅同一组信号，各自独立。
class CommMonitorPane : public QWidget
{
    Q_OBJECT
public:
    explicit CommMonitorPane(QWidget *parent = nullptr);

    /// 手动发送的目标设备，由通信管理对话框在选中设备变化时设置
    void setTargetDevice(const QString &device);
    QString targetDevice() const { return m_device; }

    /// 仅用于回归测试与诊断：当前两页的可见文本
    QString receiveText() const;
    QString sendText() const;

public slots:
    /// 公开槽：既是信号连接点，也便于测试直接驱动（无需真 socket）
    void onDataReceived(const QString &deviceName, const QByteArray &data);
    void onDataSent(const QString &deviceName, const QByteArray &data);

private slots:
    void flushRefresh();
    void onClear();
    void onSend();
    void onHexToggled(bool on);

private:
    struct Entry {
        QString device;
        QByteArray data;
    };

    static QString formatLine(const QString &device, const QByteArray &data, bool hex);
    void push(QVector<Entry> &list, const QString &device, const QByteArray &data);
    void render(QPlainTextEdit *view, const QVector<Entry> &list, bool hex) const;

    QTabWidget *m_tabs = nullptr;
    QPlainTextEdit *m_rxView = nullptr;
    QPlainTextEdit *m_txView = nullptr;
    QCheckBox *m_hexCheck = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QLineEdit *m_sendEdit = nullptr;
    QLabel *m_targetLabel = nullptr;
    QTimer *m_throttle = nullptr;

    QVector<Entry> m_rx;
    QVector<Entry> m_tx;
    QString m_device;
    bool m_dirty = false;

    static constexpr int kMaxLines = 2000;
    static constexpr int kThrottleMs = 200;
};
