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

    // ---- 写操作闸（A1-①）----
    /// 出厂默认口令仍在使用时置位：禁止**配置写入类**操作——方案覆盖保存/导出、
    /// 通信配置落盘、用户管理（否则拿到 admin/admin 就等于拿到全部权限，闸也就白设）。
    /// 不影响 canRunFlow 与运行期结果落库：产线正在跑的活不因未改口令而中断，
    /// 这是"拦住未鉴权的配置变更"而非"拒绝服务"。
    /// 默认 false：数据库不可用/未装配的单测环境不受影响（闸只在显式置位时生效）。
    void setWritesBlocked(bool blocked);
    bool writesBlocked() const { return m_writesBlocked; }
    /// 写操作闸的拒绝理由；返回空串表示放行。判据与文案**只有这一处定义**：方案落盘
    /// （ProjectManager）、用户表写入（AppDatabase）与菜单可用态都取它，避免各写一份而彼此漂移。
    QString writeGateRefusal() const;

signals:
    void sessionChanged(const QString &user, const QString &role);
    /// 闸状态变化（UI 据此重算菜单/动作可用态）
    void writesLockChanged(bool blocked);

private:
    SessionManager(QObject *parent = nullptr);
    ~SessionManager() override = default;
    SessionManager(const SessionManager &) = delete;
    SessionManager &operator=(const SessionManager &) = delete;

    QString m_user;
    QString m_role;
    bool m_writesBlocked = false;
};
