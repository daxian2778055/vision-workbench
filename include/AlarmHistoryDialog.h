#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QDateTimeEdit>
#include <QPushButton>
#include <QComboBox>

class AlarmHistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AlarmHistoryDialog(QWidget *parent = nullptr);
    ~AlarmHistoryDialog();

private slots:
    void onQuery();
    void onClear();

private:
    void setupUI();
    void refreshAlarms();

    QTableWidget *m_alarmTable;
    QDateTimeEdit *m_fromDateTime;
    QDateTimeEdit *m_toDateTime;
    QPushButton *m_queryButton;
    QPushButton *m_clearButton;
    QPushButton *m_closeButton;
};
