#include "GlobalTriggerDialog.h"
#include "GlobalTriggerManager.h"
#include "CommunicationManager.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QLabel>

GlobalTriggerDialog::GlobalTriggerDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    refreshTriggerTable();
}

GlobalTriggerDialog::~GlobalTriggerDialog()
{
}

void GlobalTriggerDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u5168\u5C40\u89E6\u53D1\u914D\u7F6E"));
    setMinimumSize(700, 400);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QStringLiteral("<b>\u5168\u5C40\u89E6\u53D1</b>"));
    titleLabel->setStyleSheet("font-size: 14px;");
    mainLayout->addWidget(titleLabel);

    m_infoLabel = new QLabel(QStringLiteral(
        "\u914D\u7F6E\u89E6\u53D1\u6E90\u4E0E\u6267\u884C\u6D41\u7A0B\u7684\u6620\u5C04\u5173\u7CFB\u3002\n"
        "\u652F\u6301\u4E24\u79CD\u89E6\u53D1\u6A21\u5F0F: \u5B57\u7B26\u4E32\u89E6\u53D1(\u5339\u914D\u901A\u4FE1\u6570\u636E) / \u4E8B\u4EF6\u89E6\u53D1(\u5339\u914D\u63A5\u6536\u4E8B\u4EF6)"
    ));
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setStyleSheet("color: gray; font-size: 11px;");
    mainLayout->addWidget(m_infoLabel);

    m_triggerTable = new QTableWidget();
    m_triggerTable->setColumnCount(5);
    m_triggerTable->setHorizontalHeaderLabels({
        QStringLiteral("\u89E6\u53D1\u540D\u79F0"),
        QStringLiteral("\u89E6\u53D1\u7C7B\u578B"),
        QStringLiteral("\u89E6\u53D1\u6E90"),
        QStringLiteral("\u76EE\u6807\u6D41\u7A0B"),
        QStringLiteral("\u89E6\u53D1\u6B21\u6570")
    });
    m_triggerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_triggerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_triggerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_triggerTable->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(m_triggerTable);

    auto *btnLayout = new QHBoxLayout();
    m_addStringBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u5B57\u7B26\u4E32\u89E6\u53D1"));
    m_addEventBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u4E8B\u4EF6\u89E6\u53D1"));
    m_removeBtn = new QPushButton(QStringLiteral("\u5220\u9664\u89E6\u53D1"));

    btnLayout->addWidget(m_addStringBtn);
    btnLayout->addWidget(m_addEventBtn);
    btnLayout->addWidget(m_removeBtn);
    btnLayout->addStretch();

    m_closeBtn = new QPushButton(QStringLiteral("\u5173\u95ED"));
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_addStringBtn, &QPushButton::clicked, this, &GlobalTriggerDialog::onAddStringTrigger);
    connect(m_addEventBtn, &QPushButton::clicked, this, &GlobalTriggerDialog::onAddEventTrigger);
    connect(m_removeBtn, &QPushButton::clicked, this, &GlobalTriggerDialog::onRemoveTrigger);
    connect(m_closeBtn, &QPushButton::clicked, this, &GlobalTriggerDialog::onApply);
}

void GlobalTriggerDialog::refreshTriggerTable()
{
    m_triggerTable->setRowCount(0);
    auto triggers = GlobalTriggerManager::instance()->allTriggers();

    for (const auto &t : triggers) {
        int row = m_triggerTable->rowCount();
        m_triggerTable->insertRow(row);
        m_triggerTable->setItem(row, 0, new QTableWidgetItem(t.id));

        QString typeStr;
        if (t.type == GlobalTriggerManager::STRING_TRIGGER)
            typeStr = QStringLiteral("\u5B57\u7B26\u4E32\u89E6\u53D1");
        else
            typeStr = QStringLiteral("\u4E8B\u4EF6\u89E6\u53D1");
        m_triggerTable->setItem(row, 1, new QTableWidgetItem(typeStr));
        m_triggerTable->setItem(row, 2, new QTableWidgetItem(t.triggerSource));
        m_triggerTable->setItem(row, 3, new QTableWidgetItem(t.flowName));

        auto *countItem = new QTableWidgetItem(QString::number(t.triggerCount));
        countItem->setForeground(t.triggerCount > 0 ? QColor(0, 128, 0) : Qt::gray);
        m_triggerTable->setItem(row, 4, countItem);
    }
    m_triggerTable->resizeColumnsToContents();
}

