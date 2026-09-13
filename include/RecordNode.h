#pragma once

#include "HalconNode.h"
#include <QLineEdit>
#include <QCheckBox>

/// 数据记录算子 — 将检测结果写入数据库（AppDatabase）
/// 对应 VM 4.4 的「数据记录」工具
class RecordNode : public HalconNode
{
    Q_OBJECT
public:
    explicit RecordNode(QObject *parent = nullptr);

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
    QString m_flowName = QStringLiteral("流程 1");
    QString m_nodeName = QStringLiteral("数据记录");
    bool m_passed = true;

    QLineEdit *m_flowNameEdit = nullptr;
    QLineEdit *m_nodeNameEdit = nullptr;
    QCheckBox *m_passedCheck = nullptr;
};
