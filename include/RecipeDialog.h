#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>

class FlowScene;

class RecipeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RecipeDialog(QWidget *parent = nullptr);
    ~RecipeDialog();

    /// 关联当前流程场景，**必须设置**：
    /// 之前这里没有场景，「新建配方」只能给 RecipeManager 传 nullptr（它立刻 return false），
    /// 于是什么都不存却照样 emit recipeSaved；「加载配方」发信号无人接收却弹"已加载"。
    void setFlowScene(FlowScene *scene);

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
    FlowScene *m_scene = nullptr;   ///< 当前流程场景（由 MainWindow 注入，见 setFlowScene）
};
