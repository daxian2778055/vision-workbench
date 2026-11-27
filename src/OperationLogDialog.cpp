#include "OperationLogDialog.h"
#include "AppDatabase.h"
#include "AppLog.h"
#include "CsvEscape.h"
#include "ExportWriteCheck.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QGroupBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>

OperationLogDialog::OperationLogDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    refreshLogs();
}

OperationLogDialog::~OperationLogDialog()
{
}

void OperationLogDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u64CD\u4F5C\u65E5\u5FD7"));
    setMinimumSize(820, 500);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QStringLiteral("<b>\u64CD\u4F5C\u65E5\u5FD7\u67E5\u8BE2</b>"));
    titleLabel->setStyleSheet("font-size: 14px;");
    mainLayout->addWidget(titleLabel);

    auto *filterGroup = new QGroupBox(QStringLiteral("\u67E5\u8BE2\u6761\u4EF6"));
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

    filterLayout->addWidget(new QLabel(QStringLiteral("\u7528\u6237:")));
    m_userFilter = new QLineEdit();
    m_userFilter->setPlaceholderText(QStringLiteral("\u7528\u6237\u540D"));
    m_userFilter->setMinimumWidth(100);
    filterLayout->addWidget(m_userFilter);

    m_queryButton = new QPushButton(QStringLiteral("\u67E5\u8BE2"));
    m_queryButton->setMinimumHeight(28);
    filterLayout->addWidget(m_queryButton);

    // 只清 UI 表格，不动数据库：按钮文案必须如实（V2）
    m_clearButton = new QPushButton(QStringLiteral("\u6E05\u7A7A\u663E\u793A"));
    m_clearButton->setMinimumHeight(28);
    filterLayout->addWidget(m_clearButton);

    m_exportButton = new QPushButton(QStringLiteral("\u5BFC\u51FA CSV"));
    m_exportButton->setMinimumHeight(28);
    filterLayout->addWidget(m_exportButton);

    mainLayout->addWidget(filterGroup);

    m_logTable = new QTableWidget();
    m_logTable->setColumnCount(4);
    m_logTable->setHorizontalHeaderLabels({
        QStringLiteral("\u65F6\u95F4"),
        QStringLiteral("\u7528\u6237"),
        QStringLiteral("\u64CD\u4F5C"),
        QStringLiteral("\u8BE6\u60C5")
    });
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    mainLayout->addWidget(m_logTable);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_closeButton = new QPushButton(QStringLiteral("\u5173\u95ED"));
    m_closeButton->setMinimumHeight(30);
    btnLayout->addWidget(m_closeButton);
    mainLayout->addLayout(btnLayout);

    connect(m_queryButton, &QPushButton::clicked, this, &OperationLogDialog::onQuery);
    connect(m_clearButton, &QPushButton::clicked, this, &OperationLogDialog::onClear);
    connect(m_exportButton, &QPushButton::clicked, this, &OperationLogDialog::onExport);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void OperationLogDialog::refreshLogs()
{
    // V2：用户过滤下推到 SQL，不能再"先 LIMIT 2000 再过滤"漏掉老记录
    auto logs = AppDatabase::instance()->queryOperationLogs(
        m_fromDateTime->dateTime(), m_toDateTime->dateTime(), 2000,
        m_userFilter->text().trimmed());

    m_logTable->setRowCount(0);
    for (const auto &log : logs) {
        int row = m_logTable->rowCount();
        m_logTable->insertRow(row);
        m_logTable->setItem(row, 0, new QTableWidgetItem(
            log.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        m_logTable->setItem(row, 1, new QTableWidgetItem(log.user));
        m_logTable->setItem(row, 2, new QTableWidgetItem(log.action));
        m_logTable->setItem(row, 3, new QTableWidgetItem(log.detail));
    }
    m_logTable->resizeColumnsToContents();
}

void OperationLogDialog::onQuery()
{
    refreshLogs();
}

void OperationLogDialog::onClear()
{
    // 操作日志由数据库持久化，此处仅清空当前显示（保留数据）
    m_logTable->setRowCount(0);
}

void OperationLogDialog::onExport()
{
    // AE1：导出的是当前查询结果（所见即所得），不另起一次可能漂移的查询。
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("\u5BFC\u51FA\u64CD\u4F5C\u65E5\u5FD7"), QString(),
        QStringLiteral("CSV (*.csv);;\u6587\u672C (*.txt)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!ExportWriteCheck::openForExport(this, file, path))
        return;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QChar(0xFEFF);
    out << csvField(QStringLiteral("\u65F6\u95F4")) << QLatin1Char(',')
        << csvField(QStringLiteral("\u7528\u6237")) << QLatin1Char(',')
        << csvField(QStringLiteral("\u64CD\u4F5C")) << QLatin1Char(',')
        << csvField(QStringLiteral("\u8BE6\u60C5")) << Qt::endl;
    for (int row = 0; row < m_logTable->rowCount(); ++row) {
        QStringList cells;
        for (int col = 0; col < m_logTable->columnCount(); ++col) {
            QTableWidgetItem *item = m_logTable->item(row, col);
            cells << csvField(item ? item->text() : QString());
        }
        out << cells.join(QLatin1Char(',')) << Qt::endl;
    }
    ExportWriteCheck::finishTextExport(this, file, out, path);
}
