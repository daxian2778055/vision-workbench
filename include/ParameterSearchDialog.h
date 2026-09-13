#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QList>

class FlowScene;
class NodeBase;

/// 参数查找工具 — 方案内全局搜索算子的参数（名称/值过滤）
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

private:
    void setupUI();
    void collectNodes();
    void doSearch();

    struct Entry {
        QString flowName;
        QString nodeName;
        QString paramName;
        QString paramValue;
        NodeBase *node = nullptr;
    };
    QList<Entry> m_entries;

    QList<FlowScene *> m_flows;
    QTableWidget *m_resultTable = nullptr;
    QLineEdit *m_keywordEdit = nullptr;
    QComboBox *m_scopeCombo = nullptr;   ///< 全部 / 参数名 / 参数值
    QPushButton *m_searchBtn = nullptr;
    QPushButton *m_locateBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};
