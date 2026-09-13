#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QDateTimeEdit>
#include <QPushButton>
#include <QWidget>
#include <QList>

/// 检测结果统计报表与图表 — 按流程统计通过/失败数量，并绘制柱状图
class ReportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ReportDialog(QWidget *parent = nullptr);
    ~ReportDialog() override;

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onQuery();
    void onExport();

private:
    void setupUI();
    void refreshStats();

    struct FlowStat {
        QString flowName;
        int total = 0;
        int passed = 0;
        int failed = 0;
    };
    QList<FlowStat> m_stats;

    QTableWidget *m_statTable = nullptr;
    QDateTimeEdit *m_fromDateTime = nullptr;
    QDateTimeEdit *m_toDateTime = nullptr;
    QPushButton *m_queryBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;
};
