#include "LargeFileImageStore.h"
#include "VmSharedText.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QMutexLocker>
#include <cstring>
#include <opencv2/imgcodecs.hpp>

namespace {
constexpr char kMagic[8] = {'V', 'F', 'P', 'I', 'M', 'G', '0', '1'};
constexpr int kMaxEntries = 100000;   ///< 防坏文件：条目数上界
/// 单批内存预算：batchSize 可能被设成很大（现场常见 1000+），5MP 彩色图 PNG 单张可达数 MB，
/// 只按张数攒批会把内存吃光。超过预算就提前落盘（批次文件略小，但不会 OOM）。
constexpr qint64 kMaxPendingBytes = 256LL * 1024 * 1024;
}   // namespace

LargeFileImageStore::LargeFileImageStore()
{
    QSettings s;
    m_settings.enabled = s.value(QStringLiteral("acq/largeFileEnabled"), false).toBool();
    m_settings.directory = s.value(QStringLiteral("acq/largeFileDirectory"), QString()).toString();
    m_settings.batchSize =
        qBound(1, s.value(QStringLiteral("acq/largeFileBatchSize"), 100).toInt(), 10000);
}

LargeFileImageStore *LargeFileImageStore::instance()
{
    static LargeFileImageStore s_store;
    return &s_store;
}

LargeFileImageStore::Settings LargeFileImageStore::settings() const
{
    QMutexLocker locker(&m_mutex);
    return m_settings;
}

bool LargeFileImageStore::setSettings(const Settings &settings, QString *error)
{
    QMutexLocker locker(&m_mutex);
    Settings next = settings;
    next.batchSize = qBound(1, next.batchSize, 10000);
    next.directory = next.directory.trimmed();
    if (next.enabled && next.directory.isEmpty()) {
        if (error) *error = QStringLiteral("启用大文件存图前需设置保存路径");
        return false;
    }

    // 目录/批量变化前先把未满批次落盘：否则旧目录的图会被写进新目录，
    // 或批量缩小后已攒够的图要继续等下一批。
    if (!m_pending.isEmpty()) {
        const bool dirChanged = next.directory != m_settings.directory;
        const bool needFlush = dirChanged || m_pending.size() >= next.batchSize;
        if (needFlush) {
            QString why;
            if (!writePendingBatchLocked(nullptr, &why)) {
                if (error) *error = QStringLiteral("切换采集策略前写入当前批次失败：%1").arg(why);
                return false;
            }
        }
    }

    m_settings = next;
    persistSettings();
    return true;
}

bool LargeFileImageStore::enabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_settings.enabled;
}

int LargeFileImageStore::pendingCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_pending.size();
}

void LargeFileImageStore::persistSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("acq/largeFileEnabled"), m_settings.enabled);
    s.setValue(QStringLiteral("acq/largeFileDirectory"), m_settings.directory);
    s.setValue(QStringLiteral("acq/largeFileBatchSize"), m_settings.batchSize);
}

bool LargeFileImageStore::append(const cv::Mat &image, const QString &sourceName,
                                 QString *batchFile, QString *error)
{
    if (batchFile) batchFile->clear();
    if (image.empty()) {
        if (error) *error = QStringLiteral("图像为空");
        return false;
    }
    if (image.depth() != CV_8U) {
        if (error) *error = QStringLiteral("大文件存图只支持 8 位图像（当前 depth=%1）")
                                .arg(image.depth());
        return false;
    }

    std::vector<uchar> encoded;
    std::vector<int> encodeParams{cv::IMWRITE_PNG_COMPRESSION, 3};
    if (!cv::imencode(".png", image, encoded, encodeParams)) {
        if (error) *error = QStringLiteral("PNG 编码失败");
        return false;
    }

    QMutexLocker locker(&m_mutex);
    if (!m_settings.enabled) {
        if (error) *error = QStringLiteral("大文件存图未启用");
        return false;
    }
    LargeImageEntry entry;
    entry.name = sourceName.isEmpty() ? QStringLiteral("image") : sourceName;
    entry.pngData = QByteArray(reinterpret_cast<const char *>(encoded.data()),
                               static_cast<int>(encoded.size()));
    m_pending.append(entry);
    m_pendingBytes += entry.pngData.size();

    if (m_pending.size() >= m_settings.batchSize || m_pendingBytes >= kMaxPendingBytes) {
        QString path, why;
        if (!writePendingBatchLocked(&path, &why)) {
            if (error) *error = why;
            return false;
        }
        if (batchFile) *batchFile = path;
    }
    return true;
}

