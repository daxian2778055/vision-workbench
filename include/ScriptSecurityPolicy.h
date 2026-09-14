#pragma once

#include <QString>
#include <QStringList>
#include <QProcessEnvironment>
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
    /// 这是 Qt 6.11 移除 QProcess token 钩子后，恢复"去特权 + 管理员 SID deny-only +
    /// 低完整性"降权的替代路径：使用本次进程的受限令牌副本启动解释器，
    /// 因此子进程拿不到当前用户的完整权限。
    /// - 子进程会脱离父作业并被关入独立 Job Object（父退出即终止整棵进程树）。
    /// - timeoutMs <= 0 表示不限时；cancelRequested() 返回 true 时立即终止子进程。
    /// - 返回 false 时 error 说明原因（令牌不可用/进程创建失败/超时/被取消）。
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

private:
    ScriptSecurityPolicy();
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
    bool m_sandboxEnabled = true;
    void *m_restrictedToken = nullptr;   // Windows HANDLE；非 Windows 恒为 nullptr
};
