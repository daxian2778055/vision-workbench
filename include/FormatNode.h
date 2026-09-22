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
    // S1 残留收口：template / outputSuffix 的唯一来源是参数表（默认值在 init() 写入），
    // 不再保留无锁成员镜像——此前 setParam 写成员、run() 读成员，与界面线程写参数构成无保护竞态。

    QTextEdit *m_templateEdit = nullptr;
};
