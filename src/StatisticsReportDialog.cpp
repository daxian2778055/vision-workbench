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
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSettings>

#include "ReportAutoExport.h"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QHorizontalBarSeries>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QLineSeries>
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

    // 时间序列粒度：默认"自动"——今天/近 2 天看小时级（按天会把"下午开始连续 NG"这类事抹平），
    // 更长范围看天级（小时级会堆成几百个柱子，什么也看不出来）
    top->addSpacing(12);
    top->addWidget(new QLabel(QStringLiteral("粒度:"), this));
    m_granCombo = new QComboBox(this);
    m_granCombo->addItems({ QStringLiteral("自动"), QStringLiteral("按小时"), QStringLiteral("按天") });
    m_granCombo->setToolTip(QStringLiteral("时间序列的分桶粒度；导出的 CSV / HTML 按同一粒度分段"));
    top->addWidget(m_granCombo);

    // 良率目标（P1-11）：0 = 未设目标（不画目标线、不判定达标、运行期也不报警）。
    // 与"运行期报警联动"共用同一个配置键，改了立刻对两边生效。
    top->addSpacing(12);
    top->addWidget(new QLabel(QStringLiteral("良率目标(%):"), this));
    m_targetSpin = new QDoubleSpinBox(this);
    m_targetSpin->setRange(0.0, 100.0);
    m_targetSpin->setDecimals(1);
    m_targetSpin->setSingleStep(0.5);
    m_targetSpin->setSpecialValueText(QStringLiteral("未设目标"));   // 取最小值(0)时显示为文字
    m_targetSpin->setToolTip(QStringLiteral("设为 0 表示不设目标：不画目标线、不判定达标，运行期也不报警。\n"
                                            "该值同时用于运行期报警（连续 N 轮良率低于目标时报警）"));
    m_targetSpin->setValue(QSettings().value(QStringLiteral("reporting/yieldTargetPercent"), 0.0).toDouble());
    top->addWidget(m_targetSpin);

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

    // ── 图表 ×3 ──
    // 标题随粒度变化（"按小时/按天"），故用成员指针在 updateCharts 里更新
    m_trendTitle = new QLabel(QStringLiteral("趋势（OK / NG 轮次）"), this);
    root->addWidget(m_trendTitle);
    auto *trendChart = new QChart();
    m_trendView = makeChartView(trendChart);
    root->addWidget(m_trendView, 1);

    // 良率趋势单独一张图：上面那张是"轮次计数"，百分比目标线画在计数轴上没有意义
    m_yieldTitle = new QLabel(QStringLiteral("良率（%）与目标线"), this);
    root->addWidget(m_yieldTitle);
    auto *yieldChart = new QChart();
    m_yieldView = makeChartView(yieldChart);
    root->addWidget(m_yieldView, 1);

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

    // ── 定时导出（P1-11 收尾）：策略与落盘都在 ReportAutoExport（可单测），这里只写配置 ──
    auto *autoRow = new QHBoxLayout();
    const ReportAutoExport::Config autoCfg = ReportAutoExport::loadConfig();
    m_autoExportCheck = new QCheckBox(QStringLiteral("定时导出"), this);
    m_autoExportCheck->setChecked(autoCfg.enabled);
    m_autoExportCheck->setToolTip(QStringLiteral("按间隔自动把统计报表写成 CSV/HTML 到指定目录（默认关闭）。\n"
                                                 "目录里只保留最近若干份，避免无人值守机把磁盘写满。"));
    autoRow->addWidget(m_autoExportCheck);
    autoRow->addWidget(new QLabel(QStringLiteral("间隔(分钟):"), this));
    m_autoExportInterval = new QSpinBox(this);
    m_autoExportInterval->setRange(1, 100000);
    m_autoExportInterval->setValue(autoCfg.intervalMinutes);
    m_autoExportInterval->setEnabled(autoCfg.enabled);
    autoRow->addWidget(m_autoExportInterval);
    m_autoExportDirLabel = new QLabel(this);
    m_autoExportDirLabel->setText(QStringLiteral("目录：%1").arg(autoCfg.effectiveDir()));
    m_autoExportDirLabel->setStyleSheet(QStringLiteral("color:#666"));
    autoRow->addWidget(m_autoExportDirLabel);
    autoRow->addStretch(1);
    auto *autoNowBtn = new QPushButton(QStringLiteral("立即导出"), this);
    autoNowBtn->setToolTip(QStringLiteral("按上面的设置立刻导出一份（与定时导出走同一条落盘与保留策略）"));
    autoRow->addWidget(autoNowBtn);
    root->addLayout(autoRow);

    // 粒度切换要重新取数（换粒度是重新分桶，不是重画）
    connect(m_granCombo, &QComboBox::currentTextChanged, this, &StatisticsReportDialog::reload);
    connect(m_autoExportCheck, &QCheckBox::toggled, this,
            &StatisticsReportDialog::saveAutoExportSettings);
    connect(m_autoExportInterval, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &StatisticsReportDialog::saveAutoExportSettings);
    connect(autoNowBtn, &QPushButton::clicked, this, &StatisticsReportDialog::exportScheduledNow);

    connect(refreshBtn, &QPushButton::clicked, this, &StatisticsReportDialog::reload);
    connect(m_rangeCombo, &QComboBox::currentTextChanged, this, &StatisticsReportDialog::reload);
    connect(m_flowCombo, &QComboBox::currentTextChanged, this, &StatisticsReportDialog::reload);
    // 改目标只影响目标线/达标结论，不需要重新查库（30 天数据可能几十万条，能省就省）
    connect(m_targetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        saveTargetSetting();
        updateCharts();
        updateKpiAndHint();
    });
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

    m_summary = StatisticsReport::compute(m_records, currentFlowFilter(), currentGranularity());
    updateCharts();
    updateKpiAndHint();
}

