#include "RecipeDialog.h"
#include "RecipeManager.h"
#include "FlowScene.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QLabel>

RecipeDialog::RecipeDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    refreshRecipeList();
}

RecipeDialog::~RecipeDialog()
{
}

void RecipeDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u914D\u65B9\u7BA1\u7406"));
    setMinimumSize(500, 400);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QStringLiteral("<b>\u914D\u65B9\u7BA1\u7406</b>"));
    titleLabel->setStyleSheet("font-size: 14px;");
    mainLayout->addWidget(titleLabel);

    m_recipeList = new QListWidget();
    m_recipeList->setAlternatingRowColors(true);
    mainLayout->addWidget(m_recipeList);

    m_infoLabel = new QLabel();
    m_infoLabel->setStyleSheet("color: gray;");
    m_infoLabel->hide();
    mainLayout->addWidget(m_infoLabel);

    auto *buttonLayout = new QHBoxLayout();

    m_newButton = new QPushButton(QStringLiteral("\u65B0\u5EFA\u914D\u65B9"));
    m_loadButton = new QPushButton(QStringLiteral("\u52A0\u8F7D\u914D\u65B9"));
    m_deleteButton = new QPushButton(QStringLiteral("\u5220\u9664\u914D\u65B9"));
    m_exportButton = new QPushButton(QStringLiteral("\u5BFC\u51FA"));
    m_importButton = new QPushButton(QStringLiteral("\u5BFC\u5165"));
    m_closeButton = new QPushButton(QStringLiteral("\u5173\u95ED"));

    buttonLayout->addWidget(m_newButton);
    buttonLayout->addWidget(m_loadButton);
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_exportButton);
    buttonLayout->addWidget(m_importButton);
    buttonLayout->addWidget(m_closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_newButton, &QPushButton::clicked, this, &RecipeDialog::onNewRecipe);
    connect(m_loadButton, &QPushButton::clicked, this, &RecipeDialog::onLoadRecipe);
    connect(m_deleteButton, &QPushButton::clicked, this, &RecipeDialog::onDeleteRecipe);
    connect(m_exportButton, &QPushButton::clicked, this, &RecipeDialog::onExportRecipe);
    connect(m_importButton, &QPushButton::clicked, this, &RecipeDialog::onImportRecipe);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_recipeList, &QListWidget::itemDoubleClicked, this, &RecipeDialog::onLoadRecipe);
}

void RecipeDialog::setFlowScene(FlowScene *scene)
{
    m_scene = scene;
}

