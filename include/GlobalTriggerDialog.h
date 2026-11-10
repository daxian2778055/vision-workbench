#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>

/// 全局触发配置对话框 — 配置触发源到流程的映射表
class GlobalTriggerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GlobalTriggerDialog(QWidget *parent = nullptr);
    ~GlobalTriggerDialog() override;

public slots:
    /// 外部导入/重载触发配置后刷新表（gtd 导入用）
    void refreshTriggerTable();

private slots:
    void onAddStringTrigger();
    void onAddEventTrigger();
    void onRemoveTrigger();
    void onApply();

private:
    void setupUI();

    QTableWidget *m_triggerTable;
    QPushButton *m_addStringBtn;
    QPushButton *m_addEventBtn;
    QPushButton *m_removeBtn;
    QPushButton *m_closeBtn;
    QLabel *m_infoLabel;
};
