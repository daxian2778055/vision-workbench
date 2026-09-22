#pragma once

#include "HalconNode.h"
#include <QComboBox>

/// 数据排序算子 — 对输入数组（Array）按升序/降序排列输出
/// 对应 VM 4.4 的「数据排序」工具
class SortNode : public HalconNode
{
    Q_OBJECT
public:
    explicit SortNode(QObject *parent = nullptr);

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
    // S1 残留收口：排序方式唯一来源是参数表（默认值在 init() 写入），不再保留无锁成员镜像
    QComboBox *m_orderCombo = nullptr;
};
