#pragma once

#include "HalconNode.h"
#include <QTextEdit>
#include <QLineEdit>

/// 公式计算算子 — 表达式求值，支持 p0/p1... 引用输入端口数值
/// 对应 VM 4.4 的「公式计算」工具
class FormulaNode : public HalconNode
{
    Q_OBJECT
public:
    explicit FormulaNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool process() override;
    void setParam(const QString &name, const QVariant &value) override;
    QVariant getParam(const QString &name) const override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

    /// 对指定表达式求值（供测试/调试），失败时返回 false
    static bool evaluate(const QString &expression,
                         const QMap<QString, double> &variables,
                         double &result);

private:
    QString m_expression = QStringLiteral("p0 + p1");

    QTextEdit *m_expressionEdit = nullptr;
};
