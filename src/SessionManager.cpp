#include "SessionManager.h"

SessionManager *SessionManager::instance()
{
    static SessionManager s_instance;
    return &s_instance;
}

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
{
}

void SessionManager::login(const QString &user, const QString &role)
{
    m_user = user;
    m_role = role.isEmpty() ? QStringLiteral("Operator") : role;
    emit sessionChanged(m_user, m_role);
}

void SessionManager::logout()
{
    m_user.clear();
    m_role.clear();
    emit sessionChanged(m_user, m_role);
}

int SessionManager::roleLevel(const QString &role)
{
    if (role.compare(QStringLiteral("Admin"), Qt::CaseInsensitive) == 0) return 3;
    if (role.compare(QStringLiteral("Engineer"), Qt::CaseInsensitive) == 0) return 2;
    if (role.compare(QStringLiteral("Operator"), Qt::CaseInsensitive) == 0) return 1;
    return 0;
}

bool SessionManager::hasRoleLevel(const QString &requiredRole) const
{
    return roleLevel(m_role) >= roleLevel(requiredRole);
}

bool SessionManager::canManageUsers() const
{
    return hasRoleLevel(QStringLiteral("Admin"));
}

bool SessionManager::canEditScheme() const
{
    return hasRoleLevel(QStringLiteral("Engineer"));
}

bool SessionManager::canRunFlow() const
{
    return isLoggedIn();
}

bool SessionManager::canConfigCommunications() const
{
    return hasRoleLevel(QStringLiteral("Engineer"));
}
