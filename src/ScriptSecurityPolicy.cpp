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
#include <QElapsedTimer>
#include <QStringList>
#include <algorithm>
#include <vector>

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
    // 多个流程可并发执行脚本，这里必须串行化：否则会重复创建令牌并泄漏句柄（P1）
    QMutexLocker locker(&m_tokenMutex);
    if (m_restrictedToken) {
        return;
    }
    HANDLE hProc = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                           TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &hProc)) {
        qWarning() << "[ScriptSecurityPolicy] OpenProcessToken 失败，沙箱降级为无降权";
        return;
    }

    // 关于「管理员组 deny-only」：本机实测一旦施加，子进程即以 0xC0000142
    // STATUS_DLL_INIT_FAILED 退出（默认桌面、专用窗口站/桌面、以及把对象标签降为
    // Medium 三种情形均如此），说明其成因不止"桌面访问"一项，需借助 Process Monitor
    // 等工具进一步定位后才能启用，故当前不施加。
    //
    // 历史坑（务必核对参数位置）：CreateRestrictedToken 的 deny-only 位于第 3/4 个参数
    // (DisableSidCount / SidsToDisable)；第 7/8 个参数是 RestrictedSidCount /
    // SidsToRestrict（受限 SID 列表，语义完全不同——那会让令牌只能访问显式授权这些
    // SID 的对象）。早期实现把条目放到了第 7/8 位，因此 deny-only 从未真正生效；
    // 而把「用户 SID」放进受限列表会让子进程失去一切访问权，启动即 0xC0000022。

    HANDLE hRestricted = nullptr;
    // 去除全部特权（仅保留 SeChangeNotifyPrivilege）
    if (!CreateRestrictedToken(hProc, DISABLE_MAX_PRIVILEGE,
                               0, nullptr,   // SidsToDisable（暂不施加 deny-only，见上）
                               0, nullptr,   // PrivilegesToDelete
                               0, nullptr,   // SidsToRestrict
                               &hRestricted)) {
        CloseHandle(hProc);
        return;
    }

    // 低完整性级别（Low IL）暂不施加：需在专用窗口站/桌面上设置低完整性标签
    // （SACL + 完整性 ACE），属后续独立项。
    //
    // 当前降权边界（务必如实对外描述，不得夸大）：
    //   已施加：去除全部特权（仅剩 SeChangeNotifyPrivilege）
    //   未施加：管理员组 deny-only（实测导致子进程 0xC0000142，见上文）
    //   未施加：低完整性级别、专用窗口站/桌面（CreateProcessAsUserW 的 lpDesktop 传 nullptr，
    //           子进程继承当前桌面）、网络隔离
    //   结论：只能描述为「已去特权」，不能描述为「管理员权限已降级」。

    m_restrictedToken = hRestricted;
    CloseHandle(hProc);
}

