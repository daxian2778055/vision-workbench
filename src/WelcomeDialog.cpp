#include "WelcomeDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QPushButton *makeActionButton(const QString &text, const QString &subtitle, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setToolTip(subtitle);
    button->setMinimumHeight(46);
    button->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color:#2d3a52; color:#e6e6e6; border:1px solid #46688f;"
        "  border-radius:5px; text-align:left; padding-left:14px; font-size:14px;"
        "}"
        "QPushButton:hover { background-color:#3a4d6d; border-color:#6f9ac9; }"
        "QPushButton:pressed { background-color:#26344a; }"));
    return button;
}
}

WelcomeDialog::WelcomeDialog(const QStringList &recentFiles, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("welcomeDialog"));
    setWindowTitle(tr("欢迎使用 VisionFlowPlatform"));
    setMinimumSize(780, 500);
    setStyleSheet(QStringLiteral(
        "QDialog#welcomeDialog { background-color:#1f2430; }"
        "QLabel { color:#d8dee9; }"
        "QListWidget { background-color:#252b38; color:#d8dee9; border:1px solid #3a4354; }"
        "QCheckBox { color:#aeb8c8; }"));

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(26);

    // ---- 左列：起点动作 ----
    auto *left = new QVBoxLayout();
    left->setSpacing(12);

    auto *title = new QLabel(tr("VisionFlowPlatform"), this);
    title->setStyleSheet(QStringLiteral("font-size:26px; font-weight:bold; color:#ffffff;"));
    left->addWidget(title);

    auto *subtitle = new QLabel(tr("视觉流程开发平台 · 对标 VisionMaster 4.4"), this);
    subtitle->setStyleSheet(QStringLiteral("font-size:13px; color:#9fb3c8;"));
    left->addWidget(subtitle);
    left->addSpacing(8);

    auto *actions = new QGridLayout();
    actions->setHorizontalSpacing(12);
    actions->setVerticalSpacing(12);
    auto *newBtn = makeActionButton(tr("新建空白方案"), tr("创建一条空的流程并开始搭建"), this);
    auto *openBtn = makeActionButton(tr("打开已有方案"), tr("浏览并载入 .vfp 方案文件"), this);
    auto *exampleBtn = makeActionButton(tr("打开示例方案"), tr("载入随软件附带的示例"), this);
    auto *manualBtn = makeActionButton(tr("使用手册"), tr("查看操作说明与快捷键（F1）"), this);
    actions->addWidget(newBtn, 0, 0);
    actions->addWidget(openBtn, 0, 1);
    actions->addWidget(exampleBtn, 1, 0);
    actions->addWidget(manualBtn, 1, 1);
    left->addLayout(actions);
    left->addSpacing(10);

    auto *recentTitle = new QLabel(tr("最近打开"), this);
    recentTitle->setStyleSheet(QStringLiteral("font-size:14px; color:#c8d4e3;"));
    left->addWidget(recentTitle);

    m_recentList = new QListWidget(this);
    m_recentList->setMinimumHeight(150);
    for (const QString &path : recentFiles) {
        const QFileInfo info(path);
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n%2").arg(info.completeBaseName(), info.absoluteFilePath()),
            m_recentList);
        item->setData(Qt::UserRole, info.absoluteFilePath());
        item->setToolTip(info.absoluteFilePath());
    }
    if (recentFiles.isEmpty()) {
        auto *empty = new QListWidgetItem(tr("暂无最近打开的方案"), m_recentList);
        empty->setFlags(Qt::NoItemFlags);
    }
    left->addWidget(m_recentList, 1);

    m_dontShowCheck = new QCheckBox(tr("启动时不再显示此页"), this);
    left->addWidget(m_dontShowCheck);

    root->addLayout(left, 3);

    // ---- 右列：快速上手 ----
    auto *right = new QVBoxLayout();
    right->setSpacing(8);

    auto *guideTitle = new QLabel(tr("快速上手"), this);
    guideTitle->setStyleSheet(QStringLiteral("font-size:18px; font-weight:bold; color:#ffffff;"));
    right->addWidget(guideTitle);

    auto *guide = new QLabel(
        tr("1. 从左侧「工具箱」拖入算子。\n"
           "2. 拖到已有模块附近并水平对齐时，会自动连接。\n"
           "3. 双击模块打开配置窗，修改参数后点「确定」。\n"
           "4. 选中模块可查看其图像与上游图像。\n"
           "5. F5 运行一次，F6 连续运行，Esc 停止。"),
        this);
    guide->setWordWrap(true);
    guide->setStyleSheet(QStringLiteral("font-size:13px; line-height:22px; color:#c3cbd8;"));
    right->addWidget(guide);
    right->addSpacing(12);

    auto *note = new QLabel(
        tr("提示：\n"
           "· 流程栏右键可重命名 / 拷贝 / 删除 / 导出 Prc / 设置运行间隔。\n"
           "· 顶部工具条支持撤销/重做历史、画布锁定、综合配置。\n"
           "· 日志栏默认只显示最近一条，点「展开」查看全量。"),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("font-size:12px; color:#9fb3c8;"));
    right->addWidget(note);
    right->addStretch();

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttonBox->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    right->addWidget(buttonBox);

    root->addLayout(right, 2);

    connect(newBtn, &QPushButton::clicked, this, [this]() { choose(Action::NewProject); });
    connect(openBtn, &QPushButton::clicked, this, [this]() { choose(Action::OpenProject); });
    connect(exampleBtn, &QPushButton::clicked, this, [this]() { choose(Action::OpenExample); });
    connect(manualBtn, &QPushButton::clicked, this, [this]() { choose(Action::OpenManual); });
    connect(m_recentList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                if (!item)
                    return;
                const QString path = item->data(Qt::UserRole).toString();
                if (path.isEmpty())
                    return;
                m_selectedPath = path;
                choose(Action::OpenRecent);
            });
}

bool WelcomeDialog::dontShowAgain() const
{
    return m_dontShowCheck && m_dontShowCheck->isChecked();
}

void WelcomeDialog::choose(Action action)
{
    m_action = action;
    accept();
}
