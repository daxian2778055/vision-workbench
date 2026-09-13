#pragma once

#include <QDialog>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QMap>

class NodeBase;

/// 算子帮助文档查看器
class HelpViewer : public QDialog
{
    Q_OBJECT

public:
    explicit HelpViewer(QWidget *parent = nullptr);
    ~HelpViewer() override = default;

    /// 显示指定算子的帮助文档
    void showHelpForNode(NodeBase *node);

    /// 显示指定节点类型ID的帮助文档
    void showHelpForNodeId(const QString &nodeId);

    /// 注册帮助文档内容
    static void registerHelpContent(const QString &nodeId, const QString &htmlContent);

private:
    /// 加载帮助文档
    void loadHelpContent(const QString &nodeId);

    /// 创建默认帮助内容
    QString createDefaultHelpContent(const QString &nodeId) const;

    /// 获取节点类型的中文描述
    QString getNodeCategoryDescription(const QString &nodeId) const;

    QTextBrowser *m_textBrowser = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_titleLabel = nullptr;
    QString m_currentNodeId;

    /// 帮助文档内容缓存（静态，全局共享）
    static QMap<QString, QString> s_helpContents;
};
