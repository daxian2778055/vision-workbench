#pragma once

#include <QString>
#include <QStringList>
#include <QProcessEnvironment>
#include <QMutex>
#include <functional>

class QProcess;

/// ScriptNode 任意代码执行安全策略
///
/// ScriptNode 通过 QProcess 以当前用户完整权限启动 python/lua 解释器执行任意脚本，
/// 属于高危能力。本策略提供一层可配置的安全护栏：
///   - 总开关（enabled）：一键禁用所有脚本执行
///   - 语言白名单（allowedLanguages）：仅允许受控语言
///   - 解释器隔离（isolatedPython）：Python 以 -I -E 隔离模式运行（忽略用户 site / 环境变量）
///   - 环境清理（stripEnvironment）：剥离可注入解释器的危险环境变量
///   - 执行超时（maxExecutionMs）：防止脚本挂死（沿用既有 30s 上限）
///   - 管理员降权提示（blockWhenElevated）：以管理员身份运行时拒绝执行（默认仅告警）
///   - 审计日志（auditLogEnabled）：记录每次执行的脚本哈希、语言、允许/拒绝原因，便于溯源
///   - 交互确认（requireConfirmation）：执行前向用户弹窗确认（默认关闭，可由配置开启）
///
/// 配置通过 QSettings（分组 "scriptSecurity"）持久化；首次运行使用下方安全默认值。
class ScriptSecurityPolicy
{
public:
    static ScriptSecurityPolicy &instance();

    void load();
    void save();

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool v) { m_enabled = v; }

    QStringList allowedLanguages() const { return m_allowedLanguages; }
    void setAllowedLanguages(const QStringList &v) { m_allowedLanguages = v; }

    bool requireConfirmation() const { return m_requireConfirmation; }
    void setRequireConfirmation(bool v) { m_requireConfirmation = v; }

    int maxExecutionMs() const { return m_maxExecutionMs; }
    void setMaxExecutionMs(int v) { m_maxExecutionMs = v; }

    bool isolatedPython() const { return m_isolatedPython; }
    void setIsolatedPython(bool v) { m_isolatedPython = v; }

    bool stripEnvironment() const { return m_stripEnvironment; }
    void setStripEnvironment(bool v) { m_stripEnvironment = v; }

    bool auditLogEnabled() const { return m_auditLogEnabled; }
    void setAuditLogEnabled(bool v) { m_auditLogEnabled = v; }

    QString auditLogPath() const { return m_auditLogPath; }
    void setAuditLogPath(const QString &v) { m_auditLogPath = v; }

    bool blockWhenElevated() const { return m_blockWhenElevated; }
    void setBlockWhenElevated(bool v) { m_blockWhenElevated = v; }

    bool isSandboxEnabled() const { return m_sandboxEnabled; }
    void setSandboxEnabled(bool v) { m_sandboxEnabled = v; }

    /// 沙箱强度：
    ///   - PrivilegeStripped（默认）：去特权 + Low IL + Job Object + 句柄收敛。兼容性最好。
    ///   - AppContainer：在上一项基础上改用「普通用户令牌 + AppContainer 容器 SID」启动，
    ///     得到真正独立的文件/注册表视图（只能访问显式授权的路径 + 自己的容器目录），
    ///     且**不给任何能力 → 无网络**。
    /// 实测数据（restricted_token_probe.exe -ac）：容器内进程可正常启动；写用户临时目录被拒；
    /// 写容器专属目录成功；给解释器目录授权 (RX) 后 Python 可正常运行。
    /// 代价：解释器目录需要一次性 ACL 授权（grantInterpreterAccess）；配置文件首次创建
    /// 需要管理员权限；拿不到容器 SID 时**拒绝执行**（fail-closed，不会静默降级）。
    enum class SandboxMode { PrivilegeStripped, AppContainer };

    ScriptSecurityPolicy::SandboxMode sandboxMode() const { return m_sandboxMode; }
    void setSandboxMode(SandboxMode mode) { m_sandboxMode = mode; }

    /// 建立或派生 AppContainer 配置文件（幂等）。首次创建需要管理员权限。
    /// 返回 false 时 error 说明原因（此调用本身不抛错、不降级）。
    static bool ensureAppContainerProfile(QString *error = nullptr);
    /// 当前用户的 AppContainer 配置文件 SID 字符串；未建立时返回空。
    static QString appContainerSid();
    /// 为容器 SID 授予「读取 + 执行」权限，使解释器能在容器内启动。
    /// program 可以是解释器路径或 PATH 中的命令名（取其所在目录，含子目录）。
    /// 需要管理员权限；ACL 是持久的，通常只需执行一次。失败时 error 说明原因。
    static bool grantInterpreterAccess(const QString &program, QString *error = nullptr);

    /// 评估脚本是否允许执行。拒绝时通过 reason 返回原因。
    bool evaluate(const QString &language, const QString &script, QString &reason) const;

    /// 交互确认。requireConfirmation 为 false 时直接返回 true（即放行）。
    bool requestConfirmation(const QString &language, const QString &script) const;

    /// 记录一次执行尝试到审计日志。
    void audit(const QString &language, const QString &script, bool allowed, const QString &reason) const;

    /// 构造受限的进程环境变量（剥离可注入变量，保留 PATH 以便定位解释器）。
    QProcessEnvironment buildEnvironment() const;

    /// 解释器隔离参数，例如 Python 返回 {"-I", "-E"}。
    QStringList interpreterFlags(const QString &language) const;

    /// 当前进程是否以管理员（提权）身份运行。
    bool isElevated() const;

    /// 进程隔离（仅 Windows 生效，其它平台为空操作）：
    ///   - applyProcessSandbox：start() 之前调用（仅 QProcess 路径）。只设置
    ///     CREATE_BREAKAWAY_FROM_JOB，使子进程可脱离父作业并被纳入 Job Object 生命周期管理。
    ///     【重要】Qt 6.11 起 QProcess::CreateProcessArguments 不再提供 token 成员，
    ///     QProcess 路径自身无法降权；需要降权请使用 runWithRestrictedToken()
    ///     （CreateProcessAsUserW + 受限令牌，当前提供「去特权」）。
    ///   - attachJob：start() 成功之后调用，把子进程关入 Job Object，限制其
    ///     创建桌面/改显示设置/退出 Windows/读写剪贴板/跨句柄；并设置
    ///     KILL_ON_JOB_CLOSE，作业句柄关闭时整棵进程树被强杀，超时可控。
    ///     返回 false 表示 Job 建立/加入失败；沙箱开启时调用方必须拒绝执行脚本。
    ///   - closeJob：执行结束（含超时 kill 之后）调用，关闭作业句柄释放并触发杀树。
    /// 边界：Job Object 不限制网络访问；无受限令牌时脚本以当前用户完整权限运行，
    /// 仅适用于「内部可信脚本」场景，不构成不可信代码的安全隔离。
    void applyProcessSandbox(QProcess *proc);
    bool attachJob(QProcess *proc);
    void closeJob(QProcess *proc);

    /// 以「受限令牌」启动子进程并收集 stdout/stderr（仅 Windows 实现；其它平台返回 false）。
    ///
    /// 这是 Qt 6.11 移除 QProcess token 钩子后恢复降权的替代路径：用本次进程的受限令牌副本
    /// 启动解释器。**当前实际边界（勿夸大）**：
    ///   - 已施加：去除全部特权（仅剩 SeChangeNotifyPrivilege）；
    ///   - 已施加：低完整性级别（Low IL）——脚本无法写入 Medium 完整性的用户目录
    ///     （即无法改动用户文件/持久化），读取不受影响；
    ///   - 已施加：每次执行独立的 Low 私有临时目录，并把子进程 TEMP/TMP 指过去
    ///     （否则依赖 tempfile 的脚本会失败），执行结束即删除；
    ///   - 已施加：子进程脱离父作业并关入独立 Job Object，限制 UI/剪贴板/内存，
    ///     且 Job Object 的创建/配置/加入任一失败即终止子进程并拒绝执行（fail-closed）；
    ///   - 已施加：仅 stdout/stderr 两个管道写端被继承（PROC_THREAD_ATTRIBUTE_HANDLE_LIST），
    ///     父进程其它可继承句柄不会泄漏给脚本进程；
    ///   - 未施加：管理员组 deny-only、受限 SID 列表——实测二者均使**任何**子进程以
    ///     0xC0000142 退出，并已用「调试器 + 加载器快照」定位到确切失败点：Windows 自身的
    ///     KERNELBASE.dll 在 DLL_PROCESS_ATTACH 阶段失败（restricted_token_probe.exe -dbg 可复现）；
    ///   - 未施加：专用窗口站/桌面、网络隔离。
    /// 因此只能描述为「已去特权 + 低完整性 + 进程树/句柄约束」，不等于完整沙箱。
    /// - timeoutMs <= 0 表示不限时；cancelRequested() 返回 true 时立即终止子进程。
    /// - 返回 false 时 error 说明原因（令牌不可用/进程创建失败/进程隔离失败/超时/被取消）。
    bool runWithRestrictedToken(const QString &program,
                               const QStringList &args,
                               int timeoutMs,
                               const std::function<bool()> &cancelRequested,
                               QString &stdOut,
                               QString &stdErr,
                               QString &error,
                               int *exitCode = nullptr);

    /// 受限令牌是否已就绪（仅 Windows）；供自检/诊断使用
    bool hasRestrictedToken() const;

    /// 受限令牌自检摘要（用于上线验收/现场排查），形如：
    ///   privileges=1 adminSid=enabled integrity=Medium
    /// 字段含义：剩余特权数（去特权后应为 1，仅 SeChangeNotifyPrivilege）/
    /// 管理员组状态 / 完整性级别。
    /// 说明：adminSid 与 integrity 反映"实际生效"的降权程度，不夸大。
    QString restrictedTokenSelfCheck();

