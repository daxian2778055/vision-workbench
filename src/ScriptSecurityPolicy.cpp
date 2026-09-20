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
#include <QFileInfo>
#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTextCursor>
#include <QFont>
#include <QThread>
#include <QProcess>
#include <QElapsedTimer>
#include <QStringList>
#include <algorithm>
#include <vector>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <aclapi.h>   // SetNamedSecurityInfoW（给沙箱临时目录打 Low 完整性标签）
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
    // QSettings 在进程内会缓存值：刚 save() 过就读，会读到旧值（有测试用例专门盯这一点）。
    // 先 sync() 强制重新读取，保证 save→load 往返语义正确。
    settings.sync();
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
    // 沙箱强度：以字符串持久化，避免将来枚举顺序变化导致误读
    const QString sandboxModeName =
        settings.value(QStringLiteral("sandboxMode"), QStringLiteral("privilegeStripped")).toString();
    m_sandboxMode = (sandboxModeName.compare(QStringLiteral("appContainer"), Qt::CaseInsensitive) == 0)
                        ? SandboxMode::AppContainer
                        : SandboxMode::PrivilegeStripped;
    // 解释器路径（空串 = 回落命令名，见 interpreterPath）
    m_pythonPath = settings.value(QStringLiteral("interpreterPathPython"), m_pythonPath).toString();
    m_luaPath = settings.value(QStringLiteral("interpreterPathLua"), m_luaPath).toString();
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
    settings.setValue(QStringLiteral("sandboxMode"),
                      m_sandboxMode == SandboxMode::AppContainer
                          ? QStringLiteral("appContainer")
                          : QStringLiteral("privilegeStripped"));
    settings.setValue(QStringLiteral("interpreterPathPython"), m_pythonPath);
    settings.setValue(QStringLiteral("interpreterPathLua"), m_luaPath);
    settings.endGroup();
}

QString ScriptSecurityPolicy::interpreterPath(const QString &language) const
{
    const QString configured = (language == QStringLiteral("Python")) ? m_pythonPath : m_luaPath;

    // 1) 显式配置的绝对路径：原样使用（现场多套 Python/venv 的推荐做法）
    if (!configured.isEmpty() && QFileInfo(configured).isAbsolute()) {
        return configured;
    }

    const QString candidate = configured.isEmpty()
        ? ((language == QStringLiteral("Python")) ? QStringLiteral("python") : QStringLiteral("lua"))
        : configured;

    // 2) 按 PATH 解析成**绝对路径**（findExecutable 不搜当前工作目录）。
    //    绝不把裸名交给启动接口：Windows 的 CreateProcess 搜索序会先看
    //    应用目录与当前目录，攻击者在 exe 旁或工作目录放一个同名 python.exe，
    //    就能顶替掉"确认过的可信脚本"实际执行的解释器（提权/横向）。
    const QString byPath = QStandardPaths::findExecutable(candidate);
    if (!byPath.isEmpty()) {
        return byPath;
    }

    // 解析失败：**返回空串**，由调用方 fail-closed 拒绝执行并提示配置全路径。
    // 刻意不退回 exe 同目录——应用目录/当前目录正是 CreateProcess 搜索序的优先位置，
    // 攻击者把同名 python.exe 放在程序旁即可顶替"确认过的可信脚本"实际执行的解释器，
    // 恰是本次要杜绝的顶替面；只接受「显式配置的绝对路径」与「PATH 解析出的绝对路径」两种可信来源。
    qWarning() << "解释器未在 PATH 中找到（且未配置全路径），拒绝执行脚本，请配置解释器全路径";
    return QString();
}

