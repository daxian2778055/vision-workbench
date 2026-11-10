#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

/// 界面日志总线：主窗口日志栏每写一条就发布一次，运行界面的「界面日志」控件订阅。
/// 手册「界面日志」= 实时显示软件的日志信息（即系统日志），因此发布点挂在
/// MainWindow::logMessage（日志栏的唯一写入点），而不是散落的 VFP_DEBUG。
class UiLogBus : public QObject
{
    Q_OBJECT
public:
    static UiLogBus *instance();

    /// 发布一条日志（同时保留最近 kRecentLimit 条供后订阅的控件回填）
    void publish(const QString &message);
    /// 最近日志（从旧到新，最多 maxLines 条）
    QStringList recent(int maxLines = 500) const;

signals:
    void messageLogged(const QString &message);

private:
    explicit UiLogBus(QObject *parent = nullptr);
    static constexpr int kRecentLimit = 500;
    QStringList m_recent;
};