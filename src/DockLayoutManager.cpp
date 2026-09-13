#include "DockLayoutManager.h"
#include "AppLog.h"
#include <QDockWidget>

DockLayoutManager::DockLayoutManager(QMainWindow *mainWindow, QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
{
}

void DockLayoutManager::registerDockWidget(QDockWidget *dock, Qt::DockWidgetArea area,
                                            const QString &title)
{
    if (!dock || !m_mainWindow) return;

    if (!title.isEmpty()) {
        dock->setWindowTitle(title);
    }
    m_mainWindow->addDockWidget(area, dock);
    m_docks.append(dock);
}

void DockLayoutManager::switchToDesignMode()
{
    m_designMode = true;
    for (QDockWidget *dock : m_docks) {
        if (dock) dock->show();
    }
    emit modeChanged(true);
}

void DockLayoutManager::switchToRuntimeMode()
{
    m_designMode = false;
    for (QDockWidget *dock : m_docks) {
        if (dock) {
            // Hide tool and param docks in runtime, keep image viewer
            QString objName = dock->objectName();
            if (objName == QStringLiteral("toolDock") ||
                objName == QStringLiteral("paramDock")) {
                dock->close();
            }
        }
    }
    emit modeChanged(false);
}
