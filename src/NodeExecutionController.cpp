#include "NodeExecutionController.h"
#include "FlowExecutor.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "MvsImageSourceNode.h"
#include "AppLog.h"

NodeExecutionController::NodeExecutionController(FlowExecutor *executor, QComboBox *modeCombo,
                                                   QObject *parent)
    : QObject(parent)
    , m_executor(executor)
    , m_modeCombo(modeCombo)
    , m_currentScene(nullptr)
{
    if (m_modeCombo) {
        connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &NodeExecutionController::onModeChanged);
    }
}

void NodeExecutionController::setCurrentScene(FlowScene *scene)
{
    m_currentScene = scene;
}

void NodeExecutionController::onStartExecution()
{
    if (m_executor) {
        m_executor->startExecution();
        emit executionStarted();
    }
}

void NodeExecutionController::onStopExecution()
{
    if (m_executor) {
        m_executor->stopExecution();
        m_executor->wait(1000);
        emit executionStopped();
    }
}

void NodeExecutionController::onModeChanged(int index)
{
    if (!m_modeCombo || !m_executor) return;

    auto mode = static_cast<FlowMode>(m_modeCombo->itemData(index).toInt());
    m_executor->setFlowMode(mode);

    updateExecutionButtons();
    refreshAllMvsPixelFormats();
}

void NodeExecutionController::updateExecutionButtons()
{
    // Override in MainWindow via signal connections
}

void NodeExecutionController::refreshAllMvsPixelFormats()
{
    // This is handled by MainWindow's existing logic via the executor signal
}
