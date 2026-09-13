#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>

class FlowScene;
class NodeBase;

/// 算子搜索定位控件
class NodeSearchWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NodeSearchWidget(QWidget *parent = nullptr);
    ~NodeSearchWidget() override = default;

    /// 设置要搜索的流程场景
    void setFlowScene(FlowScene *scene);

    /// 清空搜索
    void clear();

    /// 显示搜索框并获取焦点
    void showAndFocus();

signals:
    /// 选中节点时发出（供主窗口定位到画布）
    void nodeSelected(NodeBase *node);

    /// 搜索框关闭时发出
    void closed();

protected:
    /// 按 Esc 关闭
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSearchTextChanged(const QString &text);
    void onSearchResultClicked(int row);
    void onSearchResultDoubleClicked(int row);

private:
    void setupUi();
    void performSearch(const QString &keyword);
    void selectNode(NodeBase *node);

    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_resultList = nullptr;
    QLabel *m_countLabel = nullptr;
    QPushButton *m_closeBtn = nullptr;

    FlowScene *m_flowScene = nullptr;
    QTimer *m_searchTimer = nullptr;
    static const int SEARCH_DELAY_MS = 200;
};
