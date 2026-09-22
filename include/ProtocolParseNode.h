#pragma once

#include "HalconNode.h"
#include <QComboBox>
#include <QTableWidget>
#include <QLineEdit>

/// 字段定义：协议解析后输出的每个字段
struct ParseFieldDef {
    QString name;     /// 字段名
    QString type;     /// "int", "float", "string"
    int index = 0;    /// 在分隔符数组中的索引
};

/// 协议解析算子 — 将接收到的字符串按分隔符拆分并输出 JSON 字符串
/// 输入: String（原始数据）
/// 输出: String（JSON 格式，如 {"X":123.45,"str":"OK"}）
/// 对应 VM 4.4 的「协议解析」工具
class ProtocolParseNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ProtocolParseNode(QObject *parent = nullptr);

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
    void refreshFieldTable();
    void rebuildFieldsFromTable();

    // S1 残留收口：delimiter 与字段定义列表（fieldDefs）的唯一来源都是参数表，不再保留无锁成员镜像。
    // 此前界面线程点"应用"会 clear()/append() 这个列表，而执行线程正在迭代它 —— 属"容器迭代中被改"
    // 的崩溃面（比标量撕裂更硬）。列表在参数表里以 QVariantList<QVariantMap{name,type,index}> 存放
    // （本仓首个列表型参数；基类 setParam 对未声明范围的键原样入库，JSON 往返原生支持该类型），
    // 读取侧拿到的是值拷贝，天然免疫就地修改。

    QLineEdit *m_delimiterEdit = nullptr;
    QTableWidget *m_fieldTable = nullptr;
};