void ScriptSecurityPolicy::setInterpreterPath(const QString &language, const QString &path)
{
    const QString trimmed = path.trimmed();
    if (language == QStringLiteral("Python")) {
        m_pythonPath = trimmed;
    } else {
        m_luaPath = trimmed;
    }
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

namespace {
/// 展示"脚本执行确认"窗口——**必须在 GUI 线程调用**（QWidget 归属约束）。
/// 完整展示脚本（只读、可滚动、不截断），默认按钮为"取消"（回车不会误确认）。
bool showScriptConfirmationOnGuiThread(const QString &language, const QString &script)
{
    // 历史实现只显示前 400 字符并标注"已截断"——恶意 payload 只要落在第 401 字符
    // 之后就完全不可见，确认框形同虚设（这是安全洞，不是排版问题）。
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("脚本执行确认"));
    dlg.resize(720, 480);
    auto *layout = new QVBoxLayout(&dlg);

    auto *head = new QLabel(&dlg);
    head->setWordWrap(true);
    head->setText(QStringLiteral(
        "即将执行 <b>%1</b> 脚本（以当前用户权限运行，存在安全风险）。<br>"
        "仅应运行你完全信任的脚本。请核对下方脚本<b>全部内容</b>（可滚动，共 %2 字符）：")
        .arg(language.toHtmlEscaped())
        .arg(script.size()));
    layout->addWidget(head);

    auto *view = new QPlainTextEdit(&dlg);
    view->setReadOnly(true);
    view->setPlainText(script);          // 全文：不截断
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont monospace(QStringLiteral("Consolas"));
    monospace.setStyleHint(QFont::Monospace);
    monospace.setPointSize(9);
    view->setFont(monospace);
    view->moveCursor(QTextCursor::Start);   // 滚动位置回到开头
    layout->addWidget(view, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, &dlg);
    if (QPushButton *yes = buttons->button(QDialogButtonBox::Yes))
        yes->setText(QStringLiteral("确认执行"));
    if (QPushButton *no = buttons->button(QDialogButtonBox::No)) {
        no->setText(QStringLiteral("取消"));
        no->setDefault(true);   // 默认拒绝：回车不会误确认
    }
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    return dlg.exec() == QDialog::Accepted;
}
}   // namespace

