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

    QString m_delimiter = QStringLiteral(",");
    QList<ParseFieldDef> m_fields;

    QLineEdit *m_delimiterEdit = nullptr;
    QTableWidget *m_fieldTable = nullptr;
};
