#include "StatisticsReportDialog.h"

#include "AppDatabase.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QDateTime>
#include <QSet>
#include <QPainter>
#include <QPixmap>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QHorizontalBarSeries>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>

namespace {

/// 一次最多取多少条记录：报表是"看一眼"的场景，但 30 天连续运行可能上百万条，
/// 所以给一个明确上限并把实际条数显示出来（避免 silently 只统计了一部分）。
constexpr int kMaxRecords = 200000;

QChartView *makeChartView(QChart *chart)
{
    auto *view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing, true);
    view->setMinimumHeight(220);
    return view;
}

} // namespace

StatisticsReportDialog::StatisticsReportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("统计报表"));
    setMinimumSize(880, 640);

    auto *root = new QVBoxLayout(this);

    // ── 顶部：范围 / 流程过滤 / 刷新 ──
    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel(QStringLiteral("时间范围:"), this));
    m_rangeCombo = new QComboBox(this);
    m_rangeCombo->addItems({ QStringLiteral("今天"), QStringLiteral("近 7 天"),
                             QStringLiteral("近 30 天"), QStringLiteral("全部") });
    m_rangeCombo->setCurrentIndex(1);
    top->addWidget(m_rangeCombo);

    top->addSpacing(12);
    top->addWidget(new QLabel(QStringLiteral("流程:"), this));
    m_flowCombo = new QComboBox(this);
    m_flowCombo->setMinimumWidth(180);
    top->addWidget(m_flowCombo);

    top->addStretch(1);
    auto *refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    top->addWidget(refreshBtn);
    root->addLayout(top);

    // ── KPI ──
    m_kpiLabel = new QLabel(this);
    m_kpiLabel->setTextFormat(Qt::RichText);
    m_kpiLabel->setWordWrap(true);
    root->addWidget(m_kpiLabel);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setStyleSheet(QStringLiteral("color:#a60"));
    m_hintLabel->setWordWrap(true);
    root->addWidget(m_hintLabel);

    // ── 图表 ×2 ──
    root->addWidget(new QLabel(QStringLiteral("按天趋势（OK / NG 轮次）"), this));
    auto *trendChart = new QChart();
    m_trendView = makeChartView(trendChart);
    root->addWidget(m_trendView, 1);

    root->addWidget(new QLabel(QStringLiteral("NG 按节点分布（Top 10）"), this));
    auto *ngChart = new QChart();
    m_ngView = makeChartView(ngChart);
    root->addWidget(m_ngView, 1);

    // ── 导出 ──
    auto *bottom = new QHBoxLayout();
    auto *csvBtn = new QPushButton(QStringLiteral("导出 CSV"), this);
    auto *htmlBtn = new QPushButton(QStringLiteral("导出 HTML 报告"), this);
    auto *pngBtn = new QPushButton(QStringLiteral("导出图表 PNG"), this);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    bottom->addWidget(csvBtn);
    bottom->addWidget(htmlBtn);
    bottom->addWidget(pngBtn);
    bottom->addStretch(1);
    bottom->addWidget(closeBtn);
    root->addLayout(bottom);

    connect(refreshBtn, &QPushButton::clicked, this, &StatisticsReportDialog::reload);
    connect(m_rangeCombo, &QComboBox::currentTextChanged, this, &StatisticsReportDialog::reload);
    connect(m_flowCombo, &QComboBox::currentTextChanged, this, &StatisticsReportDialog::reload);
    connect(csvBtn, &QPushButton::clicked, this, &StatisticsReportDialog::exportCsv);
    connect(htmlBtn, &QPushButton::clicked, this, &StatisticsReportDialog::exportHtml);
    connect(pngBtn, &QPushButton::clicked, this, &StatisticsReportDialog::exportChartPng);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    reload();
}

QDateTime StatisticsReportDialog::rangeStart() const
{
    const QDate today = QDate::currentDate();
    switch (m_rangeCombo->currentIndex()) {
    case 0: return QDateTime(today, QTime(0, 0, 0));                       // 今天
    case 1: return QDateTime(today.addDays(-6), QTime(0, 0, 0));           // 近 7 天（含今天）
    case 2: return QDateTime(today.addDays(-29), QTime(0, 0, 0));          // 近 30 天
    default: return QDateTime();                                           // 全部
    }
}

QString StatisticsReportDialog::currentFlowFilter() const
{
    const QString t = m_flowCombo->currentText();
    if (t.isEmpty() || t == QStringLiteral("全部流程"))
        return QString();
    return t;
}

