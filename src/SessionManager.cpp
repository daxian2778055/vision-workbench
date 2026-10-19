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

void SessionManager::setWritesBlocked(bool blocked)
{
    if (m_writesBlocked == blocked) {
        return;
    }
    m_writesBlocked = blocked;
    emit writesLockChanged(blocked);
}

QString SessionManager::writeGateRefusal() const
{
    if (!m_writesBlocked) {
        return QString();
    }
    return QStringLiteral(
        "出厂默认口令（admin/admin）仍在使用，画布与模块编辑、配置写入"
        "（方案保存/导出、用户管理）已被禁止；"
        "请先修改管理员口令。");
}

bool SessionManager::canManageUsers() const
{
    return !writesBlocked() && hasRoleLevel(QStringLiteral("Admin"));
}

bool SessionManager::canEditScheme() const
{
    return !writesBlocked() && hasRoleLevel(QStringLiteral("Engineer"));
}

bool SessionManager::canRunFlow() const
{
    // 173 行拍板（P0 余量：canRunFlow 只看登录）：翻成「登录且闸未开」。A1 的产线不停线
    // 原义不丢——正在跑的流程不被打断（运行态不经过本判定），闸拦的是新一次运行发起；
    // 改密成功后闸解除，运行自然恢复。原先的「有意不加闸」钉腿就地翻成两向（闸开拒/闸开解）。
    return isLoggedIn() && !writesBlocked();
}

bool SessionManager::canConfigCommunications() const
{
    return !writesBlocked() && hasRoleLevel(QStringLiteral("Engineer"));
}
