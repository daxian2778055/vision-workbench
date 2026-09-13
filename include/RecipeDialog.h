#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>

class RecipeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RecipeDialog(QWidget *parent = nullptr);
    ~RecipeDialog();

signals:
    void recipeSelected(const QString &name);
    void recipeSaved(const QString &name);

private slots:
    void onNewRecipe();
    void onLoadRecipe();
    void onDeleteRecipe();
    void onExportRecipe();
    void onImportRecipe();
    void refreshRecipeList();

private:
    void setupUI();

    QListWidget *m_recipeList;
    QPushButton *m_newButton;
    QPushButton *m_loadButton;
    QPushButton *m_deleteButton;
    QPushButton *m_exportButton;
    QPushButton *m_importButton;
    QPushButton *m_closeButton;
    QLabel *m_infoLabel;
};
