#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QList>

class QTreeWidget;
class QTreeWidgetItem;
class FlowScene;
class NodeBase;

/// 参数查找工具（P2-7）——按 VM「参数查找工具」的三层结构展示：
/// 流程 → 模块 → 输入参数 / 输出参数 / 运行参数，叶子四列：
/// 软件参数名、算子参数名、当前值、调用代码。
/// 关键字搜索保留为加速入口：过滤后只保留命中的叶子及其父层级。
class ParameterSearchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ParameterSearchDialog(const QList<FlowScene *> &flows,
                                   QWidget *parent = nullptr);
    ~ParameterSearchDialog() override;

private slots:
    void onSearch();
    void onLocateNode();
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    void setupUI();
    void buildTree();
    void applyFilter();

    QList<FlowScene *> m_flows;
    QTreeWidget *m_resultTree = nullptr;
    QLineEdit *m_keywordEdit = nullptr;
    QComboBox *m_scopeCombo = nullptr;   ///< 全部 / 参数名 / 参数值
    QPushButton *m_searchBtn = nullptr;
    QPushButton *m_locateBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};
