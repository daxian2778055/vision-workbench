#include "ReportDialog.h"
#include "AppDatabase.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QGroupBox>
#include <QDateTime>
#include <QPainter>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>

ReportDialog::ReportDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    refreshStats();
}

ReportDialog::~ReportDialog()
{
}

void ReportDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u68C0\u6D4B\u62A5\u8868"));
    setMinimumSize(800, 560);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QStringLiteral("<b>\u68C0\u6D4B\u7ED3\u679C\u7EDF\u8BA1\u62A5\u8868</b>"));
    titleLabel->setStyleSheet("font-size: 14px;");
    mainLayout->addWidget(titleLabel);

    auto *filterGroup = new QGroupBox(QStringLiteral("\u67E5\u8BE2\u8303\u56F4"));
    auto *filterLayout = new QHBoxLayout(filterGroup);

    filterLayout->addWidget(new QLabel(QStringLiteral("\u4ECE:")));
    m_fromDateTime = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-1));
    m_fromDateTime->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_fromDateTime->setCalendarPopup(true);
    filterLayout->addWidget(m_fromDateTime);

    filterLayout->addWidget(new QLabel(QStringLiteral("\u5230:")));
    m_toDateTime = new QDateTimeEdit(QDateTime::currentDateTime());
    m_toDateTime->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_toDateTime->setCalendarPopup(true);
    filterLayout->addWidget(m_toDateTime);

    m_queryBtn = new QPushButton(QStringLiteral("\u7EDF\u8BA1"));
    m_queryBtn->setMinimumHeight(28);
    filterLayout->addWidget(m_queryBtn);

    m_exportBtn = new QPushButton(QStringLiteral("\u5BFC\u51FA CSV"));
    m_exportBtn->setMinimumHeight(28);
    filterLayout->addWidget(m_exportBtn);

    filterLayout->addStretch();
    mainLayout->addWidget(filterGroup);

    m_statTable = new QTableWidget();
    m_statTable->setColumnCount(4);
    m_statTable->setHorizontalHeaderLabels({
        QStringLiteral("\u6D41\u7A0B"),
        QStringLiteral("\u603B\u6570"),
        QStringLiteral("\u901A\u8FC7"),
        QStringLiteral("\u5931\u8D25")
    });
    m_statTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_statTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_statTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout->addWidget(m_statTable);

    auto *chartLabel = new QLabel(QStringLiteral("\u901A\u8FC7\u7387\u56FE\u8868:"));
    chartLabel->setStyleSheet("font-weight: bold; margin-top: 6px;");
    mainLayout->addWidget(chartLabel);

    // 柱状图区域（由 paintEvent 绘制）
    auto *chartWidget = new QWidget();
    chartWidget->setObjectName(QStringLiteral("reportChart"));
    chartWidget->setMinimumHeight(140);
    chartWidget->setStyleSheet("background: white; border: 1px solid #d0d0d0;");
    mainLayout->addWidget(chartWidget);

    connect(m_queryBtn, &QPushButton::clicked, this, &ReportDialog::onQuery);
    connect(m_exportBtn, &QPushButton::clicked, this, &ReportDialog::onExport);
}

void ReportDialog::refreshStats()
{
    m_stats.clear();
    auto results = AppDatabase::instance()->queryResults(
        m_fromDateTime->dateTime(), m_toDateTime->dateTime(), 100000);

    QMap<QString, FlowStat> map;
    for (const auto &r : results) {
        FlowStat &s = map[r.flowName];
        s.flowName = r.flowName;
        ++s.total;
        if (r.passed) ++s.passed;
        else ++s.failed;
    }
    m_stats = map.values();

    m_statTable->setRowCount(0);
    for (const auto &s : m_stats) {
        int row = m_statTable->rowCount();
        m_statTable->insertRow(row);
        m_statTable->setItem(row, 0, new QTableWidgetItem(s.flowName));
        m_statTable->setItem(row, 1, new QTableWidgetItem(QString::number(s.total)));
        m_statTable->setItem(row, 2, new QTableWidgetItem(QString::number(s.passed)));
        m_statTable->setItem(row, 3, new QTableWidgetItem(QString::number(s.failed)));
    }
    update(); // 触发重绘图表
}

void ReportDialog::paintEvent(QPaintEvent *event)
{
    QDialog::paintEvent(event);

    // 找到图表区域绘制柱状图
    QWidget *chart = findChild<QWidget *>(QStringLiteral("reportChart"));
    if (!chart) return;

    QPainter painter(chart);
    painter.setRenderHint(QPainter::Antialiasing);

    QRectF area = chart->rect().adjusted(40, 20, -20, -30);
    if (area.width() <= 0 || area.height() <= 0) return;

    if (m_stats.isEmpty()) {
        painter.setPen(Qt::gray);
        painter.drawText(area, Qt::AlignCenter, QStringLiteral("\u6682\u65E0\u6570\u636E"));
        return;
    }

    double maxTotal = 1.0;
    for (const auto &s : m_stats) {
        maxTotal = qMax(maxTotal, static_cast<double>(s.total));
    }

    double barWidth = area.width() / m_stats.size() * 0.6;
    double gap = area.width() / m_stats.size();

    for (int i = 0; i < m_stats.size(); ++i) {
        const auto &s = m_stats[i];
        double x = area.left() + i * gap + (gap - barWidth) / 2.0;
        double passH = area.height() * s.passed / maxTotal;
        double failH = area.height() * s.failed / maxTotal;

        // 通过段（绿）
        painter.fillRect(QRectF(x, area.bottom() - passH, barWidth, passH),
                         QColor(0x52, 0xc4, 0x1a));
        // 失败段（红）
        painter.fillRect(QRectF(x, area.bottom() - passH - failH, barWidth, failH),
                         QColor(0xe6, 0x53, 0x00));

        // 流程名
        painter.setPen(Qt::darkGray);
        QFont f = painter.font();
        f.setPointSizeF(8.5);
        painter.setFont(f);
        painter.drawText(QRectF(x, area.bottom() + 4, barWidth + 8, 18),
                         Qt::AlignHCenter, s.flowName);
        // 总数标注
        painter.setPen(Qt::black);
        painter.drawText(QRectF(x, area.top() - 2, barWidth, 16),
                         Qt::AlignHCenter, QString::number(s.total));
    }
}

void ReportDialog::onQuery()
{
    refreshStats();
}

void ReportDialog::onExport()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("\u5BFC\u51FA\u62A5\u8868"), QString(),
        QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("\u5931\u8D25"),
            QStringLiteral("\u65E0\u6CD5\u5199\u5165\u6587\u4EF6"));
        return;
    }
    QTextStream out(&file);
    // 写 UTF-8 BOM：中文 Windows 的 Excel 默认按 ANSI 解析 CSV，没有 BOM 就会把中文表头/流程名
    // 显示成乱码（结果表与检测记录两处导出已经这么做了，这里补齐一致）。
    out << QChar(0xFEFF);
    out << QStringLiteral("\u6D41\u7A0B,\u603B\u6570,\u901A\u8FC7,\u5931\u8D25\n");
    for (const auto &s : m_stats) {
        out << s.flowName << ',' << s.total << ',' << s.passed << ',' << s.failed << '\n';
    }
    file.close();
    QMessageBox::information(this, QStringLiteral("\u63D0\u793A"),
        QStringLiteral("\u5DF2\u5BFC\u51FA\u5230: %1").arg(path));
}