void GlobalTriggerDialog::onAddStringTrigger()
{
    bool ok = false;
    QString triggerChar = QInputDialog::getText(this,
        QStringLiteral("\u6DFB\u52A0\u5B57\u7B26\u4E32\u89E6\u53D1"),
        QStringLiteral("\u89E6\u53D1\u5B57\u7B26:"),
        QLineEdit::Normal, QStringLiteral("start"), &ok);
    if (!ok || triggerChar.trimmed().isEmpty()) return;

    QString flowName = QInputDialog::getText(this,
        QStringLiteral("\u76EE\u6807\u6D41\u7A0B"),
        QStringLiteral("\u76EE\u6807\u6D41\u7A0B\u540D\u79F0:"),
        QLineEdit::Normal, QStringLiteral("\u6D41\u7A0B 1"), &ok);
    if (!ok || flowName.trimmed().isEmpty()) return;

    GlobalTriggerManager::instance()->setStringTrigger(triggerChar.trimmed(), flowName.trimmed());
    refreshTriggerTable();
}

void GlobalTriggerDialog::onAddEventTrigger()
{
    bool ok = false;
    QStringList eventIds = CommunicationManager::instance()->receiveEventIds();
    if (eventIds.isEmpty()) {
        // Allow manual entry
        QString eventId = QInputDialog::getText(this,
            QStringLiteral("\u6DFB\u52A0\u4E8B\u4EF6\u89E6\u53D1"),
            QStringLiteral("\u4E8B\u4EF6ID:"),
            QLineEdit::Normal, QString(), &ok);
        if (!ok || eventId.trimmed().isEmpty()) return;

        QString flowName = QInputDialog::getText(this,
            QStringLiteral("\u76EE\u6807\u6D41\u7A0B"),
            QStringLiteral("\u76EE\u6807\u6D41\u7A0B\u540D\u79F0:"),
            QLineEdit::Normal, QStringLiteral("\u6D41\u7A0B 1"), &ok);
        if (!ok || flowName.trimmed().isEmpty()) return;

        GlobalTriggerManager::instance()->setEventTrigger(eventId.trimmed(), flowName.trimmed());
    } else {
        QString eventId = QInputDialog::getItem(this,
            QStringLiteral("\u6DFB\u52A0\u4E8B\u4EF6\u89E6\u53D1"),
            QStringLiteral("\u9009\u62E9\u4E8B\u4EF6:"),
            eventIds, 0, false, &ok);
        if (!ok) return;

        QString flowName = QInputDialog::getText(this,
            QStringLiteral("\u76EE\u6807\u6D41\u7A0B"),
            QStringLiteral("\u76EE\u6807\u6D41\u7A0B\u540D\u79F0:"),
            QLineEdit::Normal, QStringLiteral("\u6D41\u7A0B 1"), &ok);
        if (!ok || flowName.trimmed().isEmpty()) return;

        GlobalTriggerManager::instance()->setEventTrigger(eventId, flowName.trimmed());
    }

    refreshTriggerTable();
}

void GlobalTriggerDialog::onRemoveTrigger()
{
    int row = m_triggerTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u9009\u62E9\u8981\u5220\u9664\u7684\u89E6\u53D1\u89C4\u5219"));
        return;
    }

    QString triggerId = m_triggerTable->item(row, 0)->text();
    int typeCol = m_triggerTable->item(row, 1)->text() == QStringLiteral("\u5B57\u7B26\u4E32\u89E6\u53D1") ? 0 : 1;
    QString triggerSource = m_triggerTable->item(row, 2)->text();

    if (QMessageBox::question(this, QStringLiteral("\u786E\u8BA4"),
        QStringLiteral("\u786E\u5B9A\u8981\u5220\u9664\u89E6\u53D1\u89C4\u5219 %1 \u5417\uFF1F").arg(triggerId))
        == QMessageBox::Yes) {

        if (typeCol == 0) {
            GlobalTriggerManager::instance()->removeStringTrigger(triggerSource);
        } else {
            GlobalTriggerManager::instance()->removeEventTrigger(triggerSource);
        }
        refreshTriggerTable();
    }
}

void GlobalTriggerDialog::onApply()
{
    QMessageBox::information(this, QStringLiteral("\u63D0\u793A"),
        QStringLiteral("\u89E6\u53D1\u914D\u7F6E\u5DF2\u5E94\u7528"));
    accept();
}
