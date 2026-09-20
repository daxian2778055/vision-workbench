#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <functional>

class QMainWindow;
class QDialog;
class QDockWidget;
class ResultTablePanel;
class VariablePanel;
class PerformancePanel;
class OutputDataViewer;

/// 辅助面板/辅助窗口管理（从 MainWindow 抽出）。
///
/// 负责四类懒创建停靠面板：结果表 / 变量 / 性能统计 / 输出数据——
/// 创建、显示、显隐持久化（经 PanelVisibilityStore，键名契约不变）；
/// 以及「非模态辅助窗口」的统一打开逻辑（同 key 复用，关闭即销毁）。
///
/// 数据喂给由 MainWindow 的访问器完成：只有面板已创建（= 用户打开过）才喂，
/// 未创建时访问器返回 nullptr，行为与历史一致（"没打开就不喂"）。
class AuxPanelManager : public QObject
{
    Q_OBJECT
public:
    explicit AuxPanelManager(QMainWindow *mainWindow, QObject *parent = nullptr);

    /// 打开并前置（懒创建）。key: resultTable / variable / performance / outputData
    void open(const QString &key);

    /// 已创建的面板指针；未创建返回 nullptr
    ResultTablePanel *resultTablePanel() const { return m_resultTablePanel; }
    VariablePanel *variablePanel() const { return m_variablePanel; }
    PerformancePanel *performancePanel() const { return m_performancePanel; }
    OutputDataViewer *outputDataViewer() const { return m_outputDataViewer; }

    /// 记录当前显隐 / 按上次记录恢复（QSettings，经 PanelVisibilityStore）
    void saveVisibility() const;
    void restoreVisibility();

    /// 非模态打开辅助窗口：已开着就前置激活（不重复开），关闭后自动销毁
    void showDialog(const QString &key, const std::function<QDialog *()> &create);

private:
    void createResultTableIfNeeded();
    void createVariableIfNeeded();
    void createPerformanceIfNeeded();
    void createOutputDataIfNeeded();

    QMainWindow *m_mainWindow;
    ResultTablePanel *m_resultTablePanel = nullptr;
    QDockWidget *m_resultTableDock = nullptr;
    VariablePanel *m_variablePanel = nullptr;
    QDockWidget *m_variableDock = nullptr;
    PerformancePanel *m_performancePanel = nullptr;
    QDockWidget *m_performanceDock = nullptr;
    OutputDataViewer *m_outputDataViewer = nullptr;
    QDockWidget *m_outputViewerDock = nullptr;
    QHash<QString, QDialog *> m_auxDialogs;
};