StatisticsReport::Granularity StatisticsReportDialog::currentGranularity() const
{
    const QString text = m_granCombo ? m_granCombo->currentText() : QString();
    if (text == QStringLiteral("按小时"))
        return StatisticsReport::Granularity::ByHour;
    if (text == QStringLiteral("按天"))
        return StatisticsReport::Granularity::ByDay;

    // 自动：≤2 天按小时（"今天/昨晚几点开始坏"这类排障必须看小时级），更长范围按天
    const QDateTime start = rangeStart();
    if (!start.isValid())
        return StatisticsReport::Granularity::ByDay;
    const qint64 days = start.daysTo(QDateTime::currentDateTime());
    return (days >= 0 && days <= 2) ? StatisticsReport::Granularity::ByHour
                                    : StatisticsReport::Granularity::ByDay;
}

void StatisticsReportDialog::saveAutoExportSettings()
{
    ReportAutoExport::Config cfg = ReportAutoExport::loadConfig();
    cfg.enabled = m_autoExportCheck && m_autoExportCheck->isChecked();
    if (m_autoExportInterval)
        cfg.intervalMinutes = m_autoExportInterval->value();
    if (cfg.dir.trimmed().isEmpty())
        cfg.dir = ReportAutoExport::defaultDir();   // 把默认目录固化进配置：用户能看到实际会写到哪
    ReportAutoExport::saveConfig(cfg);

    if (m_autoExportInterval)
        m_autoExportInterval->setEnabled(cfg.enabled);
    if (m_autoExportDirLabel)
        m_autoExportDirLabel->setText(QStringLiteral("目录：%1").arg(cfg.effectiveDir()));
    // 运行期每分钟重新读配置（见 MainWindow::reportAutoExportTick），故这里改完**无需重启**
}

void StatisticsReportDialog::exportScheduledNow()
{
    // 「立即导出」与定时导出走**同一条**落盘与保留路径：两条路径分叉的话，现场就会出现
    // "我立刻导出的文件跟机器自己导的不一样"，而且没人能说清哪个对。
    ReportAutoExport::Config cfg = ReportAutoExport::loadConfig();
    if (!cfg.isValid()) {
        QMessageBox::warning(this, QStringLiteral("立即导出"),
                             QStringLiteral("导出配置非法：间隔与保留份数需为正数，且至少勾选一种格式。"));
        return;
    }

    QStringList written;
    QString error;
    const QDateTime now = QDateTime::currentDateTime();
    if (!ReportAutoExport::exportNow(cfg, m_summary, QString(), targetPercent(), now, &written, &error)) {
        QMessageBox::warning(this, QStringLiteral("立即导出"), error);
        return;
    }
    const int removed = ReportAutoExport::pruneOldFiles(cfg.effectiveDir(), cfg.keepFiles);
    // 手动导一次也算"导出过"：否则刚点完立刻又被定时任务重复导一份
    ReportAutoExport::setLastExportMs(now.toMSecsSinceEpoch());

    QMessageBox::information(this, QStringLiteral("立即导出"),
                             QStringLiteral("已导出 %1 个文件到：\n%2\n清理旧报告 %3 个")
                                 .arg(written.size())
                                 .arg(cfg.effectiveDir())
                                 .arg(removed));
}