QString ScriptSecurityPolicy::restrictedTokenSelfCheck()
{
    ensureRestrictedToken();
    QMutexLocker locker(&m_tokenMutex);   // 与创建路径互斥，避免读到半初始化状态（P1）
    if (!m_restrictedToken) {
        return QStringLiteral("restrictedToken=unavailable");
    }
    const HANDLE tok = reinterpret_cast<HANDLE>(m_restrictedToken);

    // 剩余特权数（DISABLE_MAX_PRIVILEGE 后应只剩 SeChangeNotifyPrivilege）
    int privCount = -1;
    DWORD privLen = 0;
    GetTokenInformation(tok, TokenPrivileges, nullptr, 0, &privLen);
    if (privLen > 0) {
        QByteArray privBuf(static_cast<int>(privLen), 0);
        if (GetTokenInformation(tok, TokenPrivileges, privBuf.data(), privLen, &privLen)) {
            privCount = int(reinterpret_cast<TOKEN_PRIVILEGES *>(privBuf.data())->PrivilegeCount);
        }
    }

    // 管理员组状态
    QString adminState = QStringLiteral("absent");
    BYTE adminBuf[SECURITY_MAX_SID_SIZE] = {};
    DWORD adminSize = sizeof(adminBuf);
    if (CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminBuf, &adminSize)) {
        DWORD groupsLen = 0;
        GetTokenInformation(tok, TokenGroups, nullptr, 0, &groupsLen);
        if (groupsLen > 0) {
            QByteArray groupsBuf(static_cast<int>(groupsLen), 0);
            if (GetTokenInformation(tok, TokenGroups, groupsBuf.data(), groupsLen, &groupsLen)) {
                auto *groups = reinterpret_cast<TOKEN_GROUPS *>(groupsBuf.data());
                for (DWORD i = 0; i < groups->GroupCount; ++i) {
                    if (EqualSid(groups->Groups[i].Sid, adminBuf)) {
                        adminState = (groups->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY)
                                         ? QStringLiteral("deny-only")
                                         : QStringLiteral("enabled");
                        break;
                    }
                }
            }
        }
    }

    // 完整性级别
    QString integrity = QStringLiteral("unknown");
    DWORD ilLen = 0;
    GetTokenInformation(tok, TokenIntegrityLevel, nullptr, 0, &ilLen);
    if (ilLen > 0) {
        QByteArray ilBuf(static_cast<int>(ilLen), 0);
        if (GetTokenInformation(tok, TokenIntegrityLevel, ilBuf.data(), ilLen, &ilLen)) {
            auto *tml = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(ilBuf.data());
            const UCHAR *countPtr = GetSidSubAuthorityCount(tml->Label.Sid);
            const DWORD rid = (countPtr && *countPtr > 0)
                                  ? *GetSidSubAuthority(tml->Label.Sid, DWORD(*countPtr - 1))
                                  : 0;
            if (rid >= SECURITY_MANDATORY_HIGH_RID) {
                integrity = QStringLiteral("High");
            } else if (rid >= SECURITY_MANDATORY_MEDIUM_RID) {
                integrity = QStringLiteral("Medium");
            } else if (rid >= SECURITY_MANDATORY_LOW_RID) {
                integrity = QStringLiteral("Low");
            } else {
                integrity = QStringLiteral("Untrusted(%1)").arg(rid);
            }
        }
    }

    return QStringLiteral("privileges=%1 adminSid=%2 integrity=%3")
        .arg(privCount)
        .arg(adminState)
        .arg(integrity);
}

namespace {

/// 按 Windows 命令行解析规则为参数加引号（反斜杠与引号需转义）
QString quoteWindowsArg(const QString &arg)
{
    if (!arg.isEmpty()
        && !arg.contains(QLatin1Char(' '))
        && !arg.contains(QLatin1Char('\t'))
        && !arg.contains(QLatin1Char('"'))) {
        return arg;
    }
    QString out = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar c : arg) {
        if (c == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (c == QLatin1Char('"')) {
            out += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            out += QLatin1Char('"');
            backslashes = 0;
            continue;
        }
        out += QString(backslashes, QLatin1Char('\\'));
        backslashes = 0;
        out += c;
    }
    out += QString(backslashes * 2, QLatin1Char('\\'));
    out += QLatin1Char('"');
    return out;
}

/// 构造 UTF-16 环境块（"KEY=VALUE\0...\0"），用于 CREATE_UNICODE_ENVIRONMENT
QByteArray buildEnvironmentBlock(const QProcessEnvironment &env)
{
    QStringList entries = env.toStringList();
    if (entries.isEmpty()) {
        return QByteArray();
    }
    std::sort(entries.begin(), entries.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    QByteArray block;
    for (const QString &entry : entries) {
        block.append(reinterpret_cast<const char *>(entry.utf16()), entry.size() * 2);
        block.append('\0');
        block.append('\0');
    }
    block.append('\0');
    block.append('\0');
    return block;
}

/// 非阻塞排空管道中已到达的数据
void drainPipe(HANDLE pipe, QByteArray &sink)
{
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            return;   // 管道已断开（子进程退出且写端关闭）
        }
        if (available == 0) {
            return;
        }
        char buf[4096];
        const DWORD want = static_cast<DWORD>(qMin<qulonglong>(available, sizeof(buf)));
        DWORD read = 0;
        if (!ReadFile(pipe, buf, want, &read, nullptr) || read == 0) {
            return;
        }
        sink.append(buf, static_cast<int>(read));
    }
}

} // namespace

bool ScriptSecurityPolicy::hasRestrictedToken() const
{
    QMutexLocker locker(&m_tokenMutex);
    return m_restrictedToken != nullptr;
}

