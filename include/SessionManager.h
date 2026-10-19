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
    /// 173 行拍板：登录且写闸未开（「闸开不发新跑」）。正在跑的流程不受此判定影响
    /// （产线不停线原义保留在运行态那半边），改密成功闸解除后自动恢复。
    bool canRunFlow() const;        ///< 所有角色
    bool canConfigCommunications() const; ///< Admin / Engineer

    // ---- 写操作闸（A1-①）----
    /// 出厂默认口令仍在使用时置位：禁止**配置写入类**操作——方案覆盖保存/导出、
    /// 通信配置落盘、用户管理（否则拿到 admin/admin 就等于拿到全部权限，闸也就白设）。
    /// 不影响运行期结果落库；canRunFlow 于 173 行拍板随闸收拢（闸开不发新跑，跑着的照跑），
    /// 「产线正在跑的活不因未改口令而中断」仍是边界（这是拦未鉴权态的发跑与配置变更，不是拒绝服务）。
    /// 默认 false：数据库不可用/未装配的单测环境不受影响（闸只在显式置位时生效）。
    void setWritesBlocked(bool blocked);
    bool writesBlocked() const { return m_writesBlocked; }
    /// 写操作闸的拒绝理由；返回空串表示放行。判据与文案**只有这一处定义**：方案落盘
    /// （ProjectManager）、用户表写入（AppDatabase）与菜单可用态都取它，避免各写一份而彼此漂移。
    QString writeGateRefusal() const;
    /// 画布编辑锁的组合判据（P0）：运行态锁图（产线语义，运行中场景不可改）与写闸
    /// （出厂口令在用 ⇒ 画布与模块编辑一并禁止，否则"改完就跑"绕过落库闸）取或。
    /// 决策收在 SessionManager 这一处，MainWindow 的各处接线只是分发它（可单测）。
    bool canvasEditShouldBeLocked(bool runnerLockedGraph) const
    {
        return runnerLockedGraph || m_writesBlocked;
    }

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
