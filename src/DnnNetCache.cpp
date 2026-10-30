#include "DnnNetCache.h"

#include <QHash>
#include <QMutexLocker>
#include <QFileInfo>
#include <QDir>

namespace {

QMutex &mapMutex()
{
    static QMutex m;
    return m;
}

QHash<QString, std::shared_ptr<DnnNetEntry>> &table()
{
    static QHash<QString, std::shared_ptr<DnnNetEntry>> t;
    return t;
}

DnnNetCache::InjectionHook &injectionHook()
{
    static DnnNetCache::InjectionHook h;
    return h;
}

DnnNetCache::ReadyHook &readyHook()
{
    static DnnNetCache::ReadyHook h;
    return h;
}

/// 键：真实落点 + mtime（同路径换文件 → 重新加载）。联接/别名指向同一文件时共用一个条目。
QString cacheKey(const QString &absPath, qint64 mtime)
{
    return QDir::cleanPath(absPath) + QLatin1Char('|') + QString::number(mtime);
}

}   // namespace

void DnnNetCache::setInjectionHook(InjectionHook hook)
{
    QMutexLocker locker(&mapMutex());
    injectionHook() = std::move(hook);
}

void DnnNetCache::setReadyHook(ReadyHook hook)
{
    QMutexLocker locker(&mapMutex());
    readyHook() = std::move(hook);
}

std::shared_ptr<DnnNetEntry> DnnNetCache::acquire(const QString &absPath, bool useOpenCL,
                                                  QString *error)
{
    if (error)
        error->clear();
    const QString path = absPath.trimmed();
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("模型路径为空");
        return {};
    }

    QMutexLocker locker(&mapMutex());
    InjectionHook inj = injectionHook();

    const QFileInfo fi(path);
    const qint64 mtime = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0;
    const QString key = cacheKey(path, mtime);

    QHash<QString, std::shared_ptr<DnnNetEntry>>::iterator it = table().find(key);
    if (it != table().end())
        return it.value();

    // 加载（持 map 锁：加载是冷路径，简单优先；条目内的 mutex 另有作用域）
    std::shared_ptr<DnnNetEntry> entry(new DnnNetEntry());
    entry->absPath = path;
    entry->mtime = mtime;

    if (inj) {
        // 测试注入：跳过磁盘与空值检查（注入方知道自己在做什么）
        entry->net = inj(path);
        entry->backend = useOpenCL ? 1 : 0;
    } else {
        if (!fi.exists()) {
            if (error)
                *error = QStringLiteral("模型文件不存在：%1").arg(path);
            return {};
        }
        try {
            entry->net = cv::dnn::readNetFromONNX(path.toStdString());
        } catch (const std::exception &e) {
            if (error)
                *error = QStringLiteral("ONNX 模型读取异常：%1").arg(QString::fromUtf8(e.what()));
            return {};
        } catch (...) {
            if (error)
                *error = QStringLiteral("ONNX 模型读取异常：%1").arg(path);
            return {};
        }
        if (entry->net.empty()) {
            if (error)
                *error = QStringLiteral("ONNX 模型加载失败：%1").arg(path);
            return {};
        }
        // 后端只在这里设一次（P1-9：原先每帧 setPreferable*，既浪费又与 forward 并发）
        entry->net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        entry->net.setPreferableTarget(useOpenCL ? cv::dnn::DNN_TARGET_OPENCL
                                                 : cv::dnn::DNN_TARGET_CPU);
        entry->backend = useOpenCL ? 1 : 0;
    }

    ReadyHook ready = readyHook();
    if (ready)
        ready(path, entry->net);

    // 同路径换文件：把旧 mtime 的条目摘掉（否则模型残留、内存随换型增长）
    for (QHash<QString, std::shared_ptr<DnnNetEntry>>::iterator it = table().begin();
         it != table().end();) {
        if (it.value() && it.value()->absPath == path && it.key() != key)
            it = table().erase(it);
        else
            ++it;
    }
    table().insert(key, entry);
    return entry;
}

QString DnnNetCache::textLoadFailed()
{
    return QStringLiteral("ONNX 模型加载失败");
}

void DnnNetCache::clear()
{
    QMutexLocker locker(&mapMutex());
    table().clear();
}

int DnnNetCache::size()
{
    QMutexLocker locker(&mapMutex());
    return int(table().size());
}
