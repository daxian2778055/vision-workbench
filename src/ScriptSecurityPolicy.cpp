#include "ScriptSecurityPolicy.h"

#include <QSettings>
#include <QStandardPaths>
#include <QFile>
#include <QTextStream>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <QCryptographicHash>
#include <QProcessEnvironment>
#include <QMessageBox>
#include <QApplication>
#include <QDir>
#include <QProcess>

#if defined(Q_OS_WIN)
#  include <windows.h>
#endif

ScriptSecurityPolicy &ScriptSecurityPolicy::instance()
{
    static ScriptSecurityPolicy s_instance;
    return s_instance;
}

ScriptSecurityPolicy::ScriptSecurityPolicy()
{
    load();
}

void ScriptSecurityPolicy::load()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("scriptSecurity"));
    m_enabled = settings.value(QStringLiteral("enabled"), m_enabled).toBool();
    const QStringList def = { QStringLiteral("Python"), QStringLiteral("Lua") };
    m_allowedLanguages = settings.value(QStringLiteral("allowedLanguages"), def).toStringList();
    m_requireConfirmation = settings.value(QStringLiteral("requireConfirmation"), m_requireConfirmation).toBool();
    m_maxExecutionMs = settings.value(QStringLiteral("maxExecutionMs"), m_maxExecutionMs).toInt();
    m_isolatedPython = settings.value(QStringLiteral("isolatedPython"), m_isolatedPython).toBool();
    m_stripEnvironment = settings.value(QStringLiteral("stripEnvironment"), m_stripEnvironment).toBool();
    m_auditLogEnabled = settings.value(QStringLiteral("auditLogEnabled"), m_auditLogEnabled).toBool();
    m_auditLogPath = settings.value(QStringLiteral("auditLogPath"), m_auditLogPath).toString();
    m_blockWhenElevated = settings.value(QStringLiteral("blockWhenElevated"), m_blockWhenElevated).toBool();
    m_sandboxEnabled = settings.value(QStringLiteral("sandboxEnabled"), m_sandboxEnabled).toBool();
    settings.endGroup();
}

void ScriptSecurityPolicy::save()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("scriptSecurity"));
    settings.setValue(QStringLiteral("enabled"), m_enabled);
    settings.setValue(QStringLiteral("allowedLanguages"), m_allowedLanguages);
    settings.setValue(QStringLiteral("requireConfirmation"), m_requireConfirmation);
    settings.setValue(QStringLiteral("maxExecutionMs"), m_maxExecutionMs);
    settings.setValue(QStringLiteral("isolatedPython"), m_isolatedPython);
    settings.setValue(QStringLiteral("stripEnvironment"), m_stripEnvironment);
    settings.setValue(QStringLiteral("auditLogEnabled"), m_auditLogEnabled);
    settings.setValue(QStringLiteral("auditLogPath"), m_auditLogPath);
    settings.setValue(QStringLiteral("blockWhenElevated"), m_blockWhenElevated);
    settings.setValue(QStringLiteral("sandboxEnabled"), m_sandboxEnabled);
    settings.endGroup();
}

bool ScriptSecurityPolicy::evaluate(const QString &language, const QString & /*script*/, QString &reason) const
{
    if (!m_enabled) {
        reason = QStringLiteral("脚本执行已被全局安全策略禁用");
        return false;
    }
    if (!m_allowedLanguages.contains(language)) {
        reason = QStringLiteral("语言 '%1' 不在允许列表内，禁止执行").arg(language);
        return false;
    }
    if (m_blockWhenElevated && isElevated()) {
        reason = QStringLiteral("安全策略禁止在管理员（提权）身份下执行任意脚本");
        return false;
    }
    return true;
}

