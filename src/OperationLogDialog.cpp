#include "OperationLogDialog.h"
#include "AppDatabase.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QGroupBox>
#include <QDateTime>

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

    m_clearButton = new QPushButton(QStringLiteral("\u6E05\u9664\u65E5\u5FD7"));
    m_clearButton->setMinimumHeight(28);
    filterLayout->addWidget(m_clearButton);

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
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void OperationLogDialog::refreshLogs()
{
    auto logs = AppDatabase::instance()->queryOperationLogs(
        m_fromDateTime->dateTime(), m_toDateTime->dateTime(), 2000);

    m_logTable->setRowCount(0);
    for (const auto &log : logs) {
        // 用户过滤
        if (!m_userFilter->text().trimmed().isEmpty()
            && log.user != m_userFilter->text().trimmed()) {
            continue;
        }
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
