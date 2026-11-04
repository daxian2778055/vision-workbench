#pragma once

#include <QByteArray>
#include <QVector>
#include <QString>
#include <QMutex>
#include <opencv2/core.hpp>

/// 一个批次条目（名称 + 原始 PNG 编码字节；读取时按需解码）
struct LargeImageEntry {
    QString name;
    QByteArray pngData;
};

/// 采集策略·大文件存图（对标 VisionMaster 4.4 手册 11.5）：
/// 多个输出图像模块的存图请求先攒在内存，达到批量张数后统一写成一个 .dat，
/// 降低高频小文件写入对磁盘的压力；.dat 由本类的 readBatch/extractBatch 解包。
///
/// 线程安全：append/flush/setSettings 可在执行线程与 UI 线程并发调用（内部互斥锁）。
class LargeFileImageStore
{
public:
    struct Settings {
        bool enabled = false;
        QString directory;      ///< .dat 保存目录（启用时必须非空）
        int batchSize = 100;    ///< 每批张数（达到即落盘）
    };

    static LargeFileImageStore *instance();

    Settings settings() const;
    /// 写设置；路径/批量/开关变化前先把未满批次落盘（避免旧目录数据写进新目录）。
    /// 返回 false 时设置未生效，原因写入 error。
    bool setSettings(const Settings &settings, QString *error = nullptr);
    bool enabled() const;
    int pendingCount() const;

    /// 追加一张 8 位图像。成功返回 true；若本次触发了落盘，batchFile 返回写出的 .dat 路径
    /// （未触发时为空）。失败原因写入 error。
    bool append(const cv::Mat &image, const QString &sourceName,
                QString *batchFile, QString *error);
    /// 立即把未满一批写入磁盘；返回本次写出的文件数（0 = 无待写数据）
    int flush();

    /// 读取 .dat（解包工具与测试共用）
    static bool readBatch(const QString &path, QVector<LargeImageEntry> *entries,
                          QString *error);
    /// 解包到目录：每个条目写成 <序号>_<名称>.png；返回写出的文件数
    static int extractBatch(const QString &path, const QString &outDir, QString *error);

private:
    LargeFileImageStore();
    ~LargeFileImageStore() = default;
    /// 持锁调用：把 m_pending 写成一个 .dat（临时文件 + 原子改名）
    bool writePendingBatchLocked(QString *outPath, QString *error);
    void persistSettings() const;

    mutable QMutex m_mutex;
    Settings m_settings;
    QVector<LargeImageEntry> m_pending;
    qint64 m_pendingBytes = 0;   ///< 待写批次的内存占用（防止“每批张数”设太大导致 OOM）
    int m_batchSeq = 0;   ///< 同秒内多次落盘时的序号，避免文件名冲突
};
