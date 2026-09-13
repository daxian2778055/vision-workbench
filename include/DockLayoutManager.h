#pragma once

#include <QObject>
#include <QMainWindow>

class QDockWidget;

/// 管理 QDockWidget 布局和设计/运行模式切换
class DockLayoutManager : public QObject
{
    Q_OBJECT
public:
    explicit DockLayoutManager(QMainWindow *mainWindow, QObject *parent = nullptr);

    void registerDockWidget(QDockWidget *dock, Qt::DockWidgetArea area,
                            const QString &title = QString());

    void switchToDesignMode();
    void switchToRuntimeMode();

    bool isDesignMode() const { return m_designMode; }

signals:
    void modeChanged(bool designMode);

private:
    QMainWindow *m_mainWindow;
    bool m_designMode = true;
    QList<QDockWidget *> m_docks;
};
