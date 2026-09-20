#include "ExecutionStatusController.h"

#include <QStatusBar>
#include <QLabel>
#include <QAction>
#include <QToolButton>

ExecutionStatusController::ExecutionStatusController(QStatusBar *statusBar, QObject *parent)
    : QObject(parent)
    , m_statusBar(statusBar)
{
    if (!m_statusBar)
        return;
    const QString statusStyle =
        "QLabel {"
        "  color: #c8c8c8; font-size: 12px;"
        "  padding: 2px 10px; border-right: 1px solid #3a3a4a;"
        "}";
    auto makeStatusLabel = [&](const QString &text) {
        auto *lbl = new QLabel(text, m_statusBar);
        lbl->setStyleSheet(statusStyle);
        m_statusBar->addPermanentWidget(lbl);
        return lbl;
    };
    m_stateLabel = makeStatusLabel(QStringLiteral("状态: 空闲"));
    m_timeLabel = makeStatusLabel(QStringLiteral("耗时: --"));
    m_triggerLabel = makeStatusLabel(QStringLiteral("触发: 0"));
}

void ExecutionStatusController::setControls(QAction *startAction, QAction *stopAction,
                                            QToolButton *singleShotBtn)
{
    m_startAction = startAction;
    m_stopAction = stopAction;
    m_singleShotBtn = singleShotBtn;
}

void ExecutionStatusController::updateButtons(ExecutionState state)
{
    FlowExecutor *ex = m_executorProvider ? m_executorProvider() : nullptr;
    switch (state) {
    case ExecutionState::Stopped:
        // 只有软触发模式才启用「开始执行」和「单次执行」
        if (ex && ex->getFlowMode() == FlowMode::SoftwareTrigger) {
            if (m_startAction) m_startAction->setEnabled(true);
            if (m_singleShotBtn) m_singleShotBtn->setEnabled(true);
        } else {
            if (m_startAction) m_startAction->setEnabled(false);
            if (m_singleShotBtn) m_singleShotBtn->setEnabled(false);
        }
        if (m_stopAction) m_stopAction->setEnabled(false);
        break;
    case ExecutionState::Running:
        if (m_startAction) m_startAction->setEnabled(false);
        if (m_singleShotBtn) m_singleShotBtn->setEnabled(false);
        if (m_stopAction) m_stopAction->setEnabled(true);
        break;
    default:
        break;
    }
}

void ExecutionStatusController::onStarted()
{
    if (m_statusBar) m_statusBar->showMessage(tr("执行开始"));
    m_runTimer.start();
    if (m_stateLabel) m_stateLabel->setText(QStringLiteral("状态: 运行中"));
    updateButtons(ExecutionState::Running);
}

void ExecutionStatusController::onStopped()
{
    if (m_statusBar) m_statusBar->showMessage(tr("执行停止"));
    if (m_stateLabel) m_stateLabel->setText(QStringLiteral("状态: 已停止"));
    if (m_timeLabel && m_runTimer.isValid()) {
        m_lastRunMs = m_runTimer.elapsed();
        m_timeLabel->setText(QStringLiteral("耗时: %1 ms").arg(m_lastRunMs));
    }
    updateButtons(ExecutionState::Stopped);
}

void ExecutionStatusController::onFinished()
{
    // 连续模式下，执行完毕会自动继续循环，这里仅刷新 UI；软触发/硬触发模式下执行结束
    FlowExecutor *ex = m_executorProvider ? m_executorProvider() : nullptr;
    const FlowMode currentMode = ex ? ex->getFlowMode() : FlowMode::SoftwareTrigger;
    if (currentMode == FlowMode::Continuous) {
        if (m_statusBar) m_statusBar->showMessage(tr("连续运行中..."));
    } else {
        if (m_statusBar) m_statusBar->showMessage(tr("执行完成"));
        if (m_stateLabel) m_stateLabel->setText(QStringLiteral("状态: 空闲"));
        updateButtons(ExecutionState::Stopped);
    }

    // 每次流程完整执行一轮：触发计数 +1 并刷新耗时
    ++m_triggerCount;
    if (m_triggerLabel)
        m_triggerLabel->setText(QStringLiteral("触发: %1").arg(m_triggerCount));
    if (m_timeLabel && m_runTimer.isValid()) {
        m_lastRunMs = m_runTimer.elapsed();
        m_timeLabel->setText(QStringLiteral("耗时: %1 ms").arg(m_lastRunMs));
    }
}

void ExecutionStatusController::onError(const QString &error)
{
    if (m_stateLabel) m_stateLabel->setText(QStringLiteral("状态: 错误"));
    if (m_statusBar) m_statusBar->showMessage(tr("执行错误: %1").arg(error));
    // 不再弹模态框：连续模式下错误可能每轮出现，模态框嵌套事件循环会把界面
    // 变成"点不完的确认框"（生产现场不可接受）。错误改为状态栏 + 调用方日志留痕。
}

QString ExecutionStatusController::stateText() const
{
    return m_stateLabel ? m_stateLabel->text() : QString();
}
