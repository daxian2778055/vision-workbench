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
    /// 必填数据端口 = 表达式**实际引用到**的 pN（W-2 的唯一"按配置逐端口判定"覆写）。
    /// p0~p3 是可选操作数：`2 * 3` 不接输入也合法；`p0 + 1` 没接 p0 就必须判失败，
    /// 而不是拿 getInputData 缺失时的 0.0 算出一个看起来正常的数。
    QSet<int> requiredInputDataPorts() const override;
    void setParam(const QString &name, const QVariant &value) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

    /// 对指定表达式求值（供测试/调试），失败时返回 false
    static bool evaluate(const QString &expression,
                         const QMap<QString, double> &variables,
                         double &result);

private:
    // S1 残留收口：expression 的唯一来源是参数表（默认值在 init() 写入），不再保留无锁成员镜像。
    // 本类此前还重写 getParam 直接返回该成员——收口后该重写已无意义，一并删除（直接走基类）。

    QTextEdit *m_expressionEdit = nullptr;
};