bool ScriptSecurityPolicy::requestConfirmation(const QString &language, const QString &script) const
{
    if (!m_requireConfirmation) {
        return true;
    }

    QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app) {
        return true;   // 无 QApplication（如测试环境）：不弹窗直接放行
    }

    // 本函数会从执行线程调用（ScriptNode::run），而 QWidget 必须在其所属 GUI 线程创建：
    // 非 GUI 线程时阻塞式切回主线程弹窗并取回结果（默认拒绝语义不变）。
    if (QThread::currentThread() != app->thread()) {
        bool accepted = false;
        QMetaObject::invokeMethod(app, [&accepted, &language, &script]() {
            accepted = showScriptConfirmationOnGuiThread(language, script);
        }, Qt::BlockingQueuedConnection);
        return accepted;
    }
    return showScriptConfirmationOnGuiThread(language, script);
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
    // 必须同时申请 TOKEN_ADJUST_DEFAULT：受限令牌句柄的权限受源句柄限制，缺少它
    // SetTokenInformation(TokenIntegrityLevel) 会以 ERROR_ACCESS_DENIED(5) 失败，
    // 低完整性级别便静默失效（探针 tests/restricted_token_probe.cpp 正是靠这一点定位的）。
    if (!OpenProcessToken(GetCurrentProcess(),
                           TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT,
                           &hProc)) {
        qWarning() << "[ScriptSecurityPolicy] OpenProcessToken 失败，沙箱降级为无降权";
        return;
    }

    // 关于「管理员组 deny-only」：本机实测一旦施加，**任何**子进程都以 0xC0000142
    // STATUS_DLL_INIT_FAILED 退出（默认桌面、专用窗口站/桌面、标签降为 Medium 均如此；
    // 与管道/句柄继承、工作目录无关；连 cmd /c echo 也一样）。已用调试器 + 加载器快照
    // 定位到确切失败点：
    //   LdrpInitializeNode    - ERROR: Init routine ... for DLL "C:\WINDOWS\System32\KERNELBASE.dll"
    //                                 failed during DLL_PROCESS_ATTACH
    //   LdrpInitializeProcess - ERROR: Loading Windows subsystem DLL "KERNEL32.DLL" failed 0xc0000142
    //   _LdrpInitialize       - ERROR: Process initialization failed with status 0xc0000142
    // 即 **Windows 自身的 KERNELBASE.dll 在进程附加阶段就失败**，与本项目代码无关；
    // 在这台机器/该 Windows 版本上 deny-only 与受限 SID 列表不具备可用性。
    // 复现：build/bin/Release/restricted_token_probe.exe -dbg（见 tests/restricted_token_probe.cpp）。
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

    // 低完整性级别（Low IL）：实测可阻止子进程写入 Medium 完整性的用户目录（脚本无法改动
    // 用户文件、无法持久化），同时不影响读取与执行——见 tests/restricted_token_probe.cpp
    // 打印的组合矩阵（A 组 vs C 组的写入测试：fileWritten=YES vs no）。
    // 注意：Low IL 下脚本无法向 Medium 目录写临时文件，故 runWithRestrictedToken 会为每次
    // 执行准备一个打了 Low 标签的私有临时目录，并把子进程的 TEMP/TMP 指过去。
    {
        SID_IDENTIFIER_AUTHORITY authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
        PSID lowSid = nullptr;
        if (AllocateAndInitializeSid(&authority, 1, SECURITY_MANDATORY_LOW_RID,
                                     0, 0, 0, 0, 0, 0, 0, &lowSid)) {
            TOKEN_MANDATORY_LABEL label{};
            label.Label.Sid = lowSid;
            label.Label.Attributes = SE_GROUP_INTEGRITY;
            if (!SetTokenInformation(hRestricted, TokenIntegrityLevel, &label,
                                     sizeof(label) + GetLengthSid(lowSid))) {
                qWarning() << "[ScriptSecurityPolicy] 设置低完整性级别失败，降权仅剩去特权, err="
                           << GetLastError();
            }
            FreeSid(lowSid);
        }
    }
    //
    // 当前降权边界（务必如实对外描述，不得夸大）：
    //   已施加：去除全部特权（仅剩 SeChangeNotifyPrivilege）
    //   已施加：低完整性级别（Low IL）+ 每次执行独立的 Low 私有临时目录
    //   已施加：独立 Job Object（失败即拒绝执行）、仅继承 stdout/stderr 两个句柄
    //   未施加：管理员组 deny-only、受限 SID 列表——探针实测二者均使**任何**子进程以
    //           0xC0000142 STATUS_DLL_INIT_FAILED 退出（连 cmd /c echo、有无管道都一样，
    //           即失败在进程初始化阶段，与 stdio/句柄无关）
    //   未施加：专用窗口站/桌面（lpDesktop 传 nullptr，子进程继承当前桌面）、网络隔离
    //   结论：可描述为「已去特权 + 低完整性 + 进程树/句柄约束」，仍不可描述为完整隔离。

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

