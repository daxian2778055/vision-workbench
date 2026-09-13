#pragma once

#include "HalconNode.h"
#include <QLineEdit>
#include <QTextEdit>

/// 格式化算子 — 将多个输入拼装为格式化字符串
/// 对应 VM 4.4 的「格式化」工具
class FormatNode : public HalconNode
{
    Q_OBJECT
public:
    explicit FormatNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool process() override;
    void setParam(const QString &name, const QVariant &value) override;
    QVariant getParam(const QString &name) const override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private:
    QString m_template;      /// 格式模板，如 "OK,{x:.2f},{y:.2f},{z}\r\n"
    QString m_outputSuffix;  /// 输出后缀（默认 \r\n）

    QTextEdit *m_templateEdit = nullptr;
};
