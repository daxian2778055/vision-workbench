#include "UserManagementDialog.h"
#include "AppDatabase.h"
#include "AppLog.h"
#include "SessionManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QLabel>

UserManagementDialog::UserManagementDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    refreshUserTable();
}

UserManagementDialog::~UserManagementDialog()
{
}

void UserManagementDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u7528\u6237\u7BA1\u7406"));
    setMinimumSize(600, 400);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QStringLiteral("<b>\u7528\u6237\u7BA1\u7406</b>"));
    titleLabel->setStyleSheet("font-size: 14px;");
    mainLayout->addWidget(titleLabel);

    m_userTable = new QTableWidget();
    m_userTable->setColumnCount(4);
    m_userTable->setHorizontalHeaderLabels({
        QStringLiteral("\u7528\u6237\u540D"),
        QStringLiteral("\u89D2\u8272"),
        QStringLiteral("\u521B\u5EFA\u65F6\u95F4"),
        QStringLiteral("\u72B6\u6001")
    });
    m_userTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_userTable->horizontalHeader()->setStretchLastSection(true);
    m_userTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    mainLayout->addWidget(m_userTable);

    auto *buttonLayout = new QHBoxLayout();

    m_addButton = new QPushButton(QStringLiteral("\u6DFB\u52A0\u7528\u6237"));
    m_addButton->setMinimumHeight(30);
    m_removeButton = new QPushButton(QStringLiteral("\u5220\u9664\u7528\u6237"));
    m_removeButton->setMinimumHeight(30);
    m_closeButton = new QPushButton(QStringLiteral("\u5173\u95ED"));
    m_closeButton->setMinimumHeight(30);

    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_addButton, &QPushButton::clicked, this, &UserManagementDialog::onAddUser);
    connect(m_removeButton, &QPushButton::clicked, this, &UserManagementDialog::onRemoveUser);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void UserManagementDialog::onAddUser()
{
    bool ok = false;
    QString username = QInputDialog::getText(this,
        QStringLiteral("\u6DFB\u52A0\u7528\u6237"),
        QStringLiteral("\u7528\u6237\u540D:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || username.trimmed().isEmpty()) return;

    QString password = QInputDialog::getText(this,
        QStringLiteral("\u8BBE\u7F6E\u5BC6\u7801"),
        QStringLiteral("\u5BC6\u7801:"),
        QLineEdit::Password, QString(), &ok);
    if (!ok || password.isEmpty()) return;

    QStringList roles = { QStringLiteral("Admin"), QStringLiteral("Engineer"), QStringLiteral("Operator") };
    QString role = QInputDialog::getItem(this,
        QStringLiteral("\u9009\u62E9\u89D2\u8272"),
        QStringLiteral("\u89D2\u8272:"),
        roles, 2, false, &ok);
    if (!ok) return;

    auto *db = AppDatabase::instance();
    if (db->addUser(username.trimmed(), password, role)) {
        refreshUserTable();
        db->logOperation(QStringLiteral("Admin"), QStringLiteral("\u6DFB\u52A0\u7528\u6237"),
                         QStringLiteral("\u6DFB\u52A0\u7528\u6237: %1, \u89D2\u8272: %2").arg(username).arg(role));
        QMessageBox::information(this, QStringLiteral("\u6210\u529F"), QStringLiteral("\u7528\u6237\u6DFB\u52A0\u6210\u529F"));
    } else {
        // 闸拒绝与"用户名已存在"是两件事：一律报后者就是在给出一句假话（W-1）
        const QString refusal = SessionManager::instance()->writeGateRefusal();
        QMessageBox::warning(this, QStringLiteral("\u5931\u8D25"),
                             refusal.isEmpty() ? QStringLiteral("\u7528\u6237\u540D\u5DF2\u5B58\u5728")
                                               : refusal);
    }
}

void UserManagementDialog::onRemoveUser()
{
    int row = m_userTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"), QStringLiteral("\u8BF7\u9009\u62E9\u8981\u5220\u9664\u7684\u7528\u6237"));
        return;
    }

    QString username = m_userTable->item(row, 0)->text();
    if (username == QStringLiteral("admin")) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"), QStringLiteral("\u4E0D\u80FD\u5220\u9664\u7BA1\u7406\u5458\u8D26\u6237"));
        return;
    }

    if (QMessageBox::question(this, QStringLiteral("\u786E\u8BA4"),
                              QStringLiteral("\u786E\u5B9A\u8981\u5220\u9664\u7528\u6237 %1 \u5417\uFF1F").arg(username))
        == QMessageBox::Yes) {
        auto *db = AppDatabase::instance();
        if (!db->removeUser(username)) {
            // 旧写法把返回值丢掉：删没删都刷表、都记一条"删除用户"日志（W-1）
            const QString refusal = SessionManager::instance()->writeGateRefusal();
            QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
                                 refusal.isEmpty() ? QStringLiteral("\u5220\u9664\u7528\u6237\u5931\u8D25")
                                                   : refusal);
            return;
        }
        refreshUserTable();
        db->logOperation(QStringLiteral("Admin"), QStringLiteral("\u5220\u9664\u7528\u6237"),
                         QStringLiteral("\u5220\u9664\u7528\u6237: %1").arg(username));
    }
}

void UserManagementDialog::refreshUserTable()
{
    m_userTable->setRowCount(0);
    auto users = AppDatabase::instance()->queryUsers();
    for (const auto &u : users) {
        int row = m_userTable->rowCount();
        m_userTable->insertRow(row);
        m_userTable->setItem(row, 0, new QTableWidgetItem(u.name));
        m_userTable->setItem(row, 1, new QTableWidgetItem(u.role));
        m_userTable->setItem(row, 2, new QTableWidgetItem(u.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        m_userTable->setItem(row, 3, new QTableWidgetItem(QStringLiteral("\u6B63\u5E38")));
    }
    m_userTable->resizeColumnsToContents();
}