private:
    ScriptSecurityPolicy();
    /// 创建受限令牌（去特权）。注意：管理员组 deny-only 与低完整性级别
    /// 经实测会导致子进程 0xC0000142 STATUS_DLL_INIT_FAILED（专用窗口站/桌面
    /// 与降标签均未解决），故当前未施加，详见 .cpp 内注释。
    void ensureRestrictedToken();

    bool m_enabled = true;
    QStringList m_allowedLanguages = { QStringLiteral("Python"), QStringLiteral("Lua") };
    bool m_requireConfirmation = false;
    int m_maxExecutionMs = 30000;
    bool m_isolatedPython = true;
    bool m_stripEnvironment = true;
    bool m_auditLogEnabled = true;
    QString m_auditLogPath;
    bool m_blockWhenElevated = false;
#if defined(Q_OS_WIN)
    bool m_sandboxEnabled = true;
#else
    /// 受限令牌与 Job Object 仅有 Windows 实现：非 Windows 平台若默认开启，
    /// 脚本会走「沙箱不可用」的 fail-closed 分支而完全无法执行，故此处默认关闭。
    bool m_sandboxEnabled = false;
#endif
    SandboxMode m_sandboxMode = SandboxMode::PrivilegeStripped;   // 沙箱强度（见上方 SandboxMode）
    void *m_restrictedToken = nullptr;        // Windows HANDLE；非 Windows 恒为 nullptr
    mutable QMutex m_tokenMutex;              // 保护 m_restrictedToken 的创建与读取（多流程并发执行脚本）
};
