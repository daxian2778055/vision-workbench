#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QDateTimeEdit>
#include <QPushButton>
#include <QLineEdit>

class InspectionResultDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InspectionResultDialog(QWidget *parent = nullptr);
    ~InspectionResultDialog();

private slots:
    void onQuery();
    void onExport();

private:
    void setupUI();
    void refreshResults();

    QTableWidget *m_resultTable;
    QDateTimeEdit *m_fromDateTime;
    QDateTimeEdit *m_toDateTime;
    QLineEdit *m_flowFilter;
    QPushButton *m_queryButton;
    QPushButton *m_exportButton;
    QPushButton *m_closeButton;
};