bool ScriptSecurityPolicy::requestConfirmation(const QString &language, const QString &script) const
{
    if (!m_requireConfirmation) {
        return true;
    }
    const int previewLen = 400;
    const QString preview = script.size() > previewLen
        ? script.left(previewLen) + QStringLiteral("\n... (已截断)")
        : script;
    const QString text = QStringLiteral(
        "即将执行 %1 脚本（以当前用户权限运行，存在安全风险）。\n\n"
        "仅应运行你完全信任的脚本。脚本内容预览：\n\n%2")
        .arg(language, preview);

    // 无 QApplication（如测试）时不弹窗，直接放行。
    if (!qobject_cast<QApplication *>(QCoreApplication::instance())) {
        return true;
    }
    const int ret = QMessageBox::warning(nullptr, QStringLiteral("脚本执行确认"), text,
                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return ret == QMessageBox::Yes;
}

void ScriptSecurityPolicy::audit(const QString &language, const QString &script,
                                 bool allowed, const QString &reason) const
{
    if (!m_auditLogEnabled) {
        return;
    }
    QString path = m_auditLogPath;
    if (path.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        path = dir + QStringLiteral("/script-audit.log");
    }

    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream ts(&file);

    const QByteArray hash = QCryptographicHash::hash(script.toUtf8(), QCryptographicHash::Sha256).toHex();
    QString user = QString::fromLocal8Bit(qgetenv("USERNAME"));
    if (user.isEmpty()) {
        user = QString::fromLocal8Bit(qgetenv("USER"));
    }

    QJsonObject obj;
    obj[QStringLiteral("time")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    obj[QStringLiteral("user")] = user;
    obj[QStringLiteral("language")] = language;
    obj[QStringLiteral("allowed")] = allowed;
    obj[QStringLiteral("reason")] = reason;
    obj[QStringLiteral("scriptSha256")] = QString::fromLatin1(hash);
    obj[QStringLiteral("scriptLen")] = script.size();

    ts << QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)) << QLatin1Char('\n');
}

QProcessEnvironment ScriptSecurityPolicy::buildEnvironment() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!m_stripEnvironment) {
        return env;
    }
    // 剥离可用于向解释器注入代码的危险环境变量；保留 PATH 以便定位解释器本体。
    const QStringList dangerous = {
        QStringLiteral("PYTHONPATH"),
        QStringLiteral("PYTHONHOME"),
        QStringLiteral("PYTHONSTARTUP"),
        QStringLiteral("PYTHONPYCACHEPREFIX"),
        QStringLiteral("PYTHONUSERSITE"),
        QStringLiteral("PYTHONSAFEPATH"),
        QStringLiteral("LD_PRELOAD"),
        QStringLiteral("LD_LIBRARY_PATH"),
        QStringLiteral("DYLD_INSERT_LIBRARIES"),
        QStringLiteral("DYLD_LIBRARY_PATH"),
        QStringLiteral("BASH_ENV"),
        QStringLiteral("ENV"),
        QStringLiteral("PYTHONPATH_EXTRA")
    };
    for (const QString &key : dangerous) {
        env.remove(key);
    }
    return env;
}

QStringList ScriptSecurityPolicy::interpreterFlags(const QString &language) const
{
    if (language == QStringLiteral("Python") && m_isolatedPython) {
        // -I: 隔离模式（忽略环境、用户 site、PYTHON* 变量）；-E: 完全忽略 PYTHON* 环境变量
        return { QStringLiteral("-I"), QStringLiteral("-E") };
    }
    return {};
}

bool ScriptSecurityPolicy::isElevated() const
{
#if defined(Q_OS_WIN)
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool ok = GetTokenInformation(hToken, TokenElevation, &elevation, size, &size);
    CloseHandle(hToken);
    return ok && elevation.TokenIsElevated;
#else
    return false;
#endif
}

void ScriptSecurityPolicy::applyProcessSandbox(QProcess *proc)
{
#if defined(Q_OS_WIN)
    if (!m_sandboxEnabled) {
        return;
    }
    // Qt 6.11 起 QProcess::CreateProcessArguments 不再提供 token 成员，
    // 因此无法通过 QProcess 钩子施加受限令牌（去特权/低完整性）降权；
    // 保留 CREATE_BREAKAWAY_FROM_JOB，子进程仍可脱离父作业并被纳入本进程 Job Object（父退出即终止子进程）。
    proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_BREAKAWAY_FROM_JOB;
    });
#else
    Q_UNUSED(proc)
#endif
}

