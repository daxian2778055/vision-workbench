#include <QStringList>
#include "ModelCache.h"

#include <QHash>
#include <QMutexLocker>
#include <QFileInfo>
#include <QDir>
#include <climits>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/features2d.hpp>

namespace {

QMutex &mapMutex()
{
    static QMutex m;
    return m;
}

struct ImgEntry {
    cv::Mat mat;
    qint64 mtime = 0;
};

struct KpEntry {
    std::vector<cv::KeyPoint> kp;
    cv::Mat desc;
};

/// 插入顺序（LRU 用）：QHash 本身不保序，靠它才知道谁最旧
QStringList &imgOrder()
{
    static QStringList o;
    return o;
}

QStringList &kpOrder()
{
    static QStringList o;
    return o;
}

QHash<QString, ImgEntry> &imgTable()
{
    static QHash<QString, ImgEntry> t;
    return t;
}

QHash<QString, KpEntry> &kpTable()
{
    static QHash<QString, KpEntry> t;
    return t;
}

qint64 mtimeOf(const QString &path)
{
    const QFileInfo fi(path);
    return fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0;
}

}   // namespace

cv::Mat ModelCache::imageFor(const QString &absPath, int flags)
{
    const QString path = absPath.trimmed();
    if (path.isEmpty())
        return {};
    const qint64 mt = mtimeOf(path);
    const QString key = QDir::cleanPath(path) + QLatin1Char('|') + QString::number(flags)
                        + QLatin1Char('|') + QString::number(mt);

    QMutexLocker<QMutex> locker(&mapMutex());
    QHash<QString, ImgEntry>::const_iterator it = imgTable().constFind(key);
    if (it != imgTable().constEnd()) {
        imgOrder().removeAll(key);
        imgOrder().append(key);   // 命中即刷新（真 LRU）
        return it.value().mat;
    }

    ImgEntry e;
    e.mtime = mt;
    if (mt != 0) {
        try {
            e.mat = cv::imread(path.toStdString(), flags);
        } catch (...) {
            e.mat = cv::Mat();
        }
    }
    // 同路径换文件：把旧 mtime 的条目摘掉（否则换型后内存随次数增长）
    for (QHash<QString, ImgEntry>::iterator i = imgTable().begin(); i != imgTable().end();) {
        const QString prefix = QDir::cleanPath(path) + QLatin1Char('|') + QString::number(flags)
                               + QLatin1Char('|');
        if (i.key().startsWith(prefix) && i.key() != key) {
            imgOrder().removeAll(i.key());   // 顺序表同步（r19 复核 P3）：否则积累失效键
            i = imgTable().erase(i);
        } else {
            ++i;
        }
    }
    imgTable().insert(key, e);
    // 上限（r19 复核 P2）：模板/样本路径多了会一直累积。只淘汰本表最旧的插入项，
    // 正在被调用方使用的 cv::Mat 是引用计数数据，表里删掉不影响已返回的副本。
    imgOrder().append(key);
    constexpr int kMaxImages = 32;
    while (imgTable().size() > kMaxImages && !imgOrder().isEmpty()) {
        const QString victim = imgOrder().takeFirst();   // 真正最旧的插入项
        if (victim != key)
            imgTable().remove(victim);
    }
    return e.mat;
}

void ModelCache::keypointsFor(const QString &templateKey, int detectorIdx, int nFeatures,
                              const cv::Mat &tmpl,
                              std::vector<cv::KeyPoint> *kp, cv::Mat *desc)
{
    if (!kp || !desc)
        return;
    const QString key = templateKey + QLatin1Char('|') + QString::number(detectorIdx)
                        + QLatin1Char('|') + QString::number(nFeatures);
    {
        QMutexLocker<QMutex> locker(&mapMutex());
        QHash<QString, KpEntry>::const_iterator it = kpTable().constFind(key);
        if (it != kpTable().constEnd()) {
            kpOrder().removeAll(key);
            kpOrder().append(key);
            *kp = it.value().kp;
            *desc = it.value().desc;
            return;
        }
    }

    // 提取在锁外做（可能几十毫秒），做完再入表
    KpEntry e;
    try {
        cv::Ptr<cv::Feature2D> f2d;
        // 与 OpencvFeatureMatchNode::makeDetector 同一映射（1=SIFT，其余=ORB）
        if (detectorIdx == 1)
            f2d = cv::SIFT::create(nFeatures > 0 ? nFeatures : INT_MAX);
        else
            f2d = cv::ORB::create(nFeatures > 0 ? nFeatures : INT_MAX);
        f2d->detectAndCompute(tmpl, cv::noArray(), e.kp, e.desc);
    } catch (...) {
        e.kp.clear();
        e.desc = cv::Mat();
    }

    QMutexLocker<QMutex> locker(&mapMutex());
    kpTable().insert(key, e);
    kpOrder().append(key);
    constexpr int kMaxKeypointSets = 32;
    while (kpTable().size() > kMaxKeypointSets && !kpOrder().isEmpty()) {
        const QString victim = kpOrder().takeFirst();
        if (victim != key)
            kpTable().remove(victim);
    }
    *kp = e.kp;
    *desc = e.desc;
}

void ModelCache::clear()
{
    QMutexLocker<QMutex> locker(&mapMutex());
    imgTable().clear();
    kpTable().clear();
    imgOrder().clear();
    kpOrder().clear();
}

int ModelCache::size()
{
    QMutexLocker<QMutex> locker(&mapMutex());
    return int(imgTable().size() + kpTable().size());
}
