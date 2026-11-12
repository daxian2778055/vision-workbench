#pragma once

#include <QDialog>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QStringList>

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

    /// 帮助 HTML 的空列表占位项（主界面帮助页与算子帮助窗共用一份，避免两处各写一遍）
    static QString emptyListEntry();
    /// 枚举参数当前值的显示文本：越界索引显式标注，避免把坏值显示成某个合法枚举项
    static QString enumValueText(const QStringList &values, int index);

private:
    /// 加载帮助文档
    void loadHelpContent(const QString &nodeId);

    /// 创建默认帮助内容
    QString createDefaultHelpContent(const QString &nodeId) const;

    /// 按**实例**生成详细帮助：参数表（当前值/范围/默认/说明）、结果字段、端口。
    /// 与「帮助信息」页同源（ParamSpec / resultDisplayFields），但采用本窗深色版式。
    /// 节点实例为空或非算法节点时返回空串，调用方回退到按类型 ID 的通用内容。
    QString buildInstanceHelpHtml(NodeBase *node) const;

    /// 获取节点类型的中文描述
    QString getNodeCategoryDescription(const QString &nodeId) const;

    QTextBrowser *m_textBrowser = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_titleLabel = nullptr;
    QString m_currentNodeId;

    /// 帮助文档内容缓存（静态，全局共享）
    static QMap<QString, QString> s_helpContents;
};