/// 为一次脚本执行创建私有临时目录，并标记为 Low 完整性（否则 Low IL 子进程无法写入）。
/// 标签带继承标志，脚本在其中创建的文件/子目录同样为 Low。
QString createSandboxTempDir()
{
    const QString runId = QStringLiteral("%1-%2")
                              .arg(QCoreApplication::applicationPid())
                              .arg(QDateTime::currentMSecsSinceEpoch());
    const QString path = QDir::tempPath() + QStringLiteral("/vfp-script-sandbox/run-%1").arg(runId);
    if (!QDir().mkpath(path)) {
        return QString();
    }

    SID_IDENTIFIER_AUTHORITY authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    PSID lowSid = nullptr;
    if (!AllocateAndInitializeSid(&authority, 1, SECURITY_MANDATORY_LOW_RID,
                                 0, 0, 0, 0, 0, 0, 0, &lowSid)) {
        return path;   // 目录可用，但未降标签：脚本可写，隔离性弱
    }
    const DWORD sidLength = GetLengthSid(lowSid);
    const DWORD aceLength = sizeof(SYSTEM_MANDATORY_LABEL_ACE) + sidLength - sizeof(DWORD);
    std::vector<BYTE> aclStorage(sizeof(ACL) + aceLength);
    PACL acl = reinterpret_cast<PACL>(aclStorage.data());
    bool labelled = InitializeAcl(acl, static_cast<DWORD>(aclStorage.size()), ACL_REVISION)
                    && AddMandatoryAce(acl, ACL_REVISION,
                                       OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
                                       SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, lowSid);

    if (labelled) {
        std::wstring wide = path.toStdWString();
        if (SetNamedSecurityInfoW(wide.data(), SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION,
                                  nullptr, nullptr, nullptr, acl) != ERROR_SUCCESS) {
            qWarning() << "[ScriptSecurityPolicy] 沙箱临时目录设置 Low 标签失败, err="
                       << GetLastError();
        }
    }
    FreeSid(lowSid);

    if (!labelled) {
        // 未成功降标签：返回系统临时目录会失去"可写区"隔离，故仍返回该目录（至少是独立目录）
        qWarning() << "[ScriptSecurityPolicy] 构造 Low 完整性标签失败，沙箱目录未降标签";
    }
    return path;
}

/// 离开作用域时删除沙箱临时目录（覆盖 runWithRestrictedToken 的所有返回路径）
struct SandboxTempDirGuard {
    QString path;
    ~SandboxTempDirGuard()
    {
        if (!path.isEmpty()) {
            QDir(path).removeRecursively();
        }
    }
};

#if defined(Q_OS_WIN)
// AppContainer 相关 API 在 userenv.lib（CreateAppContainerProfile / DeriveAppContainerSidFromAppContainerName /
// GetAppContainerFolderPath）；SID 字符串转换在 advapi32（默认库）。放在这里是因为只被下方 Windows 分支使用。
#include <QFileInfo>
#include <userenv.h>
#include <sddl.h>
#pragma comment(lib, "userenv.lib")

/// 产品使用的 AppContainer 配置文件（一次创建，长期复用）
constexpr const wchar_t *kSandboxProfileName = L"VisionFlowPlatform.Sandbox";
constexpr const wchar_t *kSandboxProfileDisplayName = L"VisionFlowPlatform Sandbox";

/// 进程内缓存的容器 SID；返回空表示不可用（调用方必须 fail-closed，不得静默降级）。
PSID appContainerSidRaw()
{
    static QMutex mutex;
    static PSID cached = nullptr;
    static bool attempted = false;
    QMutexLocker locker(&mutex);
    if (!attempted) {
        attempted = true;
        QString error;
        if (!ScriptSecurityPolicy::ensureAppContainerProfile(&error)) {
            qWarning() << "[ScriptSecurityPolicy] AppContainer 配置文件不可用:" << error;
            return nullptr;
        }
        const QString sidString = ScriptSecurityPolicy::appContainerSid();
        PSID parsed = nullptr;
        if (!sidString.isEmpty()
            && ConvertStringSidToSidW(reinterpret_cast<const wchar_t *>(sidString.utf16()),
                                      &parsed)) {
            cached = parsed;
        } else {
            qWarning() << "[ScriptSecurityPolicy] AppContainer SID 解析失败:" << sidString;
        }
    }
    return cached;
}

