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
    // S1 残留收口（第二批）：flowName / nodeName / passed 的唯一来源是参数表（默认值在 init() 写入），
    // 不再保留无锁成员镜像——run() 在执行线程读它们，界面线程会写。
    // 注：passed 是"要记录成什么结果"的**输入型参数**（界面复选框设置），不是执行产出的结果，
    // 故与 imageWidth 那类"结果回读"不同，属本批收口范围。

    QLineEdit *m_flowNameEdit = nullptr;
    QLineEdit *m_nodeNameEdit = nullptr;
    QCheckBox *m_passedCheck = nullptr;
};
