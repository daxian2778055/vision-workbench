#include "RecipeDialog.h"
#include "RecipeManager.h"
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

    // Save empty recipe (actual parameter saving would require a scene reference)
    RecipeManager::instance()->saveRecipe(name.trimmed(), desc, nullptr);
    refreshRecipeList();
    emit recipeSaved(name.trimmed());
}

void RecipeDialog::onLoadRecipe()
{
    auto *item = m_recipeList->currentItem();
    if (!item) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"), QStringLiteral("\u8BF7\u9009\u62E9\u8981\u52A0\u8F7D\u7684\u914D\u65B9"));
        return;
    }
    QString name = item->text();
    emit recipeSelected(name);
    QMessageBox::information(this, QStringLiteral("\u63D0\u793A"),
        QStringLiteral("\u914D\u65B9 \"%1\" \u5DF2\u52A0\u8F7D").arg(name));
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