bool ScriptSecurityPolicy::attachJob(QProcess *proc)
{
#if defined(Q_OS_WIN)
    if (!m_sandboxEnabled) {
        return false;
    }
    const HANDLE job = CreateJobObject(nullptr, nullptr);
    if (!job) {
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION ext{};
    ext.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
                                         | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION
                                         | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    ext.ProcessMemoryLimit = 512 * 1024 * 1024; // 单脚本进程工作集上限 512MB
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &ext, sizeof(ext));

    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_DESKTOP
                           | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS
                           | JOB_OBJECT_UILIMIT_EXITWINDOWS
                           | JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS
                           | JOB_OBJECT_UILIMIT_READCLIPBOARD
                           | JOB_OBJECT_UILIMIT_WRITECLIPBOARD
                           | JOB_OBJECT_UILIMIT_HANDLES;
    SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui, sizeof(ui));

    const qint64 pid = proc->processId();
    if (pid <= 0) {
        CloseHandle(job);
        return false;
    }
    const HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE | PROCESS_DUP_HANDLE
                                 | PROCESS_QUERY_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) {
        CloseHandle(job);
        return false;
    }
    const BOOL ok = AssignProcessToJobObject(job, h);
    CloseHandle(h);
    if (!ok) {
        CloseHandle(job);
        qWarning() << "[ScriptSecurityPolicy] AssignProcessToJobObject 失败"
                      "（父进程可能处于不可跳出的作业中，脚本未加沙箱运行）";
        return false;
    }
    proc->setProperty("scriptJobHandle", static_cast<qulonglong>(reinterpret_cast<quintptr>(job)));
    return true;
#else
    Q_UNUSED(proc)
    return false;
#endif
}

void ScriptSecurityPolicy::closeJob(QProcess *proc)
{
#if defined(Q_OS_WIN)
    const qulonglong v = proc->property("scriptJobHandle").toULongLong();
    if (v) {
        CloseHandle(reinterpret_cast<HANDLE>(v));
        proc->setProperty("scriptJobHandle", QVariant());
    }
#else
    Q_UNUSED(proc)
#endif
}

#if defined(Q_OS_WIN)
void ScriptSecurityPolicy::ensureRestrictedToken()
{
    if (m_restrictedToken) {
        return;
    }
    HANDLE hProc = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                           TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &hProc)) {
        qWarning() << "[ScriptSecurityPolicy] OpenProcessToken 失败，沙箱降级为无降权";
        return;
    }

    DWORD userLen = 0;
    GetTokenInformation(hProc, TokenUser, nullptr, 0, &userLen);
    QByteArray userBuf(static_cast<int>(userLen), 0);
    auto *tu = reinterpret_cast<TOKEN_USER *>(userBuf.data());
    if (!GetTokenInformation(hProc, TokenUser, tu, userLen, &userLen)) {
        CloseHandle(hProc);
        return;
    }
    SID_AND_ATTRIBUTES sattr{};
    sattr.Sid = tu->User.Sid;
    sattr.Attributes = 0;

    HANDLE hRestricted = nullptr;
    // 优先：去除全部特权，并把管理员 SID 限制为 deny-only（无法再提权/写受保护对象）
    if (!CreateRestrictedToken(hProc, DISABLE_MAX_PRIVILEGE, 0, nullptr, 0, nullptr,
                               1, &sattr, &hRestricted)) {
        // 退化：仅去除特权（仍比原令牌安全）
        if (!CreateRestrictedToken(hProc, DISABLE_MAX_PRIVILEGE, 0, nullptr, 0, nullptr,
                                   0, nullptr, &hRestricted)) {
            CloseHandle(hProc);
            return;
        }
    }

    // 低完整性级别：无法写入中/高完整性对象，进一步限制破坏面（仍可读取用户文件/联网）
    SID_IDENTIFIER_AUTHORITY sia = SECURITY_MANDATORY_LABEL_AUTHORITY;
    PSID lowSid = nullptr;
    if (AllocateAndInitializeSid(&sia, 1, SECURITY_MANDATORY_LOW_RID,
                                 0, 0, 0, 0, 0, 0, 0, &lowSid)) {
        TOKEN_MANDATORY_LABEL tml{};
        tml.Label.Attributes = SE_GROUP_INTEGRITY;
        tml.Label.Sid = lowSid;
        SetTokenInformation(hRestricted, TokenIntegrityLevel, &tml, sizeof(tml));
        FreeSid(lowSid);
    }

    m_restrictedToken = hRestricted;
    CloseHandle(hProc);
}
#endif

