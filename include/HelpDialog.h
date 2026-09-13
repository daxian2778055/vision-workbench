#pragma once

#include <QDialog>

class QTextBrowser;

/// 使用手册查看对话框（Markdown 渲染，只读）
class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(const QString &markdownText, QWidget *parent = nullptr);

private:
    QTextBrowser *m_browser = nullptr;
};