void StatisticsReportDialog::reload()
{
    m_lastError.clear();

    AppDatabase *db = AppDatabase::instance();
    if (db->databasePath().isEmpty()) {
        m_lastError = QStringLiteral("数据库未启用（数据库路径为空）——连续运行前请先在设置中指定数据库，"
                                     "否则没有可统计的记录。");
    }

    const QDateTime from = rangeStart();
    const QDateTime to = QDateTime::currentDateTime();
    m_records = db->queryResults(from, to, kMaxRecords);
    if (m_records.size() >= kMaxRecords) {
        m_hintLabel->setText(QStringLiteral("⚠ 记录数达到上限 %1 条，报表只统计了最近 %1 条；"
                                            "请缩小时间范围或缩短数据保留天数。").arg(kMaxRecords));
    }

    // 流程下拉：保留用户当前选择（重载后不要跳回"全部"）
    const QString keep = currentFlowFilter();
    QStringList flows;
    for (const InspectionRecord &r : m_records) {
        if (!flows.contains(r.flowName))
            flows << r.flowName;
    }
    flows.sort();
    m_flowCombo->blockSignals(true);
    m_flowCombo->clear();
    m_flowCombo->addItem(QStringLiteral("全部流程"));
    m_flowCombo->addItems(flows);
    if (!keep.isEmpty() && flows.contains(keep))
        m_flowCombo->setCurrentText(keep);
    m_flowCombo->blockSignals(false);

    m_summary = StatisticsReport::compute(m_records, currentFlowFilter());
    updateCharts();

    QString kpi = QStringLiteral("<b>总记录</b> %1 &nbsp; <b>NG 记录</b> %2 &nbsp; ")
                      .arg(m_summary.totalRecords)
                      .arg(m_summary.ngRecords);
    if (m_summary.hasRounds) {
        kpi += QStringLiteral("<b>总轮次</b> %1 &nbsp; <b>OK/NG 轮次</b> %2 / %3 &nbsp; "
                              "<b>良率</b> <span style=\"color:#2b7;font-size:15px\">%4%</span>")
                   .arg(m_summary.totalRounds)
                   .arg(m_summary.okRounds)
                   .arg(m_summary.ngRounds)
                   .arg(m_summary.yieldPercent, 0, 'f', 2);
    } else {
        kpi += QStringLiteral("<b>总轮次</b> 0 &nbsp; <b>良率</b> <span style=\"color:#a60\">无轮次数据</span>");
    }
    if (m_summary.firstSeen.isValid()) {
        kpi += QStringLiteral("<br/><span style=\"color:#666\">数据区间：%1 ~ %2</span>")
                   .arg(m_summary.firstSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                        m_summary.lastSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    }
    m_kpiLabel->setText(kpi);

    QString hint;
    if (!m_lastError.isEmpty())
        hint = QStringLiteral("⚠ ") + m_lastError;
    else if (!m_summary.hasRounds)
        hint = QStringLiteral("本范围内没有「整轮汇总」记录，因此**无法计算良率**（显示为无轮次数据）。"
                              "该记录由执行器在轮末写入，升级后的新数据才会有；节点 NG 分布仍可用。");
    m_hintLabel->setText(hint);
}

void StatisticsReportDialog::updateCharts()
{
    // ── 按天趋势：堆叠柱（OK / NG）──
    {
        auto *chart = m_trendView->chart();
        chart->removeAllSeries();
        for (QAbstractAxis *ax : chart->axes())
            chart->removeAxis(ax);

        auto *okSet = new QBarSet(QStringLiteral("OK 轮次"));
        auto *ngSet = new QBarSet(QStringLiteral("NG 轮次"));
        okSet->setColor(QColor(0x2b, 0xb7, 0x77));
        ngSet->setColor(QColor(0xd9, 0x53, 0x4f));

        QStringList categories;
        int maxValue = 1;
        // 天数很多时只画最近 31 天（否则柱子细到看不清）
        const int total = m_summary.days.size();
        const int begin = qMax(0, total - 31);
        for (int i = begin; i < total; ++i) {
            const StatisticsReport::DayBucket &d = m_summary.days.at(i);
            *okSet << d.okRounds;
            *ngSet << d.ngRounds;
            categories << d.date.toString(QStringLiteral("MM-dd"));
            maxValue = qMax(maxValue, d.okRounds + d.ngRounds);
        }

        if (categories.isEmpty()) {
            chart->setTitle(QStringLiteral("（范围内没有带有效时间戳的记录）"));
        } else {
            auto *series = new QBarSeries();
            series->append(okSet);
            series->append(ngSet);
            series->setLabelsVisible(false);
            chart->addSeries(series);
            chart->setTitle(QStringLiteral("按天轮次（最近 %1 天）").arg(categories.size()));

            auto *axisX = new QBarCategoryAxis();
            axisX->append(categories);
            chart->addAxis(axisX, Qt::AlignBottom);
            series->attachAxis(axisX);

            auto *axisY = new QValueAxis();
            axisY->setRange(0, maxValue);
            axisY->setLabelFormat(QStringLiteral("%d"));
            chart->addAxis(axisY, Qt::AlignLeft);
            series->attachAxis(axisY);
        }
        chart->legend()->setVisible(true);
    }

    // ── NG 按节点：横向条（Top 10）──
    {
        auto *chart = m_ngView->chart();
        chart->removeAllSeries();
        for (QAbstractAxis *ax : chart->axes())
            chart->removeAxis(ax);

        auto *set = new QBarSet(QStringLiteral("NG 次数"));
        set->setColor(QColor(0xd9, 0x53, 0x4f));
        QStringList categories;
        int maxValue = 1;
        int shown = 0;
        for (const auto &p : m_summary.ngByNode) {
            if (++shown > 10)
                break;
            *set << p.second;
            categories << p.first;
            maxValue = qMax(maxValue, p.second);
        }

        if (categories.isEmpty()) {
            chart->setTitle(QStringLiteral("（范围内没有 NG 节点）"));
        } else {
            auto *series = new QHorizontalBarSeries();
            series->append(set);
            chart->addSeries(series);
            chart->setTitle(QStringLiteral("NG 次数（按节点，Top %1）").arg(categories.size()));

            auto *axisY = new QBarCategoryAxis();
            axisY->append(categories);
            chart->addAxis(axisY, Qt::AlignLeft);
            series->attachAxis(axisY);

            auto *axisX = new QValueAxis();
            axisX->setRange(0, maxValue);
            axisX->setLabelFormat(QStringLiteral("%d"));
            chart->addAxis(axisX, Qt::AlignBottom);
            series->attachAxis(axisX);
        }
        chart->legend()->setVisible(false);
    }
}

void StatisticsReportDialog::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 CSV"),
        StatisticsReport::suggestedFileName(QStringLiteral("csv")), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("无法写入 %1：%2").arg(path, f.errorString()));
        return;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << StatisticsReport::toCsv(m_summary);
    ts.flush();
    QMessageBox::information(this, QStringLiteral("导出完成"), QStringLiteral("已导出：\n%1").arg(path));
}

