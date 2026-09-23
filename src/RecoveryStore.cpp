#include "RecoveryStore.h"
#include "AppLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
const QString kRecoveryFile = QStringLiteral("recovery.vfp");
const QString kRestoreFile = QStringLiteral("restore-tmp.vfp");
const QString kMetaKey = QStringLiteral("recoveryMeta");
}   // namespace

RecoveryStore::RecoveryStore()
    : m_dir(defaultDirPath())
{
}

RecoveryStore::RecoveryStore(const QString &dirPath)
    : m_dir(dirPath)
{
}

QString RecoveryStore::recoveryFileName()
{
    return kRecoveryFile;
}

QString RecoveryStore::restoreFileName()
{
    return kRestoreFile;
}

QString RecoveryStore::metaKey()
{
    return kMetaKey;
}

QString RecoveryStore::runtimeDataRoot()
{
    // 与应用数据库（exe/data/visionflow.db）同一惯例：运行时数据随安装目录走。
    const QString preferred = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    if (QDir().mkpath(preferred)) {
        // 只读安装目录（Program Files 等）必须能退让：用写探针实测，不靠猜路径是否存在。
        QFile probe(preferred + QStringLiteral("/.write-probe"));
        if (probe.open(QIODevice::WriteOnly)) {
            probe.close();
            probe.remove();
            return preferred;
        }
    }
    QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (fallback.isEmpty())
        fallback = QDir::tempPath() + QStringLiteral("/VisionFlowPlatform");
    return fallback;
}

QString RecoveryStore::defaultDirPath()
{
    const QString dir = runtimeDataRoot() + QStringLiteral("/recovery");
    QDir().mkpath(dir);   // 调用方（saveRecovery / info）假定目录已存在，这里保证
    return dir;
}

QByteArray RecoveryStore::compact(const QJsonObject &json)
{
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

bool RecoveryStore::isEmptyProject(const QJsonObject &json)
{
    const QJsonArray scenes = json.value(QStringLiteral("scenes")).toArray();
    for (const QJsonValue &v : scenes) {
        if (!v.toObject().value(QStringLiteral("nodes")).toArray().isEmpty())
            return false;
    }
    return true;   // 无流程，或有流程但没有任何节点
}

bool RecoveryStore::saveRecovery(const QJsonObject &projectJson, const QString &originalPath,
                                 const QDateTime &now)
{
    if (!QDir().mkpath(m_dir)) {
        VFP_DEBUG << "自动保存失败：无法创建恢复目录" << m_dir;
        return false;
    }

    QJsonObject root = projectJson;
    QJsonObject meta;
    meta[QStringLiteral("originalPath")] = originalPath;
    meta[QStringLiteral("savedAt")] = now.toString(Qt::ISODate);
    meta[QStringLiteral("appVersion")] = QCoreApplication::applicationVersion();
    root[kMetaKey] = meta;

    const QByteArray payload = compact(root);
    QSaveFile file(QDir(m_dir).filePath(kRecoveryFile));
    if (!file.open(QIODevice::WriteOnly)) {
        VFP_DEBUG << "自动保存失败：无法写入" << file.fileName() << file.errorString();
        return false;
    }
    const qint64 written = file.write(payload);
    if (written != payload.size()) {
        VFP_DEBUG << "自动保存不完整:" << written << "/" << payload.size()
                  << " error:" << file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        VFP_DEBUG << "自动保存失败（commit）:" << file.fileName() << file.errorString();
        return false;
    }

    // 记住本次写入内容（不含元信息），供"同内容不重复写"判断
    m_lastRecovery = compact(projectJson);
    return true;
}

RecoveryStore::Info RecoveryStore::info() const
{
    Info out;
    const QString path = QDir(m_dir).filePath(kRecoveryFile);
    QFile file(path);
    if (!file.exists())
        return out;
    out.exists = true;
    out.size = file.size();
    if (!file.open(QIODevice::ReadOnly)) {
        VFP_DEBUG << "恢复文件存在但无法读取" << path << file.errorString();
        return out;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return out;   // exists = true, readable = false：损坏，调用方应告知并清除
    out.readable = true;

    const QJsonObject root = doc.object();
    if (!root.contains(kMetaKey))
        return out;
    const QJsonObject meta = root.value(kMetaKey).toObject();
    out.metaPresent = true;
    out.originalPath = meta.value(QStringLiteral("originalPath")).toString();
    out.savedAt = QDateTime::fromString(meta.value(QStringLiteral("savedAt")).toString(),
                                        Qt::ISODate);
    return out;
}

bool RecoveryStore::loadRecovery(QJsonObject *out) const
{
    const Info inf = info();
    if (!inf.readable)
        return false;

    QFile file(QDir(m_dir).filePath(kRecoveryFile));
    if (!file.open(QIODevice::ReadOnly)) {
        VFP_DEBUG << "读取恢复文件失败" << file.fileName() << file.errorString();
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    if (out)
        *out = doc.object();
    return true;
}

bool RecoveryStore::exportForLoad(QString *outPath) const
{
    QJsonObject json;
    if (!loadRecovery(&json)) {
        VFP_DEBUG << "导出恢复内容失败：恢复文件不可读";
        return false;
    }
    if (!QDir().mkpath(m_dir)) {
        VFP_DEBUG << "导出恢复内容失败：无法创建目录" << m_dir;
        return false;
    }

    const QString path = QDir(m_dir).filePath(kRestoreFile);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        VFP_DEBUG << "导出恢复内容失败：无法写入" << path << file.errorString();
        return false;
    }
    const QByteArray payload = compact(json);
    const qint64 written = file.write(payload);
    if (written != payload.size()) {
        file.cancelWriting();
        VFP_DEBUG << "导出恢复内容不完整:" << written << "/" << payload.size();
        return false;
    }
    if (!file.commit()) {
        VFP_DEBUG << "导出恢复内容失败（commit）:" << path << file.errorString();
        return false;
    }
    if (outPath)
        *outPath = path;
    return true;
}

bool RecoveryStore::clearRecovery()
{
    bool ok = true;
    const QString recovery = QDir(m_dir).filePath(kRecoveryFile);
    if (QFile::exists(recovery) && !QFile::remove(recovery)) {
        VFP_DEBUG << "清除恢复文件失败" << recovery;
        ok = false;
    }
    // 临时导出文件：清不掉不影响判定（下次导出会覆盖），故不计入返回值
    const QString restore = QDir(m_dir).filePath(kRestoreFile);
    if (QFile::exists(restore))
        QFile::remove(restore);

    m_lastRecovery.clear();
    return ok;
}

void RecoveryStore::markSaved(const QJsonObject &projectJson)
{
    m_baseline = compact(projectJson);
}

bool RecoveryStore::hasUnsavedChanges(const QJsonObject &projectJson) const
{
    if (m_baseline.isEmpty())
        return !isEmptyProject(projectJson);   // 从未保存过：空白方案不打扰，有内容才算"未保存"
    return compact(projectJson) != m_baseline;
}

bool RecoveryStore::recoveryUpToDate(const QJsonObject &projectJson) const
{
    return !m_lastRecovery.isEmpty() && m_lastRecovery == compact(projectJson);
}
