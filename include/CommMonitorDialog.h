#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QHash>

/// 通讯数据监视窗口 — 实时查看各设备的收发字节与数据流
/// 对标 VM4.4 的"通讯监视"：设备列表 + 收发数据日志（十六进制/ASCII）
class CommMonitorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CommMonitorDialog(QWidget *parent = nullptr);

private slots:
    void onDataReceived(const QString &deviceName, const QByteArray &data);
    void onDataSent(const QString &deviceName, const QByteArray &data);
    void onDeviceConnected(const QString &name);
    void onDeviceDisconnected(const QString &name);
    void onDeviceAdded(const QString &name, const QString &type);
    void onDeviceRemoved(const QString &name);
    void onDeviceSelectionChanged();
    void onClearLog();
    void onClearStats();

private:
    void setupUI();
    void ensureDeviceRow(const QString &name);
    void appendLog(const QString &deviceName, const QString &direction,
                   const QByteArray &data);
    void refreshDetailView();
    QString formatHex(const QByteArray &data) const;
    QString formatAscii(const QByteArray &data) const;
    void updateStatus();

    struct DeviceStats {
        QString type;
        bool connected = false;
        quint64 bytesReceived = 0;
        quint64 bytesSent = 0;
        quint64 framesReceived = 0;
        quint64 framesSent = 0;
        QStringList logLines; // 每个元素一行已格式化的记录
        int logLineCount = 0;
    };
    QHash<QString, DeviceStats> m_stats;
    QStringList m_deviceOrder;

    QTableWidget *m_deviceTable = nullptr;
    QComboBox *m_viewModeCombo = nullptr;   ///< 显示模式：十六进制 / ASCII / 混合
    QPlainTextEdit *m_logView = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_clearLogBtn = nullptr;
    QPushButton *m_clearStatsBtn = nullptr;

    static constexpr int kMaxLogLines = 2000;
};
