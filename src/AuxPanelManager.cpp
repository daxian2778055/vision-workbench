#include "AuxPanelManager.h"

#include "ResultTablePanel.h"
#include "VariablePanel.h"
#include "PerformancePanel.h"
#include "OutputDataViewer.h"
#include "PanelVisibilityStore.h"

#include <QMainWindow>
#include <QDockWidget>
#include <QDialog>

AuxPanelManager::AuxPanelManager(QMainWindow *mainWindow, QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
{
}

void AuxPanelManager::open(const QString &key)
{
    if (key == QStringLiteral("resultTable")) {
        createResultTableIfNeeded();
        m_resultTableDock->show();
        m_resultTableDock->raise();
        return;
    }
    if (key == QStringLiteral("performance")) {
        createPerformanceIfNeeded();
        m_performanceDock->show();
        m_performanceDock->raise();
        return;
    }
    if (key == QStringLiteral("outputData")) {
        createOutputDataIfNeeded();
        m_outputViewerDock->show();
        m_outputViewerDock->raise();
        return;
    }
    if (key == QStringLiteral("variable")) {
        createVariableIfNeeded();
        m_variableDock->show();
        m_variableDock->raise();
        // 全局变量随时可能被运行时改写（计数器等），打开时重新读一遍
        m_variablePanel->refreshGlobalVariables();
        return;
    }
}

void AuxPanelManager::createResultTableIfNeeded()
{
    if (m_resultTablePanel)
        return;
    m_resultTablePanel = new ResultTablePanel(m_mainWindow);
    m_resultTableDock = new QDockWidget(QStringLiteral("结果表"), m_mainWindow);
    m_resultTableDock->setObjectName(QStringLiteral("resultTableDock"));
    m_resultTableDock->setWidget(m_resultTablePanel);
    m_resultTableDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    m_resultTableDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_resultTableDock->setMinimumWidth(320);
    m_mainWindow->addDockWidget(Qt::RightDockWidgetArea, m_resultTableDock);
}

void AuxPanelManager::createVariableIfNeeded()
{
    if (m_variablePanel)
        return;
    m_variablePanel = new VariablePanel(m_mainWindow);
    m_variableDock = new QDockWidget(QStringLiteral("变量"), m_mainWindow);
    m_variableDock->setObjectName(QStringLiteral("variableDock"));
    m_variableDock->setWidget(m_variablePanel);
    m_variableDock->setFeatures(QDockWidget::DockWidgetClosable
                                | QDockWidget::DockWidgetMovable);
    m_variableDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_variableDock->setMinimumWidth(320);
    m_mainWindow->addDockWidget(Qt::RightDockWidgetArea, m_variableDock);
}

void AuxPanelManager::createPerformanceIfNeeded()
{
    if (m_performancePanel)
        return;
    m_performancePanel = new PerformancePanel(m_mainWindow);
    m_performanceDock = new QDockWidget(QStringLiteral("性能统计"), m_mainWindow);
    m_performanceDock->setWidget(m_performancePanel);
    m_mainWindow->addDockWidget(Qt::RightDockWidgetArea, m_performanceDock);
}

void AuxPanelManager::createOutputDataIfNeeded()
{
    if (m_outputDataViewer)
        return;
    m_outputDataViewer = new OutputDataViewer(m_mainWindow);
    m_outputViewerDock = new QDockWidget(QStringLiteral("输出数据"), m_mainWindow);
    m_outputViewerDock->setWidget(m_outputDataViewer);
    m_mainWindow->addDockWidget(Qt::RightDockWidgetArea, m_outputViewerDock);
}

void AuxPanelManager::saveVisibility() const
{
    // 键名与读写规则集中在 PanelVisibilityStore（该类可单测；MainWindow 无法在 CI 中实例化）
    PanelVisibilityStore store;
    store.setVisible(QStringLiteral("resultTable"),
                     m_resultTableDock && m_resultTableDock->isVisible());
    store.setVisible(QStringLiteral("variable"),
                     m_variableDock && m_variableDock->isVisible());
    store.setVisible(QStringLiteral("performance"),
                     m_performanceDock && m_performanceDock->isVisible());
    store.setVisible(QStringLiteral("outputData"),
                     m_outputViewerDock && m_outputViewerDock->isVisible());
}

void AuxPanelManager::restoreVisibility()
{
    // 只恢复「显隐」，不恢复几何：分辨率或显示器变化时几何恢复容易把窗口丢到屏幕外，
    // 反而让用户以为面板"打不开"。
    // 注意：这里只做「创建 + 显示」——面板特有的补充动作（性能面板绑定执行器等）
    // 由 MainWindow 在对应时机补做：启动恢复阶段执行器尚未创建，任何绑定都是空操作。
    PanelVisibilityStore store;
    const QStringList keys = PanelVisibilityStore::knownKeys();
    for (const QString &key : keys) {
        if (store.isVisible(key))
            open(key);
    }
}

void AuxPanelManager::showDialog(const QString &key, const std::function<QDialog *()> &create)
{
    if (QDialog *existing = m_auxDialogs.value(key, nullptr)) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    QDialog *dlg = create();
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_auxDialogs.insert(key, dlg);
    connect(dlg, &QObject::destroyed, this, [this, key]() { m_auxDialogs.remove(key); });
    dlg->show();   // 非模态：不阻塞主界面与其它已开窗口
    dlg->raise();
    dlg->activateWindow();
}
