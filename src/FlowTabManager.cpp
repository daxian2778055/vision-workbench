#include "FlowTabManager.h"
#include "FlowScene.h"
#include "VisionWorkbenchStyle.h"
#include "AppLog.h"
#include <QGraphicsView>
#include <QTimer>

FlowTabManager::FlowTabManager(QTabWidget *tabWidget, QObject *parent)
    : QObject(parent)
    , m_tabWidget(tabWidget)
{
    if (m_tabWidget) {
        m_tabWidget->setDocumentMode(true);
        m_tabWidget->setTabsClosable(true);
        connect(m_tabWidget, &QTabWidget::currentChanged, this, &FlowTabManager::onTabChanged);
    }
}

FlowScene *FlowTabManager::currentScene() const
{
    int idx = m_tabWidget ? m_tabWidget->currentIndex() : -1;
    if (idx >= 0 && idx < m_flowScenes.size())
        return m_flowScenes[idx];
    return nullptr;
}

FlowScene *FlowTabManager::createNewFlow()
{
    try {
        FlowScene *scene = new FlowScene(this);
        m_flowScenes.append(scene);

        QGraphicsView *view = new QGraphicsView(scene);
        VisionWorkbenchStyle::applyGraphicsViewWorkbenchDefaults(view);
        QTimer::singleShot(0, this, [view]() {
            view->centerOn(0, 0);
        });

        if (m_tabWidget) {
            int index = m_tabWidget->addTab(view, QStringLiteral("\u6D41\u7A0B %1 [\u8F6F\u89E6\u53D1]").arg(m_flowScenes.size()));
            m_tabWidget->setCurrentIndex(index);
        }

        // Default mode: SoftwareTrigger
        m_flowModes[scene] = FlowMode::SoftwareTrigger;

        emit sceneCreated(scene);
        return scene;
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in FlowTabManager::createNewFlow:" << e.what();
        return nullptr;
    }
}

void FlowTabManager::closeFlowTab(int index)
{
    if (index < 0 || index >= m_flowScenes.size() || !m_tabWidget)
        return;

    FlowScene *scene = m_flowScenes[index];
    emit sceneAboutToClose(scene);

    m_flowModes.remove(scene);
    QWidget *tabWidget = m_tabWidget->widget(index);
    m_flowScenes.removeAt(index);
    m_tabWidget->removeTab(index);
    delete tabWidget;
    delete scene;
}

void FlowTabManager::clearAllFlows()
{
    while (!m_flowScenes.isEmpty() && m_tabWidget && m_tabWidget->count() > 0) {
        closeFlowTab(0);
    }
}

FlowMode FlowTabManager::flowModeForScene(FlowScene *scene) const
{
    return m_flowModes.value(scene, FlowMode::SoftwareTrigger);
}

void FlowTabManager::setFlowModeForScene(FlowScene *scene, FlowMode mode)
{
    if (scene && m_flowScenes.contains(scene)) {
        m_flowModes[scene] = mode;

        // Update tab title with mode suffix
        int idx = m_flowScenes.indexOf(scene);
        if (idx >= 0 && m_tabWidget) {
            static const char *modeSuffix[] = { " [\u8FDE\u7EED]", " [\u8F6F\u89E6\u53D1]", " [\u786C\u89E6\u53D1]" };
            int mi = static_cast<int>(mode);
            const char *suffix = (mi >= 0 && mi < 3) ? modeSuffix[mi] : "";
            m_tabWidget->setTabText(idx, QStringLiteral("\u6D41\u7A0B %1%2").arg(idx + 1).arg(suffix));
        }

        emit flowModeChangedForScene(scene, mode);
    }
}

void FlowTabManager::onTabChanged(int index)
{
    if (index >= 0 && index < m_flowScenes.size()) {
        emit currentSceneChanged(m_flowScenes[index]);
    } else {
        emit currentSceneChanged(nullptr);
    }
}