bool ScriptSecurityPolicy::runWithRestrictedToken(const QString &program,
                                                  const QStringList &args,
                                                  int timeoutMs,
                                                  const std::function<bool()> &cancelRequested,
                                                  QString &stdOut,
                                                  QString &stdErr,
                                                  QString &error,
                                                  int *exitCode)
{
    stdOut.clear();
    stdErr.clear();
    error.clear();
    if (exitCode) {
        *exitCode = -1;
    }

    ensureRestrictedToken();
    HANDLE restrictedToken = nullptr;
    {
        QMutexLocker locker(&m_tokenMutex);   // 与创建路径互斥（P1）
        restrictedToken = reinterpret_cast<HANDLE>(m_restrictedToken);
    }
    if (!restrictedToken) {
        error = QStringLiteral("受限令牌不可用");
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;
    HANDLE errRead = nullptr;
    HANDLE errWrite = nullptr;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)
        || !CreatePipe(&errRead, &errWrite, &sa, 0)) {
        if (outRead) CloseHandle(outRead);
        if (outWrite) CloseHandle(outWrite);
        if (errRead) CloseHandle(errRead);
        if (errWrite) CloseHandle(errWrite);
        error = QStringLiteral("创建输出管道失败");
        return false;
    }
    // 父进程持有的读端不能被继承，否则管道永远不会收到 EOF
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    QString commandLine = quoteWindowsArg(program);
    for (const QString &a : args) {
        commandLine += QLatin1Char(' ') + quoteWindowsArg(a);
    }
    std::vector<wchar_t> cmdBuf(static_cast<size_t>(commandLine.size()) + 1, L'\0');
    commandLine.toWCharArray(cmdBuf.data());

    const QByteArray envBlock = buildEnvironmentBlock(buildEnvironment());

    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(STARTUPINFOW);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    siex.StartupInfo.hStdInput = nullptr;
    siex.StartupInfo.hStdOutput = outWrite;
    siex.StartupInfo.hStdError = errWrite;

    // 只允许 stdout/stderr 两个管道写端被继承（P1）：否则父进程中所有带 HANDLE_FLAG_INHERIT
    // 的句柄（文件/设备/同步对象）都会被一并继承，而受限令牌并不能收回这些句柄自身的访问权。
    // 注意：使用 PROC_THREAD_ATTRIBUTE_HANDLE_LIST 时 bInheritHandles 仍须为 TRUE——
    // 该属性起「过滤器」作用；若传 FALSE，连 stdout/stderr 都不会被继承，输出捕获会失效。
    bool inheritFiltered = false;
#if defined(PROC_THREAD_ATTRIBUTE_HANDLE_LIST)
    HANDLE inheritList[2] = { outWrite, errWrite };
    SIZE_T attrBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrBytes);
    std::vector<BYTE> attrBuf(attrBytes);
    LPPROC_THREAD_ATTRIBUTE_LIST attrList =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    if (attrBytes > 0
        && InitializeProcThreadAttributeList(attrList, 1, 0, &attrBytes)
        && UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                     inheritList, sizeof(inheritList), nullptr, nullptr)) {
        siex.StartupInfo.cb = sizeof(siex);   // 使用扩展结构时 cb 必须为 STARTUPINFOEXW 大小
        siex.lpAttributeList = attrList;
        inheritFiltered = true;
    } else {
        qWarning() << "[ScriptSecurityPolicy] 句柄继承白名单不可用，退回默认继承语义（仍保留受限令牌）";
    }
#endif

    PROCESS_INFORMATION pi{};
    const DWORD baseFlags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT
                            | (inheritFiltered ? EXTENDED_STARTUPINFO_PRESENT : 0);
    LPVOID envPtr = envBlock.isEmpty() ? nullptr : const_cast<char *>(envBlock.constData());

    auto spawnRestricted = [&](DWORD extraFlags) {
        return CreateProcessAsUserW(restrictedToken,
                                    nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                    baseFlags | extraFlags,
                                    envPtr, nullptr, &siex.StartupInfo, &pi);
    };

    // 优先脱离父作业（便于关入自己的 Job Object）；若父作业不允许脱离则退回不带该标志
    BOOL created = spawnRestricted(CREATE_BREAKAWAY_FROM_JOB);
    if (!created) {
        created = spawnRestricted(0);
    }