void RecipeDialog::onNewRecipe()
{
    bool ok = false;
    QString name = QInputDialog::getText(this,
        QStringLiteral("\u65B0\u5EFA\u914D\u65B9"),
        QStringLiteral("\u914D\u65B9\u540D\u79F0:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QString desc = QInputDialog::getText(this,
        QStringLiteral("\u914D\u65B9\u63CF\u8FF0"),
        QStringLiteral("\u63CF\u8FF0:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) desc.clear();

    // 这里以前传的是 nullptr：RecipeManager 会立刻 return false，
    // 于是"什么都没存"却照样 emit recipeSaved、界面还提示成功。
    // 宁可不做，也不要假装做成了。
    if (!m_scene) {
        QMessageBox::warning(this, QStringLiteral("无法保存配方"),
                             QStringLiteral("没有关联的流程，抓取不到算子参数。"));
        return;
    }

    const QString recipeName = name.trimmed();
    if (!RecipeManager::instance()->saveRecipe(recipeName, desc, m_scene)) {
        QMessageBox::warning(this, QStringLiteral("保存配方失败"),
                             QStringLiteral("配方「%1」未能保存。").arg(recipeName));
        return;
    }
    refreshRecipeList();
    emit recipeSaved(recipeName);
    QMessageBox::information(this, QStringLiteral("成功"),
                             QStringLiteral("配方「%1」已保存（当前流程 %2 个算子）。")
                                 .arg(recipeName)
                                 .arg(m_scene->nodes().size()));
}

void RecipeDialog::onLoadRecipe()
{
    auto *item = m_recipeList->currentItem();
    if (!item) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"), QStringLiteral("\u8BF7\u9009\u62E9\u8981\u52A0\u8F7D\u7684\u914D\u65B9"));
        return;
    }
    const QString name = item->text();

    if (!m_scene) {
        QMessageBox::warning(this, QStringLiteral("无法加载配方"),
                             QStringLiteral("没有关联的流程，配方参数无从写回。"));
        return;
    }

    // 以前这里只发信号、不真的加载，却无条件弹"已加载"——提示与实际脱节
    // （而且信号当时根本没人接）。现在同步加载，并按**真实结果**提示。
    const bool loaded = RecipeManager::instance()->loadRecipe(name, m_scene);
    m_scene->update();   // 参数已变，重绘画布上的算子摘要

    if (loaded) {
        emit recipeSelected(name);   // 仍发信号：MainWindow 可据此做后续刷新
        QMessageBox::information(this, QStringLiteral("成功"),
                                 QStringLiteral("配方「%1」已加载。").arg(name));
    } else {
        // 配方按算子 ID 匹配：换了工程/流程后 ID 对不上，就会一个都匹配不到。
        // 这时如实说清原因，比弹"已加载"有用得多。
        QMessageBox::warning(
            this, QStringLiteral("加载失败"),
            QStringLiteral("配方「%1」没有任何参数匹配到当前流程的算子。\n"
                           "配方按算子 ID 匹配，通常需先打开保存该配方时的工程。")
                .arg(name));
    }
}

void RecipeDialog::onDeleteRecipe()
{
    auto *item = m_recipeList->currentItem();
    if (!item) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"), QStringLiteral("\u8BF7\u9009\u62E9\u8981\u5220\u9664\u7684\u914D\u65B9"));
        return;
    }

    if (QMessageBox::question(this, QStringLiteral("\u786E\u8BA4"),
                              QStringLiteral("\u786E\u5B9A\u8981\u5220\u9664\u914D\u65B9 \"%1\" \u5417\uFF1F").arg(item->text()))
        == QMessageBox::Yes) {
        RecipeManager::instance()->deleteRecipe(item->text());
        refreshRecipeList();
    }
}

void RecipeDialog::onExportRecipe()
{
    auto *item = m_recipeList->currentItem();
    if (!item) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"), QStringLiteral("\u8BF7\u9009\u62E9\u8981\u5BFC\u51FA\u7684\u914D\u65B9"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        QStringLiteral("\u5BFC\u51FA\u914D\u65B9"),
        item->text() + QStringLiteral(".json"),
        QStringLiteral("JSON Files (*.json)"));
    if (filePath.isEmpty()) return;

    if (RecipeManager::instance()->exportRecipe(item->text(), filePath)) {
        QMessageBox::information(this, QStringLiteral("\u6210\u529F"), QStringLiteral("\u914D\u65B9\u5BFC\u51FA\u6210\u529F"));
    } else {
        QMessageBox::warning(this, QStringLiteral("\u5931\u8D25"), QStringLiteral("\u914D\u65B9\u5BFC\u51FA\u5931\u8D25"));
    }
}

void RecipeDialog::onImportRecipe()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        QStringLiteral("\u5BFC\u5165\u914D\u65B9"),
        QString(),
        QStringLiteral("JSON Files (*.json)"));
    if (filePath.isEmpty()) return;

    if (RecipeManager::instance()->importRecipe(filePath)) {
        refreshRecipeList();
        QMessageBox::information(this, QStringLiteral("\u6210\u529F"), QStringLiteral("\u914D\u65B9\u5BFC\u5165\u6210\u529F"));
    } else {
        QMessageBox::warning(this, QStringLiteral("\u5931\u8D25"), QStringLiteral("\u914D\u65B9\u5BFC\u5165\u5931\u8D25"));
    }
}

void RecipeDialog::refreshRecipeList()
{
    m_recipeList->clear();
    QStringList names = RecipeManager::instance()->recipeNames();
    for (const QString &name : names) {
        m_recipeList->addItem(name);
    }

    if (names.isEmpty()) {
        m_infoLabel->setText(QStringLiteral("\u6682\u65E0\u914D\u65B9\uFF0C\u8BF7\u65B0\u5EFA"));
        m_infoLabel->show();
    } else {
        m_infoLabel->hide();
    }
}
