#include "UserLoginDialog.h"
#include "AppDatabase.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QKeyEvent>
#include <QShowEvent>

UserLoginDialog::UserLoginDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
}

UserLoginDialog::~UserLoginDialog()
{
}

void UserLoginDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // 模态 ≠ 一定在最前面：刚启动时主窗口还没显示、也没有前台窗口，登录框可能被压在别的窗口下面，
    // 用户就会以为"软件没打开"。这里主动抬到前台并抢焦点（应用刚启动时拥有前台权限，能生效）。
    raise();
    activateWindow();
    if (m_usernameEdit && m_usernameEdit->text().isEmpty())
        m_usernameEdit->setFocus();   // 焦点直接落在用户名，省一次点击
}

void UserLoginDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u7528\u6237\u767B\u5F55"));
    setFixedSize(350, 260);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    auto *titleLabel = new QLabel(QStringLiteral("<b>\u767B\u5F55 VisionFlowPlatform</b>"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 16px; color: #1890ff; margin-bottom: 10px;");
    mainLayout->addWidget(titleLabel);

    auto *formLayout = new QFormLayout();
    formLayout->setSpacing(8);

    m_usernameEdit = new QLineEdit();
    m_usernameEdit->setPlaceholderText(QStringLiteral("\u8BF7\u8F93\u5165\u7528\u6237\u540D"));
    m_usernameEdit->setMinimumHeight(30);
    formLayout->addRow(QStringLiteral("\u7528\u6237\u540D:"), m_usernameEdit);

    m_passwordEdit = new QLineEdit();
    m_passwordEdit->setPlaceholderText(QStringLiteral("\u8BF7\u8F93\u5165\u5BC6\u7801"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setMinimumHeight(30);
    formLayout->addRow(QStringLiteral("\u5BC6\u7801:"), m_passwordEdit);

    m_confirmPasswordEdit = new QLineEdit();
    m_confirmPasswordEdit->setPlaceholderText(QStringLiteral("\u8BF7\u518D\u6B21\u8F93\u5165\u5BC6\u7801"));
    m_confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    m_confirmPasswordEdit->setMinimumHeight(30);
    m_confirmPasswordEdit->hide();
    formLayout->addRow(QStringLiteral("\u786E\u8BA4\u5BC6\u7801:"), m_confirmPasswordEdit);

    mainLayout->addLayout(formLayout);

    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: red;");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    mainLayout->addWidget(m_statusLabel);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    m_loginButton = new QPushButton(QStringLiteral("\u767B\u5F55"));
    m_loginButton->setMinimumHeight(32);
    m_loginButton->setStyleSheet(
        "QPushButton { background-color: #1890ff; color: white; border: none; border-radius: 4px; padding: 6px 20px; }"
        "QPushButton:hover { background-color: #40a9ff; }"
    );

    m_cancelButton = new QPushButton(QStringLiteral("\u53D6\u6D88"));
    m_cancelButton->setMinimumHeight(32);

    buttonLayout->addStretch();
    buttonLayout->addWidget(m_loginButton);
    buttonLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_loginButton, &QPushButton::clicked, this, &UserLoginDialog::onLogin);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &UserLoginDialog::onLogin);
    connect(m_usernameEdit, &QLineEdit::returnPressed, [this]() {
        m_passwordEdit->setFocus();
    });
    connect(m_confirmPasswordEdit, &QLineEdit::returnPressed, this, &UserLoginDialog::onLogin);

    // 首次使用（读通了而确实一个账户都没有）：进入管理员初始化模式。
    // 判据走 accountSurface()：读失败交回的也是空表，而这条分支摆出来的是
    // 「创建管理员并当场登录」——那一次判定的方向是凭空得到一个 Admin 会话。
    auto *db = AppDatabase::instance();
    if (!db->databasePath().isEmpty() && db->accountSurface() == AccountSurface::NoneYet) {
        m_confirmPasswordEdit->show();
        m_loginButton->setText(QStringLiteral("\u521B\u5EFA\u7BA1\u7406\u5458\u5E76\u767B\u5F55"));
        m_statusLabel->setText(QStringLiteral("\u9996\u6B21\u4F7F\u7528\uFF1A\u8BF7\u8BBE\u7F6E\u521D\u59CB\u7BA1\u7406\u5458\u8D26\u53F7\uFF08\u5BC6\u7801\u81F3\u5C11 6 \u4F4D\uFF09"));
        m_statusLabel->setStyleSheet("color: #fa8c16;");
        m_statusLabel->show();
    }
}

void UserLoginDialog::onLogin()
{
    QString username = m_usernameEdit->text().trimmed();
    QString password = m_passwordEdit->text();

    if (username.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("\u7528\u6237\u540D\u4E0D\u80FD\u4E3A\u7A7A"));
        m_statusLabel->show();
        return;
    }

    if (password.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("\u5BC6\u7801\u4E0D\u80FD\u4E3A\u7A7A"));
        m_statusLabel->show();
        return;
    }

    auto *db = AppDatabase::instance();

    // 数据库不可用：拒绝登录（不允许任何隐式后门路径）
    if (db->databasePath().isEmpty()) {
        m_statusLabel->setText(QStringLiteral("\u6570\u636E\u5E93\u4E0D\u53EF\u7528\uFF0C\u65E0\u6CD5\u767B\u5F55\u3002\u8BF7\u68C0\u67E5\u6570\u636E\u5E93\u521D\u59CB\u5316\u3002"));
        m_statusLabel->setStyleSheet("color: red;");
        m_statusLabel->show();
        return;
    }

    // 首次使用：读通了而确实没有账户 → 创建初始管理员
    const AccountSurface surface = db->accountSurface();
    if (surface == AccountSurface::NoneYet) {
        if (initFirstAdmin(username, password)) {
            accept();
        }
        return;
    }
    if (surface == AccountSurface::Unreadable) {
        // 读不到用户表时既不能走首装（那是凭空建 Admin 的一条路），也不能当口令错。
        m_statusLabel->setText(QStringLiteral("读不到用户表，无法登录，也无法确认是否首次使用。")
                               + QStringLiteral("\n") + db->lastDatabaseError());
        m_statusLabel->setStyleSheet("color: red;");
        m_statusLabel->show();
        db->logOperation(username, QStringLiteral("登录受阻"),
                         QStringLiteral("用户表读不到：") + db->lastDatabaseError());
        return;
    }

    // 正常认证。readFailed 把「库读不到」与「口令错」分开——原先 false 只有一种显示。
    bool readFailed = false;
    if (db->authenticateUser(username, password, &readFailed)) {
        m_loggedInUser = username;
        auto users = db->queryUsers();
        for (const auto &u : users) {
            if (u.name == username) {
                m_loggedInRole = u.role;
                break;
            }
        }
        if (m_loggedInRole.isEmpty()) m_loggedInRole = QStringLiteral("Operator");
        db->logOperation(username, QStringLiteral("\u767B\u5F55"), QStringLiteral("\u7528\u6237\u767B\u5F55\u6210\u529F"));
        accept();
        return;
    }

    if (readFailed) {
        // 判定没做出来不等于口令错：报「密码错误」会把一次数据库故障变成操作员反复试错，
        // 还在审计里留下一条根本不成立的失败登录。
        m_statusLabel->setText(QStringLiteral("数据库读不到，无法校验口令：")
                               + db->lastDatabaseError());
        m_statusLabel->setStyleSheet("color: red;");
        m_statusLabel->show();
        db->logOperation(username, QStringLiteral("登录受阻"),
                         QStringLiteral("口令校验读不到数据库：") + db->lastDatabaseError());
        return;
    }
    m_statusLabel->setText(QStringLiteral("\u7528\u6237\u540D\u6216\u5BC6\u7801\u9519\u8BEF"));
    m_statusLabel->setStyleSheet("color: red;");
    m_statusLabel->show();
    db->logOperation(username, QStringLiteral("\u767B\u5F55\u5931\u8D25"), QStringLiteral("\u7528\u6237\u540D\u6216\u5BC6\u7801\u9519\u8BEF"));
}

