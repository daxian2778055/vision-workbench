#pragma once

#include <QObject>
#include <QString>

/// 会话/权限管理 — 记录当前登录用户与角色，提供角色权限判断
/// 角色等级：Admin(3) > Engineer(2) > Operator(1)
class SessionManager : public QObject
{
    Q_OBJECT
public:
    static SessionManager *instance();

    void login(const QString &user, const QString &role);
    void logout();

    QString currentUser() const { return m_user; }
    QString currentRole() const { return m_role; }
    bool isLoggedIn() const { return !m_user.isEmpty(); }

    /// 角色级别
    static int roleLevel(const QString &role);
    /// 当前用户是否至少达到指定角色级别
    bool hasRoleLevel(const QString &requiredRole) const;

    /// 常用权限判断
    bool canManageUsers() const;    ///< Admin
    bool canEditScheme() const;     ///< Admin / Engineer
    bool canRunFlow() const;        ///< 所有角色
    bool canConfigCommunications() const; ///< Admin / Engineer

signals:
    void sessionChanged(const QString &user, const QString &role);

private:
    SessionManager(QObject *parent = nullptr);
    ~SessionManager() override = default;
    SessionManager(const SessionManager &) = delete;
    SessionManager &operator=(const SessionManager &) = delete;

    QString m_user;
    QString m_role;
};