#if defined(PROC_THREAD_ATTRIBUTE_HANDLE_LIST)
    if (inheritFiltered) {
        DeleteProcThreadAttributeList(attrList);   // 进程已创建，属性列表即可释放
    }
#endif
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (!created) {
        const DWORD lastError = GetLastError();
        CloseHandle(outRead);
        CloseHandle(errRead);
        error = QStringLiteral("以受限令牌创建进程失败（GetLastError=%1）").arg(lastError);
        return false;
    }

    // 独立 Job Object：父退出（句柄关闭）即终止整棵脚本进程树。
    // 创建/配置/加入任一步失败都必须 fail-closed（P1）：否则超时或取消时只能终止根进程，
    // 脚本派生的子进程会继续存活——Job Object 是「进程树回收」的唯一保障。这与 QProcess
    // 路径的契约一致（ScriptNode：attachJob 失败即拒绝执行）。
    HANDLE job = CreateJobObject(nullptr, nullptr);
    bool jobReady = (job != nullptr);
    if (jobReady) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION ext{};
        ext.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
                                             | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        ext.ProcessMemoryLimit = 512 * 1024 * 1024;
        jobReady = SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                           &ext, sizeof(ext)) != FALSE;
        if (jobReady) {
            jobReady = AssignProcessToJobObject(job, pi.hProcess) != FALSE;
        }
    }
    if (!jobReady) {
        const DWORD jobError = GetLastError();
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(outRead);
        CloseHandle(errRead);
        if (job) {
            CloseHandle(job);
        }
        error = QStringLiteral("脚本进程隔离（Job Object）建立失败，已拒绝执行（GetLastError=%1）")
                    .arg(jobError);
        return false;
    }

    QByteArray outBuf;
    QByteArray errBuf;
    QElapsedTimer timer;
    timer.start();
    bool cancelled = false;
    bool timedOut = false;
    for (;;) {
        drainPipe(outRead, outBuf);
        drainPipe(errRead, errBuf);
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            drainPipe(outRead, outBuf);
            drainPipe(errRead, errBuf);
            break;
        }
        if (cancelRequested && cancelRequested()) {
            cancelled = true;
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
            drainPipe(outRead, outBuf);
            drainPipe(errRead, errBuf);
            break;
        }
        if (timeoutMs > 0 && timer.elapsed() >= timeoutMs) {
            timedOut = true;
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
            drainPipe(outRead, outBuf);
            drainPipe(errRead, errBuf);
            break;
        }
    }

    DWORD exitValue = 0;
    GetExitCodeProcess(pi.hProcess, &exitValue);
    if (exitCode) {
        *exitCode = static_cast<int>(exitValue);
    }
    stdOut = QString::fromLocal8Bit(outBuf);
    stdErr = QString::fromLocal8Bit(errBuf);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(outRead);
    CloseHandle(errRead);
    if (job) {
        CloseHandle(job);   // KILL_ON_JOB_CLOSE：确保脚本的子进程一并回收
    }

    if (cancelled) {
        error = QStringLiteral("脚本执行被取消");
        return false;
    }
    if (timedOut) {
        error = QStringLiteral("脚本执行超时");
        return false;
    }
    return true;
}
#else
bool ScriptSecurityPolicy::hasRestrictedToken() const
{
    return false;
}

bool ScriptSecurityPolicy::runWithRestrictedToken(const QString &program,
                                                  const QStringList &args,
                                                  int timeoutMs,
                                                  const std::function<bool()> &cancelRequested,
                                                  QString &stdOut,
                                                  QString &stdErr,
                                                  QString &error,
                                                  int *exitCode)
{
    Q_UNUSED(program)
    Q_UNUSED(args)
    Q_UNUSED(timeoutMs)
    Q_UNUSED(cancelRequested)
    Q_UNUSED(exitCode)
    stdOut.clear();
    stdErr.clear();
    error = QStringLiteral("受限令牌启动仅支持 Windows");
    return false;
}

QString ScriptSecurityPolicy::restrictedTokenSelfCheck()
{
    return QStringLiteral("restrictedToken=unsupported");
}
#endif