double StatisticsReportDialog::targetPercent() const
{
    if (!m_targetSpin)
        return 0.0;
    const double v = m_targetSpin->value();
    return (v > 0.0 && v <= 100.0) ? v : 0.0;   // 0 = 未设目标
}

void StatisticsReportDialog::saveTargetSetting()
{
    QSettings().setValue(QStringLiteral("reporting/yieldTargetPercent"), targetPercent());
}

void StatisticsReportDialog::updateKpiAndHint()
{
    const double target = targetPercent();

    QString kpi = QStringLiteral("<b>总记录</b> %1 &nbsp; <b>NG 记录</b> %2 &nbsp; ")
                      .arg(m_summary.totalRecords)
                      .arg(m_summary.ngRecords);
    if (m_summary.hasRounds) {
        const bool meets = StatisticsReport::meetsTarget(m_summary, target);
        // 未达标标红：现场扫一眼就要能看出结果，而不是自己去和目标做心算
        const QString color = (target > 0.0 && !meets) ? QStringLiteral("#c33") : QStringLiteral("#2b7");
        kpi += QStringLiteral("<b>总轮次</b> %1 &nbsp; <b>OK/NG 轮次</b> %2 / %3 &nbsp; "
                              "<b>良率</b> <span style=\"color:%4;font-size:15px\">%5%</span>")
                   .arg(m_summary.totalRounds)
                   .arg(m_summary.okRounds)
                   .arg(m_summary.ngRounds)
                   .arg(color)
                   .arg(m_summary.yieldPercent, 0, 'f', 2);
        if (target > 0.0) {
            kpi += QStringLiteral(" &nbsp; <b>目标</b> %1% <span style=\"color:%2\"><b>%3</b></span>")
                       .arg(target, 0, 'f', 1)
                       .arg(color)
                       .arg(meets ? QStringLiteral("达标") : QStringLiteral("未达标"));
        }
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
    if (!m_lastError.isEmpty()) {
        hint = QStringLiteral("⚠ ") + m_lastError;
    } else if (!m_summary.hasRounds) {
        hint = QStringLiteral("本范围内没有「整轮汇总」记录，因此**无法计算良率**（显示为无轮次数据）。"
                              "该记录由执行器在轮末写入，升级后的新数据才会有；节点 NG 分布仍可用。");
    } else if (target > 0.0 && !StatisticsReport::meetsTarget(m_summary, target)) {
        hint = QStringLiteral("⚠ 当前良率未达目标 %1%：运行期会在**最近 N 轮**良率低于目标时报警"
                              "（窗口与开关见下方说明；报警记录可在「系统 → 报警历史」查看）。")
                   .arg(target, 0, 'f', 1);
    } else if (target > 0.0) {
        hint = QStringLiteral("良率达标（目标 %1%）。").arg(target, 0, 'f', 1);
    }
    m_hintLabel->setText(hint);
}

void StatisticsReportDialog::updateCharts()
{
    // ── 趋势：堆叠柱（OK / NG）──
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
        // 桶很多时只画最近 48 个（柱子细到看不清就失去意义；导出文件里仍是全部）
        const int total = m_summary.buckets.size();
        const int begin = qMax(0, total - 48);
        for (int i = begin; i < total; ++i) {
            const StatisticsReport::Bucket &b = m_summary.buckets.at(i);
            *okSet << b.okRounds;
            *ngSet << b.ngRounds;
            categories << m_summary.labelOf(b, true);
            maxValue = qMax(maxValue, b.okRounds + b.ngRounds);
        }

        const QString unit = m_summary.granularityName();
        if (m_trendTitle) {
            m_trendTitle->setText(QStringLiteral("趋势（OK / NG 轮次，按%1；最近 %2 个时段）")
                                      .arg(unit)
                                      .arg(categories.size()));
        }
        if (categories.isEmpty()) {
            chart->setTitle(QStringLiteral("（范围内没有带有效时间戳的记录）"));
        } else {
            auto *series = new QBarSeries();
            series->append(okSet);
            series->append(ngSet);
            series->setLabelsVisible(false);
            chart->addSeries(series);
            chart->setTitle(QStringLiteral("按%1轮次（最近 %2 个时段）").arg(unit).arg(categories.size()));

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

    // ── 良率（%）+ 目标线 ──
    // 单独一张图：上面那张是"轮次计数"，百分比目标线画在计数轴上没有意义。
    {
        auto *chart = m_yieldView->chart();
        chart->removeAllSeries();
        for (QAbstractAxis *ax : chart->axes())
            chart->removeAxis(ax);

        const double target = targetPercent();
        const QString unit = m_summary.granularityName();
        const int total = m_summary.buckets.size();
        const int begin = qMax(0, total - 48);

        auto *yieldSeries = new QLineSeries();
        yieldSeries->setName(QStringLiteral("良率(%)"));
        yieldSeries->setColor(QColor(0x2b, 0x7f, 0xd0));

        int plotted = 0;
        const int labelStep = qMax(1, (total - begin) / 12);   // 最多约 12 个标签，别挤成一团
        auto *axisX = new QCategoryAxis();
        for (int i = begin; i < total; ++i) {
            const StatisticsReport::Bucket &b = m_summary.buckets.at(i);
            const int rounds = b.okRounds + b.ngRounds;
            if (rounds <= 0)
                continue;   // 该时段没有轮次：不画点（不把"无数据"画成 0%）
            const double y = 100.0 * double(b.okRounds) / double(rounds);
            yieldSeries->append(plotted + 0.5, y);
            if (plotted % labelStep == 0)
                axisX->append(m_summary.labelOf(b, true), plotted + 1.0);
            ++plotted;
        }

        if (m_yieldTitle) {
            m_yieldTitle->setText(QStringLiteral("良率（%）与目标线，按%1；最近 %2 个时段）")
                                      .arg(unit)
                                      .arg(plotted));
        }
        if (plotted == 0) {
            chart->setTitle(QStringLiteral("（范围内没有可计算良率的时段：需要「整轮汇总」记录）"));
        } else {
            chart->addSeries(yieldSeries);
            axisX->setRange(0, double(plotted));
            chart->addAxis(axisX, Qt::AlignBottom);
            yieldSeries->attachAxis(axisX);

            auto *axisY = new QValueAxis();
            axisY->setRange(0, 100);   // 良率天然 0..100：固定量程，跨报表可比
            axisY->setLabelFormat(QStringLiteral("%d"));
            axisY->setTitleText(QStringLiteral("良率 %"));
            chart->addAxis(axisY, Qt::AlignLeft);
            yieldSeries->attachAxis(axisY);

            if (target > 0.0) {
                auto *targetSeries = new QLineSeries();
                targetSeries->setName(QStringLiteral("目标 %1%").arg(target, 0, 'f', 1));
                targetSeries->append(0.0, target);
                targetSeries->append(double(plotted), target);
                QPen pen(QColor(0xd9, 0x53, 0x4f));
                pen.setStyle(Qt::DashLine);
                pen.setWidthF(1.5);
                targetSeries->setPen(pen);
                chart->addSeries(targetSeries);
                targetSeries->attachAxis(axisX);
                targetSeries->attachAxis(axisY);
            }
            chart->setTitle(QStringLiteral("按%1良率（最近 %2 个时段；目标线：%3）")
                                .arg(unit)
                                .arg(plotted)
                                .arg(target > 0.0 ? QStringLiteral("%1%").arg(target, 0, 'f', 1)
                                                  : QStringLiteral("未设目标")));
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
    ts << StatisticsReport::toCsv(m_summary, QDateTime::currentDateTime(), targetPercent());
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
    ts << StatisticsReport::toHtml(m_summary, title, QDateTime::currentDateTime(), targetPercent());
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
    // 三张图上下拼一张，便于直接贴进报告
    const QPixmap top = m_trendView->grab();
    const QPixmap middle = m_yieldView->grab();
    const QPixmap bottom = m_ngView->grab();
    QPixmap combined(qMax(top.width(), qMax(middle.width(), bottom.width())),
                     top.height() + middle.height() + bottom.height());
    combined.fill(Qt::white);
    {
        QPainter p(&combined);
        p.drawPixmap(0, 0, top);
        p.drawPixmap(0, top.height(), middle);
        p.drawPixmap(0, top.height() + middle.height(), bottom);
    }
    if (!combined.save(path, "PNG")) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入 %1").arg(path));
        return;
    }
    QMessageBox::information(this, QStringLiteral("导出完成"), QStringLiteral("已导出：\n%1").arg(path));
}
