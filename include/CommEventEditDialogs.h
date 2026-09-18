#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTableWidget;
class ReceiveEvent;
class SendEvent;

/// 接收事件编辑对话框：所有参数可直接修改（此前"编辑"只能启停，改参数必须删了重建）
/// - 文本-协议解析：解析模式（分隔符/正则）+ 对应参数
/// - 字节匹配：规则表格（偏移/长度/类型/字节序/比较值/条件）可增删改
class ReceiveEventEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ReceiveEventEditDialog(ReceiveEvent *event, QWidget *parent = nullptr);

private slots:
    void onAccept();
    void onAddRule();
    void onRemoveRule();
    void onModeChanged();

private:
    void loadFromEvent();
    void loadRulesTable();

    ReceiveEvent *m_event = nullptr;
    QCheckBox *m_enabledCheck = nullptr;
    // 文本-协议解析
    QComboBox *m_modeCombo = nullptr;      /// 分隔符拆分 / 正则表达式
    QLineEdit *m_delimiterEdit = nullptr;
    QLineEdit *m_regexEdit = nullptr;
    // 字节匹配
    QTableWidget *m_rulesTable = nullptr;
};

/// 发送事件编辑对话框：
/// - 文本-直接输出：模板（支持 {模块号.参数名} / {global.变量名} 占位符）+ 后缀
/// - 字节组包：字段表格（偏移/长度/类型/固定值）可增删改
class SendEventEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SendEventEditDialog(SendEvent *event, QWidget *parent = nullptr);

private slots:
    void onAccept();
    void onAddField();
    void onRemoveField();

private:
    void loadFromEvent();
    void loadFieldsTable();

    SendEvent *m_event = nullptr;
    QCheckBox *m_enabledCheck = nullptr;
    // 文本-直接输出
    QLineEdit *m_templateEdit = nullptr;
    QLineEdit *m_suffixEdit = nullptr;
    // 字节组包
    QTableWidget *m_fieldsTable = nullptr;
};
