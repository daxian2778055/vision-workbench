#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

class UserLoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserLoginDialog(QWidget *parent = nullptr);
    ~UserLoginDialog();

    QString loggedInUser() const { return m_loggedInUser; }
    QString loggedInRole() const { return m_loggedInRole; }

private slots:
    void onLogin();

protected:
    /// 登录框必须"能被看见"：此时主窗口尚未 show()，若登录框落在其他窗口后面，用户看到的就是
    /// "双击了但没反应"，任务管理器里却是进程活着、没有任何可关闭的窗口（现场案例）。
    void showEvent(QShowEvent *event) override;

private:
    void setupUI();
    /// 首次使用（空用户表）初始化管理员账号；返回是否成功
    bool initFirstAdmin(const QString &username, const QString &password);

    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QLineEdit *m_confirmPasswordEdit;
    QPushButton *m_loginButton;
    QPushButton *m_cancelButton;
    QLabel *m_statusLabel;

    QString m_loggedInUser;
    QString m_loggedInRole;
};
