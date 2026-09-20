#pragma once

#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <functional>

#include "FlowExecutor.h"   // ExecutionState / FlowMode

class QStatusBar;
class QLabel;
class QAction;
class QToolButton;

/// 执行状态显示与执行按钮态（从 MainWindow 抽出）。
///
/// 职责：
///   - 状态栏三项常驻信息：运行状态 / 本次耗时 / 触发计数，及其全部更新规则；
///   - 「开始执行 / 停止执行 / 单次执行」按钮可用态（软触发才可开始；运行中可停止）；
///   - 与 FlowExecutor 四个状态信号一一对应的状态机（onStarted/Stopped/Finished/Error）。
///
/// 执行器与控件通过注入获得（setExecutorProvider / setControls），不反向依赖 MainWindow；
/// 无控件/无执行器时只跑状态机（可单测：构造只需一个 QStatusBar）。
class ExecutionStatusController : public QObject
{
    Q_OBJECT
public:
    explicit ExecutionStatusController(QStatusBar *statusBar, QObject *parent = nullptr);

    /// 注入受控控件（允许为空：无界面环境下只跑状态机）
    void setControls(QAction *startAction, QAction *stopAction, QToolButton *singleShotBtn);
    /// 注入"当前激活执行器"取值函数（按钮态与连续模式判定用）
    void setExecutorProvider(std::function<FlowExecutor *()> provider)
    { m_executorProvider = std::move(provider); }

    /// 按执行状态刷新按钮可用态（Stopped：软触发才可开始；Running：可停止）
    void updateButtons(ExecutionState state);

    // 状态机（与 FlowExecutor 信号一一对应；调用方负责刷新 MVS 像素格式/编辑锁等窗口级动作）
    void onStarted();
    void onStopped();
    void onFinished();
    void onError(const QString &error);

    // 供诊断/测试
    QString stateText() const;
    int triggerCount() const { return static_cast<int>(m_triggerCount); }
    qint64 lastRunMs() const { return m_lastRunMs; }

private:
    QStatusBar *m_statusBar;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_triggerLabel = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_stopAction = nullptr;
    QToolButton *m_singleShotBtn = nullptr;
    std::function<FlowExecutor *()> m_executorProvider;
    QElapsedTimer m_runTimer;
    qint64 m_lastRunMs = 0;
    quint64 m_triggerCount = 0;
};
