#include "HelpDialog.h"
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QPushButton>
#include <QHBoxLayout>

HelpDialog::HelpDialog(const QString &markdownText, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("VisionFlowPlatform 使用手册"));
    resize(900, 700);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_browser = new QTextBrowser(this);
    m_browser->setOpenExternalLinks(true);
    m_browser->setStyleSheet(
        "QTextBrowser { background: #fafafa; color: #222; font-size: 13px; }"
    );
    // Qt 6 Markdown 渲染（标题/列表/表格/代码块/粗体斜体）
    m_browser->document()->setMarkdown(markdownText);
    layout->addWidget(m_browser, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);
}
