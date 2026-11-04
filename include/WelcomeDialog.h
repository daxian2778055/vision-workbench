#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QListWidget;

/// P2-1：启动欢迎页（对标 VM「界面介绍」的欢迎页）。
/// 只负责“选什么”，不执行任何方案操作——打开/新建由 MainWindow 决定，
/// 避免欢迎页反向依赖主窗口的流程状态。
class WelcomeDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Action {
        None,
        NewProject,     ///< 新建空白方案
        OpenProject,    ///< 浏览打开方案
        OpenRecent,     ///< 打开 recentList 中选中的方案
        OpenExample,    ///< 打开内置示例方案
        OpenManual      ///< 打开使用手册
    };

    explicit WelcomeDialog(const QStringList &recentFiles, QWidget *parent = nullptr,
                           bool exampleAvailable = false);

    Action chosenAction() const { return m_action; }
    QString selectedProjectPath() const { return m_selectedPath; }
    bool dontShowAgain() const;

private:
    void choose(Action action);

    Action m_action = Action::None;
    QString m_selectedPath;
    QCheckBox *m_dontShowCheck = nullptr;
    QListWidget *m_recentList = nullptr;
};
