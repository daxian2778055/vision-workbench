#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QDateTimeEdit>
#include <QPushButton>
#include <QLineEdit>

/// 操作日志查询界面 — 查询用户操作日志（op_logs 表）
class OperationLogDialog : public QDialog
{
    Q_OBJECT
public:
    explicit OperationLogDialog(QWidget *parent = nullptr);
    ~OperationLogDialog() override;

private slots:
    void onQuery();
    void onClear();
    void onExport();   // AE1：把当前查询结果导出为 CSV

private:
    void setupUI();
    void refreshLogs();

    QTableWidget *m_logTable;
    QDateTimeEdit *m_fromDateTime;
    QDateTimeEdit *m_toDateTime;
    QLineEdit *m_userFilter;
    QPushButton *m_queryButton;
    QPushButton *m_clearButton;
    QPushButton *m_exportButton;
    QPushButton *m_closeButton;
};
