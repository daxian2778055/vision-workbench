#include "AlarmHistoryDialog.h"
#include "AppDatabase.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QGroupBox>
#include <QMessageBox>

AlarmHistoryDialog::AlarmHistoryDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    refreshAlarms();
}

AlarmHistoryDialog::~AlarmHistoryDialog()
{
}

void AlarmHistoryDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u62A5\u8B66\u5386\u53F2"));
    setMinimumSize(800, 500);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QStringLiteral("<b>\u62A5\u8B66\u5386\u53F2\u8BB0\u5F55</b>"));
    titleLabel->setStyleSheet("font-size: 14px;");
    mainLayout->addWidget(titleLabel);

    // Filter area
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

    m_queryButton = new QPushButton(QStringLiteral("\u67E5\u8BE2"));
    m_queryButton->setMinimumHeight(28);
    filterLayout->addWidget(m_queryButton);

    m_clearButton = new QPushButton(QStringLiteral("\u6E05\u7A7A\u5386\u53F2"));
    m_clearButton->setMinimumHeight(28);
    filterLayout->addWidget(m_clearButton);

    mainLayout->addWidget(filterGroup);

    // Alarm table
    m_alarmTable = new QTableWidget();
    m_alarmTable->setColumnCount(5);
    m_alarmTable->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("\u65F6\u95F4"),
        QStringLiteral("\u6A21\u5757"),
        QStringLiteral("\u7EA7\u522B"),
        QStringLiteral("\u5185\u5BB9")
    });
    m_alarmTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_alarmTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_alarmTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_alarmTable->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(m_alarmTable);

    auto *buttonLayout = new QHBoxLayout();
    m_closeButton = new QPushButton(QStringLiteral("\u5173\u95ED"));
    m_closeButton->setMinimumHeight(30);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_queryButton, &QPushButton::clicked, this, &AlarmHistoryDialog::onQuery);
    connect(m_clearButton, &QPushButton::clicked, this, &AlarmHistoryDialog::onClear);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void AlarmHistoryDialog::onQuery()
{
    refreshAlarms();
}

void AlarmHistoryDialog::onClear()
{
    if (QMessageBox::question(this, QStringLiteral("\u786E\u8BA4"),
                              QStringLiteral("\u786E\u5B9A\u8981\u6E05\u7A7A\u62A5\u8B66\u5386\u53F2\u5417\uFF1F"))
        == QMessageBox::Yes) {
        m_alarmTable->setRowCount(0);
        QMessageBox::information(this, QStringLiteral("\u63D0\u793A"), QStringLiteral("\u62A5\u8B66\u5386\u53F2\u5DF2\u6E05\u7A7A"));
    }
}

void AlarmHistoryDialog::refreshAlarms()
{
    m_alarmTable->setRowCount(0);
    QDateTime from = m_fromDateTime->dateTime();
    QDateTime to = m_toDateTime->dateTime();

    auto alarms = AppDatabase::instance()->queryAlarms(from, to);
    for (const auto &a : alarms) {
        int row = m_alarmTable->rowCount();
        m_alarmTable->insertRow(row);
        m_alarmTable->setItem(row, 0, new QTableWidgetItem(QString::number(a.id)));
        m_alarmTable->setItem(row, 1, new QTableWidgetItem(a.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        m_alarmTable->setItem(row, 2, new QTableWidgetItem(a.module));
        m_alarmTable->setItem(row, 3, new QTableWidgetItem(a.level));

        // Color code severity
        QColor textColor;
        if (a.level == QStringLiteral("Error")) textColor = Qt::red;
        else if (a.level == QStringLiteral("Warning")) textColor = QColor(255, 165, 0);
        else textColor = Qt::black;

        auto *msgItem = new QTableWidgetItem(a.message);
        msgItem->setForeground(textColor);
        m_alarmTable->setItem(row, 4, msgItem);
    }
    m_alarmTable->resizeColumnsToContents();
}