void StatisticsReportDialog::exportHtml()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 HTML 报告"),
        StatisticsReport::suggestedFileName(QStringLiteral("html")), QStringLiteral("HTML (*.html)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("无法写入 %1：%2").arg(path, f.errorString()));
        return;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    const QString title = currentFlowFilter().isEmpty()
                              ? QStringLiteral("VisionFlowPlatform 统计报表")
                              : QStringLiteral("VisionFlowPlatform 统计报表 - %1").arg(currentFlowFilter());
    ts << StatisticsReport::toHtml(m_summary, title);
    ts.flush();
    QMessageBox::information(this, QStringLiteral("导出完成"),
                             QStringLiteral("已导出（自包含 HTML，离线可看）：\n%1").arg(path));
}

void StatisticsReportDialog::exportChartPng()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出图表 PNG"),
        StatisticsReport::suggestedFileName(QStringLiteral("png")), QStringLiteral("PNG (*.png)"));
    if (path.isEmpty())
        return;
    // 两张图上下拼一张，便于直接贴进报告
    const QPixmap top = m_trendView->grab();
    const QPixmap bottom = m_ngView->grab();
    QPixmap combined(top.width(), top.height() + bottom.height());
    combined.fill(Qt::white);
    {
        QPainter p(&combined);
        p.drawPixmap(0, 0, top);
        p.drawPixmap(0, top.height(), bottom);
    }
    if (!combined.save(path, "PNG")) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入 %1").arg(path));
        return;
    }
    QMessageBox::information(this, QStringLiteral("导出完成"), QStringLiteral("已导出：\n%1").arg(path));
}