bool UserLoginDialog::initFirstAdmin(const QString &username, const QString &password)
{
    // 安全约束：用户名非空、密码 ≥ 6 位、两次输入一致（无任何默认口令）
    if (m_confirmPasswordEdit->text() != password) {
        m_statusLabel->setText(QStringLiteral("\u4E24\u6B21\u5BC6\u7801\u4E0D\u4E00\u81F4"));
        m_statusLabel->setStyleSheet("color: red;");
        m_statusLabel->show();
        return false;
    }
    if (password.length() < 6) {
        m_statusLabel->setText(QStringLiteral("\u5BC6\u7801\u81F3\u5C11 6 \u4F4D"));
        m_statusLabel->setStyleSheet("color: red;");
        m_statusLabel->show();
        return false;
    }

    auto *db = AppDatabase::instance();
    // 竞态保护：仅在确实没有用户时创建。判据同样是三态——读不出来时一律不建账户，
    // 因为「读不出来」在界面这边长得和「还没有用户」一模一样。
    if (db->accountSurface() != AccountSurface::NoneYet) {
        m_statusLabel->setText(QStringLiteral("\u7528\u6237\u5DF2\u521B\u5EFA\uFF0C\u8BF7\u76F4\u63A5\u767B\u5F55"));
        m_statusLabel->setStyleSheet("color: red;");
        m_statusLabel->show();
        return false;
    }

    if (!db->addUser(username, password, QStringLiteral("Admin"))) {
        m_statusLabel->setText(QStringLiteral("\u521B\u5EFA\u7BA1\u7406\u5458\u5931\u8D25\uFF0C\u8BF7\u68C0\u67E5\u6570\u636E\u5E93"));
        m_statusLabel->setStyleSheet("color: red;");
        m_statusLabel->show();
        return false;
    }

    m_loggedInUser = username;
    m_loggedInRole = QStringLiteral("Admin");
    db->logOperation(username, QStringLiteral("\u767B\u5F55"), QStringLiteral("\u9996\u6B21\u4F7F\u7528\u521D\u59CB\u5316\u7BA1\u7406\u5458"));
    return true;
}
