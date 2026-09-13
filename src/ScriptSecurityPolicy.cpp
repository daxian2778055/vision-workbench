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