/// 容器专属可写临时目录（<容器根>\Temp，幂等创建）。容器内解释器的 TEMP/TMP 指向这里：
/// Low 标签目录容器内未必可写、用户目录更会被拒绝，否则解释器会报"找不到可用临时目录"。
QString appContainerTempDir()
{
    PSID sid = appContainerSidRaw();
    if (!sid) {
        return QString();
    }
    LPWSTR sidString = nullptr;
    if (!ConvertSidToStringSidW(sid, &sidString) || !sidString) {
        return QString();
    }
    PWSTR folder = nullptr;
    QString result;
    if (SUCCEEDED(GetAppContainerFolderPath(sidString, &folder)) && folder) {
        result = QDir(QString::fromWCharArray(folder)).filePath(QStringLiteral("Temp"));
        QDir().mkpath(result);
        CoTaskMemFree(folder);
    }
    LocalFree(sidString);
    return result;
}
#endif

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

    // AppContainer 模式不需要受限令牌：它用「普通用户令牌 + 容器 SID」，不触碰任何
    // "减弱型"令牌标志，因此不会触发 deny-only 那条 KERNELBASE 附加失败的死路
    // （实测见 restricted_token_probe.exe -ac / -dbg）。其余情况沿用受限令牌。
    PSID containerSid = nullptr;
    HANDLE restrictedToken = nullptr;
    if (m_sandboxMode == SandboxMode::AppContainer) {
        containerSid = appContainerSidRaw();
        if (!containerSid) {
            // fail-closed：拿不到容器 SID 就拒绝执行，绝不静默降级为"无隔离"
            error = QStringLiteral("AppContainer 不可用：配置文件未建立或派生失败"
                                   "（首次创建需要管理员权限）");
            return false;
        }
    } else {
        ensureRestrictedToken();
        {
            QMutexLocker locker(&m_tokenMutex);   // 与创建路径互斥（P1）
            restrictedToken = reinterpret_cast<HANDLE>(m_restrictedToken);
        }
        if (!restrictedToken) {
            error = QStringLiteral("受限令牌不可用");
            return false;
        }
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

    // Low IL 子进程无法写入 Medium 完整性的系统临时目录（Python 的 tempfile 会因此失败），
    // 故每次执行都给一个打了 Low 标签的私有临时目录，并把 TEMP/TMP 指过去：
    // 脚本仍能正常读写临时文件，但写入被约束在这个目录内。
    // AppContainer 模式下必须把 TEMP/TMP 指向**容器自己的**可写目录：Low 标签目录在容器内
    // 未必可写、用户目录更会被拒绝，否则解释器会报"找不到可用临时目录"。
    const QString sandboxTemp = containerSid ? appContainerTempDir() : createSandboxTempDir();
    SandboxTempDirGuard sandboxGuard{ containerSid ? QString() : sandboxTemp };   // 容器目录是持久的，不删
    QProcessEnvironment childEnv = buildEnvironment();
    if (!sandboxTemp.isEmpty()) {
        childEnv.insert(QStringLiteral("TEMP"), sandboxTemp);
        childEnv.insert(QStringLiteral("TMP"), sandboxTemp);
    }
    const QByteArray envBlock = buildEnvironmentBlock(childEnv);

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
    // AppContainer 模式下再加一条 SECURITY_CAPABILITIES（属性列表容量随之 +1）。
    bool inheritFiltered = false;
#if defined(PROC_THREAD_ATTRIBUTE_HANDLE_LIST)
    HANDLE inheritList[2] = { outWrite, errWrite };
    SECURITY_CAPABILITIES caps{};
    if (containerSid) {
        caps.AppContainerSid = containerSid;   // 不给任何能力：无网络、无企业认证
        caps.CapabilityCount = 0;
        caps.Capabilities = nullptr;
    }
    const DWORD attrCount = containerSid ? 2 : 1;
    SIZE_T attrBytes = 0;
    InitializeProcThreadAttributeList(nullptr, attrCount, 0, &attrBytes);
    std::vector<BYTE> attrBuf(attrBytes);
    LPPROC_THREAD_ATTRIBUTE_LIST attrList =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    bool attrsOk = attrBytes > 0
                   && InitializeProcThreadAttributeList(attrList, attrCount, 0, &attrBytes)
                   && UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                inheritList, sizeof(inheritList), nullptr, nullptr);
    if (attrsOk && containerSid) {
        attrsOk = UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                           &caps, sizeof(caps), nullptr, nullptr);
    }
    if (attrsOk) {
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

    auto spawn = [&](DWORD extraFlags) {
        if (containerSid) {
            // AppContainer：普通用户令牌 + 容器 SID（CreateProcess 的 SECURITY_CAPABILITIES 路径）
            return CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                  baseFlags | extraFlags, envPtr, nullptr, &siex.StartupInfo, &pi);
        }
        return CreateProcessAsUserW(restrictedToken,
                                    nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                    baseFlags | extraFlags,
                                    envPtr, nullptr, &siex.StartupInfo, &pi);
    };

    // 优先脱离父作业（便于关入自己的 Job Object）；若父作业不允许脱离则退回不带该标志
    BOOL created = spawn(CREATE_BREAKAWAY_FROM_JOB);
    if (!created) {
        created = spawn(0);
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

bool ScriptSecurityPolicy::ensureAppContainerProfile(QString *error)
{
    PSID sid = nullptr;
    HRESULT hr = CreateAppContainerProfile(kSandboxProfileName, kSandboxProfileDisplayName,
                                          kSandboxProfileDisplayName, nullptr, 0, &sid);
    if (FAILED(hr) && hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        hr = DeriveAppContainerSidFromAppContainerName(kSandboxProfileName, &sid);
    }
    if (SUCCEEDED(hr) && sid) {
        FreeSid(sid);   // 这里只确认"可用"；SID 由 appContainerSid()/缓存另行获取
        return true;
    }
    if (error) {
        *error = QStringLiteral("AppContainer 配置文件不可用 hr=0x%1（首次创建需要管理员权限）")
                     .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
    }
    return false;
}

QString ScriptSecurityPolicy::appContainerSid()
{
    PSID sid = nullptr;
    if (FAILED(DeriveAppContainerSidFromAppContainerName(kSandboxProfileName, &sid)) || !sid) {
        return QString();
    }
    LPWSTR sidString = nullptr;
    QString result;
    if (ConvertSidToStringSidW(sid, &sidString) && sidString) {
        result = QString::fromWCharArray(sidString);
        LocalFree(sidString);
    }
    FreeSid(sid);
    return result;
}

bool ScriptSecurityPolicy::grantInterpreterAccess(const QString &program, QString *error)
{
    const QString sid = appContainerSid();
    if (sid.isEmpty()) {
        if (error) {
            *error = QStringLiteral("AppContainer 配置文件不存在（请先确保容器可用）");
        }
        return false;
    }
    // 定位解释器目录：program 既可能是完整路径，也可能只是 PATH 中的命令名
    QString dir = QFileInfo(program).absolutePath();
    if (QFileInfo(program).isRelative() || dir.isEmpty() || dir == QLatin1String(".")) {
        const QString found = QStandardPaths::findExecutable(program);
        dir = found.isEmpty() ? QString() : QFileInfo(found).absolutePath();
    }
    if (dir.isEmpty()) {
        if (error) {
            *error = QStringLiteral("找不到解释器所在目录: %1").arg(program);
        }
        return false;
    }
    // 非破坏性授权：读出现有 DACL → 只追加/替换「容器 SID: 读取+执行（含子目录继承）」→ 写回。
    // 【不要改用 icacls /grant】它会规范化并整体重写 DACL：本机实测重写后继承标记全部消失，
    // 并连带影响其它沙箱模式下的解释器访问，这类副作用在工业现场极难排查。
    // SetEntriesInAclW(SET_ACCESS) 只动该 SID 的条目，其余原样保留。
    //
    // 【已知相互作用，勿盲目扩大授权范围】restricted_token_probe.exe -il 的受控矩阵实证
    // （同一进程内翻转 ACL）：
    //   · 目录 DACL 上**只要存在该 AppContainer SID 的条目**，Low IL（去特权 + 低完整性）
    //     启动解释器就会以 0xC0000135 STATUS_DLL_NOT_FOUND 失败——**与权限位无关**（连只给
    //     FILE_TRAVERSE 也照样失败）；
    //   · 同样条件下换成任意**普通 SID** 的条目（同样 5→7 条 ACE）**毫无影响**；
    //   · 条目加在**文件**上（如 python.exe）则**无害**（仅解释器打印一句
    //     "Failed to find real location"，不影响运行）；
    //   · 撤销目录条目后立即恢复。
    // 【结论：两种强度在同一台机器上互斥，已双向实证】
    //   · 目录级授权 → 容器可用（python 正常），但 Low IL 失效（0xC0000135）；
    //   · 仅文件级授权（-ac-file，实测递归授权 51979 个文件、目录零条目）→ Low IL 正常，
    //     但容器内解释器起不来：getpath 需要 stat 'Lib/' 等目录，没有目录条目就遍历不了，
    //     表现为 sys.path[0]=(not set) 的路径配置报错。
    //   · 因此不能通过"缩小授权粒度"两全；现场必须在两者间二选一：
    //     需要容器隔离（独立文件视图 + 无网络）就用 AppContainer 并接受 Low IL 不可用；
    //     需要与现有去特权方案共存就别授权容器，直接用默认强度。
    //     撤销授权：icacls "<解释器目录>" /reset（或 /reset /t 连子目录一起）。
    PSID sidRaw = nullptr;
    if (!ConvertStringSidToSidW(reinterpret_cast<const wchar_t *>(sid.utf16()), &sidRaw) || !sidRaw) {
        if (error) {
            *error = QStringLiteral("无法解析容器 SID: %1").arg(sid);
        }
        return false;
    }

    const std::wstring nativeDir = QDir::toNativeSeparators(dir).toStdWString();
    PACL oldDacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    const DWORD readResult = GetNamedSecurityInfoW(
        const_cast<LPWSTR>(nativeDir.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &oldDacl, nullptr, &sd);
    if (readResult != ERROR_SUCCESS) {
        LocalFree(sidRaw);
        if (error) {
            *error = QStringLiteral("读取解释器目录权限失败（错误 %1）：%2")
                         .arg(readResult)
                         .arg(dir);
        }
        return false;
    }

    EXPLICIT_ACCESSW entry{};
    entry.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    entry.grfAccessMode = SET_ACCESS;                       // 只替换该 SID 的既有条目（幂等）
    entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sidRaw);

    PACL newDacl = nullptr;
    const DWORD mergeResult = SetEntriesInAclW(1, &entry, oldDacl, &newDacl);
    bool granted = false;
    if (mergeResult == ERROR_SUCCESS && newDacl) {
        granted = SetNamedSecurityInfoW(const_cast<LPWSTR>(nativeDir.c_str()), SE_FILE_OBJECT,
                                        DACL_SECURITY_INFORMATION, nullptr, nullptr, newDacl,
                                        nullptr) == ERROR_SUCCESS;
    }
    if (newDacl) {
        LocalFree(newDacl);
    }
    if (sd) {
        LocalFree(sd);
    }
    LocalFree(sidRaw);

    if (!granted) {
        if (error) {
            *error = QStringLiteral("为解释器目录授权失败（需要管理员权限）：%1").arg(dir);
        }
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

bool ScriptSecurityPolicy::ensureAppContainerProfile(QString *error)
{
    if (error) {
        *error = QStringLiteral("AppContainer 仅支持 Windows");
    }
    return false;
}

QString ScriptSecurityPolicy::appContainerSid()
{
    return QString();
}

bool ScriptSecurityPolicy::grantInterpreterAccess(const QString &program, QString *error)
{
    Q_UNUSED(program)
    if (error) {
        *error = QStringLiteral("AppContainer 仅支持 Windows");
    }
    return false;
}

QString ScriptSecurityPolicy::restrictedTokenSelfCheck()
{
    return QStringLiteral("restrictedToken=unsupported");
}
#endif