int LargeFileImageStore::flush()
{
    QMutexLocker locker(&m_mutex);
    if (m_pending.isEmpty())
        return 0;
    QString path, why;
    if (!writePendingBatchLocked(&path, &why))
        return 0;
    return 1;
}

bool LargeFileImageStore::writePendingBatchLocked(QString *outPath, QString *error)
{
    if (m_pending.isEmpty())
        return true;
    const QString dir = m_settings.directory.trimmed();
    if (dir.isEmpty()) {
        if (error) *error = QStringLiteral("未设置大文件存图路径");
        return false;
    }
    if (!QDir().mkpath(dir)) {
        if (error) *error = VmSharedText::mkdirFailed(dir);
        return false;
    }

    const QString fileName = QStringLiteral("vmbatch_%1_%2.dat")
                                 .arg(QDateTime::currentDateTime().toString(
                                     QStringLiteral("yyyyMMdd_HHmmss")))
                                 .arg(m_batchSeq, 4, 10, QLatin1Char('0'));
    const QString finalPath = QDir(dir).filePath(fileName);
    const QString tmpPath = finalPath + QStringLiteral(".tmp");

    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("无法写入临时文件：%1").arg(tmpPath);
        return false;
    }
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData(kMagic, 8);
    out << quint32(m_pending.size());
    for (const LargeImageEntry &e : m_pending) {
        out << e.name.toUtf8();
        out << e.pngData;
    }
    file.flush();
    const bool writeOk = file.error() == QFileDevice::NoError;
    file.close();
    if (!writeOk) {
        QFile::remove(tmpPath);
        if (error) *error = QStringLiteral("写入批次文件失败（磁盘空间不足？）：%1").arg(tmpPath);
        return false;
    }
    // 临时文件 + 原子改名：中途失败不会留下半个 .dat 让人误以为数据完整
    if (!QFile::rename(tmpPath, finalPath)) {
        QFile::remove(tmpPath);
        if (error) *error = QStringLiteral("批次文件改名失败：%1").arg(finalPath);
        return false;
    }

    m_pending.clear();
    m_pendingBytes = 0;
    ++m_batchSeq;
    if (outPath) *outPath = finalPath;
    return true;
}

bool LargeFileImageStore::readBatch(const QString &path, QVector<LargeImageEntry> *entries,
                                    QString *error)
{
    if (entries) entries->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法打开文件：%1").arg(path);
        return false;
    }
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);
    char magic[8] = {0};
    if (in.readRawData(magic, 8) != 8 || std::memcmp(magic, kMagic, 8) != 0) {
        if (error) *error = QStringLiteral("不是有效的大文件存图数据（文件头不匹配）");
        return false;
    }
    quint32 count = 0;
    in >> count;
    if (count > kMaxEntries) {
        if (error) *error = QStringLiteral("条目数异常（%1）").arg(count);
        return false;
    }
    QVector<LargeImageEntry> result;
    result.reserve(static_cast<int>(count));
    for (quint32 i = 0; i < count; ++i) {
        QByteArray name;
        QByteArray data;
        in >> name >> data;
        if (in.status() != QDataStream::Ok || data.isEmpty()) {
            if (error) *error = QStringLiteral("第 %1 条数据损坏").arg(i + 1);
            return false;
        }
        LargeImageEntry entry;
        entry.name = QString::fromUtf8(name);
        entry.pngData = data;
        result.append(entry);
    }
    if (entries) *entries = result;
    return true;
}

int LargeFileImageStore::extractBatch(const QString &path, const QString &outDir, QString *error)
{
    QVector<LargeImageEntry> entries;
    if (!readBatch(path, &entries, error))
        return 0;
    if (!QDir().mkpath(outDir)) {
        if (error) *error = QStringLiteral("无法创建输出目录：%1").arg(outDir);
        return 0;
    }
    int written = 0;
    for (int i = 0; i < entries.size(); ++i) {
        // 只取文件名部分，防止条目的名称里带路径穿越
        QString base = QFileInfo(entries[i].name).fileName();
        if (base.isEmpty())
            base = QStringLiteral("image");
        const QString outPath = QDir(outDir).filePath(
            QStringLiteral("%1_%2.png").arg(i + 1, 4, 10, QLatin1Char('0')).arg(base));
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || out.write(entries[i].pngData) != entries[i].pngData.size()) {
            if (error) *error = QStringLiteral("写出失败：%1").arg(outPath);
            return written;
        }
        out.close();
        ++written;
    }
    return written;
}
