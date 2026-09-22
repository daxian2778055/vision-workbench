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
                                            QToolButton *singleShotBtn, QToolButton *pauseBtn)
{
    m_startAction = startAction;
    m_stopAction = stopAction;
    m_singleShotBtn = singleShotBtn;
    m_pauseBtn = pauseBtn;
}

void ExecutionStatusController::updateButtons(ExecutionState state)
{
    FlowExecutor *ex = m_executorProvider ? m_executorProvider() : nullptr;
    const FlowMode mode = ex ? ex->getFlowMode() : FlowMode::SoftwareTrigger;
    const bool running = (state == ExecutionState::Running);
    const bool paused = (state == ExecutionState::Paused);

    // 运行控制与流程模式解耦（对齐 VisionMaster 的操作逻辑）：
    //  · 开始执行：只要没在跑就可点（任何模式）；暂停中也可点，语义=继续 → 不留"点了没反应"的死按钮；
    //  · 暂停/继续：运行中显示「暂停」，暂停中显示「继续」；
    //  · 停止执行：运行中与暂停中都可点（暂停中停止要能生效）；
    //  · 单次执行：语义是"以软触发跑一次"，保持仅软触发且未运行时可用。
    // 旧规则（仅 Stop/Running 两态、且只有软触发才启用开始）在连续/硬触发模式下把"开始"永久禁用了，
    // 加上 Idle/Paused 落入 default 分支不刷新按钮态 → 停止后再也无法启动。
    if (m_startAction) m_startAction->setEnabled(!running);
    if (m_singleShotBtn) m_singleShotBtn->setEnabled(!running && !paused && mode == FlowMode::SoftwareTrigger);
    if (m_stopAction) m_stopAction->setEnabled(running || paused);
    if (m_pauseBtn) {
        m_pauseBtn->setEnabled(running || paused);
        m_pauseBtn->setText(paused ? tr("继续") : tr("暂停"));
        m_pauseBtn->setToolTip(paused ? tr("继续执行（不清输入缓存）")
                                      : tr("暂停当前流程（不清输入缓存，再按继续）"));
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

void ExecutionStatusController::onPaused()
{
    // 暂停请求已受理：当前节点可能仍在跑（"已暂停"由 onParked 在 worker 真停稳后给出）
    if (m_statusBar) m_statusBar->showMessage(tr("已请求暂停（等当前节点跑完）"));
    if (m_stateLabel) m_stateLabel->setText(QStringLiteral("状态: 暂停中…"));
    updateButtons(ExecutionState::Paused);
}

void ExecutionStatusController::onParked()
{
    if (m_stateLabel) m_stateLabel->setText(QStringLiteral("状态: 已暂停"));
    if (m_statusBar) m_statusBar->showMessage(tr("已暂停"));
}

void ExecutionStatusController::onResumed()
{
    if (m_stateLabel) m_stateLabel->setText(QStringLiteral("状态: 运行中"));
    if (m_statusBar) m_statusBar->showMessage(tr("已继续"));
    updateButtons(ExecutionState::Running);
}

QString ExecutionStatusController::stateText() const
{
    return m_stateLabel ? m_stateLabel->text() : QString();
}
