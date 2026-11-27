#pragma once

// 最新帧队列（AG4a）：为"采集与处理解耦"准备的可测纯数据面。
// 当前**不接入** MvsImageSourceNode 的抓图路径（无真机环境下异步化有共享句柄互偷帧、
// 拆句柄 UAF、缓冲竞态等风险，须硬件在环后再做）。本类只把"有界 + 最新帧覆盖 +
// 帧号对齐 + 丢帧计数"的语义钉成可单测的契约。

#include <QMutex>
#include <QMutexLocker>
#include <QByteArray>
#include <QVector>

#include <cstdint>

class LatestFrame
{
public:
    quint64 sequence = 0;      ///< 来自 SDK 的帧号（nFrameNum），由调用方转成 64 位避免回绕误判
    QByteArray bytes;          ///< 该帧自有缓冲，不与消费线程共享
    int width = 0;
    int height = 0;
    int pixelType = 0;
    qint64 timestampMs = 0;
};

class LatestFrameQueue
{
public:
    struct Stats {
        quint64 pushed = 0;       ///< 成功入队次数
        quint64 consumed = 0;     ///< 被 takeLatest 取走次数
        quint64 replaced = 0;     ///< 新帧覆盖未被消费的旧帧次数（本地覆盖）
        quint64 gaps = 0;         ///< 帧号前向跳变缺失的帧数
        quint64 duplicates = 0;   ///< 重复/回退帧号次数
        quint64 sdkLostPackets = 0;///< SDK 上报的丢包数（与本地覆盖分开记）
    };

    /// capacity 仅允许 1..8；默认 1 即"只留最新一帧"的语义
    explicit LatestFrameQueue(int capacity = 1)
        : m_capacity(capacity > 0 ? (capacity > 8 ? 8 : capacity) : 1)
    {
        m_frames.resize(m_capacity);
    }

    void push(const LatestFrame &frame)
    {
        QMutexLocker locker(&m_mutex);
        ++m_stats.pushed;
        if (m_stats.pushed > 1) {
            const qint64 diff = static_cast<qint64>(frame.sequence)
                              - static_cast<qint64>(m_lastSequence);
            if (diff > 1)
                m_stats.gaps += static_cast<quint64>(diff - 1);
            else if (diff <= 0)
                ++m_stats.duplicates;
        }
        m_lastSequence = frame.sequence;
        if (m_count == m_capacity) {
            ++m_stats.replaced;
            m_frames[m_tail] = frame;
            m_tail = (m_tail + 1) % m_capacity;
        } else {
            const int pos = (m_tail + m_count) % m_capacity;
            m_frames[pos] = frame;
            ++m_count;
        }
    }

    bool takeLatest(LatestFrame *out)
    {
        QMutexLocker locker(&m_mutex);
        if (m_count == 0)
            return false;
        const int newest = (m_tail + m_count - 1) % m_capacity;
        // 只取最新一帧；更旧的帧直接作废（最新帧语义），并计入本地覆盖。
        if (m_count > 1)
            m_stats.replaced += static_cast<quint64>(m_count - 1);
        if (out)
            *out = m_frames[newest];
        for (int i = 0; i < m_capacity; ++i)
            m_frames[i] = LatestFrame();
        m_count = 0;
        m_tail = 0;
        ++m_stats.consumed;
        return true;
    }

    void noteSdkLostPackets(quint64 n)
    {
        QMutexLocker locker(&m_mutex);
        m_stats.sdkLostPackets += n;
    }

    Stats stats() const
    {
        QMutexLocker locker(&m_mutex);
        return m_stats;
    }

    void clear()
    {
        QMutexLocker locker(&m_mutex);
        for (int i = 0; i < m_capacity; ++i)
            m_frames[i] = LatestFrame();
        m_count = 0;
        m_tail = 0;
        m_stats = Stats();
    }

private:
    mutable QMutex m_mutex;
    int m_capacity = 1;
    int m_count = 0;
    int m_tail = 0;
    quint64 m_lastSequence = 0;
    Stats m_stats;                    ///< 累计读数，只增不清（clear 时才归零）
    QVector<LatestFrame> m_frames;   // 固定容量，push 前已在构造期分配
};
