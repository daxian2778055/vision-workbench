#include "InspectionResultDialog.h"
#include "AppDatabase.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>

InspectionResultDialog::InspectionResultDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    refreshResults();
}

InspectionResultDialog::~InspectionResultDialog()
{
}

void InspectionResultDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u68C0\u6D4B\u7ED3\u679C"));
    setMinimumSize(800, 500);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QStringLiteral("<b>\u68C0\u6D4B\u7ED3\u679C\u8BB0\u5F55</b>"));
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

    filterLayout->addWidget(new QLabel(QStringLiteral("\u6D41\u7A0B:")));
    m_flowFilter = new QLineEdit();
    m_flowFilter->setPlaceholderText(QStringLiteral("\u6D41\u7A0B\u540D\u79F0"));
    m_flowFilter->setMinimumWidth(120);
    filterLayout->addWidget(m_flowFilter);

    m_queryButton = new QPushButton(QStringLiteral("\u67E5\u8BE2"));
    m_queryButton->setMinimumHeight(28);
    filterLayout->addWidget(m_queryButton);

    m_exportButton = new QPushButton(QStringLiteral("\u5BFC\u51FACSV"));
    m_exportButton->setMinimumHeight(28);
    filterLayout->addWidget(m_exportButton);

    mainLayout->addWidget(filterGroup);

    // Result table
    m_resultTable = new QTableWidget();
    m_resultTable->setColumnCount(6);
    m_resultTable->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("\u65F6\u95F4"),
        QStringLiteral("\u6D41\u7A0B"),
        QStringLiteral("\u7B97\u5B50"),
        QStringLiteral("\u7ED3\u679C"),
        QStringLiteral("\u503C")
    });
    m_resultTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(m_resultTable);

    auto *buttonLayout = new QHBoxLayout();
    m_closeButton = new QPushButton(QStringLiteral("\u5173\u95ED"));
    m_closeButton->setMinimumHeight(30);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_queryButton, &QPushButton::clicked, this, &InspectionResultDialog::onQuery);
    connect(m_exportButton, &QPushButton::clicked, this, &InspectionResultDialog::onExport);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void InspectionResultDialog::onQuery()
{
    refreshResults();
}

void InspectionResultDialog::onExport()
{
    QString filePath = QFileDialog::getSaveFileName(this,
        QStringLiteral("\u5BFC\u51FA\u68C0\u6D4B\u7ED3\u679C"),
        QStringLiteral("inspection_results.csv"),
        QStringLiteral("CSV Files (*.csv)"));
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("\u5931\u8D25"), QStringLiteral("\u65E0\u6CD5\u521B\u5EFA\u6587\u4EF6"));
        return;
    }

    QTextStream out(&file);
    // BOM for Excel compatibility
    out.setEncoding(QStringConverter::Utf8);
    out << QChar(0xFEFF);

    // Header
    out << QStringLiteral("ID,\u65F6\u95F4,\u6D41\u7A0B,\u7B97\u5B50,\u7ED3\u679C,\u503C\n");

    for (int row = 0; row < m_resultTable->rowCount(); ++row) {
        for (int col = 0; col < m_resultTable->columnCount(); ++col) {
            if (col > 0) out << QStringLiteral(",");
            auto *item = m_resultTable->item(row, col);
            if (item) {
                QString text = item->text();
                text.replace(QStringLiteral("\""), QStringLiteral("\"\""));
                out << QStringLiteral("\"") << text << QStringLiteral("\"");
            }
        }
        out << QStringLiteral("\n");
    }

    file.close();
    QMessageBox::information(this, QStringLiteral("\u6210\u529F"), QStringLiteral("\u68C0\u6D4B\u7ED3\u679C\u5DF2\u5BFC\u51FA"));
}

void InspectionResultDialog::refreshResults()
{
    m_resultTable->setRowCount(0);
    QDateTime from = m_fromDateTime->dateTime();
    QDateTime to = m_toDateTime->dateTime();

    auto results = AppDatabase::instance()->queryResults(from, to);

    // Apply flow filter if set
    QString flowFilter = m_flowFilter->text().trimmed();

    for (const auto &r : results) {
        if (!flowFilter.isEmpty() && !r.flowName.contains(flowFilter, Qt::CaseInsensitive)) {
            continue;
        }

        int row = m_resultTable->rowCount();
        m_resultTable->insertRow(row);
        m_resultTable->setItem(row, 0, new QTableWidgetItem(QString::number(r.id)));
        m_resultTable->setItem(row, 1, new QTableWidgetItem(r.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        m_resultTable->setItem(row, 2, new QTableWidgetItem(r.flowName));
        m_resultTable->setItem(row, 3, new QTableWidgetItem(r.nodeName));

        auto *resultItem = new QTableWidgetItem(r.passed ? QStringLiteral("OK") : QStringLiteral("NG"));
        resultItem->setForeground(r.passed ? QColor(0, 128, 0) : Qt::red);
        m_resultTable->setItem(row, 4, resultItem);

        m_resultTable->setItem(row, 5, new QTableWidgetItem(r.value));
    }
    m_resultTable->resizeColumnsToContents();
}
