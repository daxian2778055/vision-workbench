#pragma once

#include "DataObject.h"   // DetectionBox

#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

/// 流水线目标计数（2026-10-28）：检测只给"帧内框"，计数需要**跨帧身份**。
/// 这里是最小可用的跟踪 + 过线判定核心，与 UI/节点解耦，便于单测（合成轨迹即可验证）。
///
/// 为什么不用现成跟踪器：ByteTrack/DeepSORT 要么依赖检测置信度分层与外观特征，要么需要
/// ReID 模型；产线计数只需"稳定 ID + 过线去重"，用 IOU + 匀速预测足够，且零额外依赖。

/// 一条轨迹
struct ObjectTrack {
    int id = -1;
    cv::Rect2f box;          // 当前框（原图坐标）
    cv::Point2f center;      // 当前中心
    cv::Point2f velocity;    // 像素/帧（漏检时用预测位）
    int hits = 0;            // 匹配上的累计帧数
    int missed = 0;          // 连续未匹配帧数
    bool counted = false;    // 已计入过线（同一 ID 只计一次）
    QString className;
    float score = 0.f;
};

/// IOU 跟踪器：贪心匹配 + 匀速预测 + 寿命管理
class ObjectTracker
{
public:
    struct Config {
        float iouThreshold = 0.3f;   // 匹配下限
        int maxMissed = 15;          // 连续漏检多少帧后丢弃
        int minHits = 3;             // 至少匹配多少帧才算确认目标（去抖）
    };

    explicit ObjectTracker(const Config &c = Config()) : m_cfg(c) {}

    QVector<ObjectTrack> update(const QVector<DetectionBox> &dets)
    {
        // 1) 匀速预测
        for (ObjectTrack &t : m_tracks) {
            t.box.x += t.velocity.x;
            t.box.y += t.velocity.y;
            t.center = centerOf(t.box);
        }

        // 2) 贪心匹配
        struct Pair { int ti; int di; float iou; };
        std::vector<Pair> pairs;
        for (int ti = 0; ti < m_tracks.size(); ++ti) {
            for (int di = 0; di < dets.size(); ++di) {
                // 类别一致才允许匹配（2026-10-28 复核 P3）：不同类别的框高度重叠时会
                // 互相"吃掉"身份，className 被改写，按类计数就会串。
                if (!m_tracks[ti].className.isEmpty() && !dets[di].className.isEmpty()
                    && m_tracks[ti].className != dets[di].className)
                    continue;
                const float iou = iouOf(m_tracks[ti].box, rectOf(dets[di]));
                if (iou >= m_cfg.iouThreshold)
                    pairs.push_back({ti, di, iou});
            }
        }
        std::sort(pairs.begin(), pairs.end(),
                  [](const Pair &a, const Pair &b) { return a.iou > b.iou; });
        std::vector<char> trackUsed(size_t(m_tracks.size()), 0);
        std::vector<char> detUsed(size_t(dets.size()), 0);
        for (const Pair &p : pairs) {
            if (trackUsed[size_t(p.ti)] || detUsed[size_t(p.di)])
                continue;
            trackUsed[size_t(p.ti)] = 1;
            detUsed[size_t(p.di)] = 1;
            applyMatch(m_tracks[p.ti], dets[p.di]);
        }

        // 3) 未匹配轨迹累积漏检
        for (int ti = 0; ti < m_tracks.size(); ++ti)
            if (!trackUsed[size_t(ti)])
                ++m_tracks[ti].missed;

        // 4) 未匹配检测 → 新轨迹
        for (int di = 0; di < dets.size(); ++di) {
            if (detUsed[size_t(di)])
                continue;
            ObjectTrack t;
            t.id = m_nextId++;
            t.box = rectOf(dets[di]);
            t.center = centerOf(t.box);
            t.hits = 1;
            t.className = dets[di].className;
            t.score = float(dets[di].confidence);
            m_tracks.push_back(t);
        }

        // 5) 丢弃久未跟上的
        m_tracks.erase(std::remove_if(m_tracks.begin(), m_tracks.end(),
                                      [this](const ObjectTrack &t) {
                                          return t.missed > m_cfg.maxMissed;
                                      }),
                       m_tracks.end());
        return m_tracks;
    }

    /// 只更新配置，不清轨迹（2026-10-28：节点每帧都会带最新参数进来）
    void setConfig(const Config &c) { m_cfg = c; }

    void reset()
    {
        m_tracks.clear();
        m_nextId = 1;
    }
    const QVector<ObjectTrack> &tracks() const { return m_tracks; }
    bool confirmed(const ObjectTrack &t) const { return t.hits >= m_cfg.minHits; }

private:
    static cv::Rect2f rectOf(const DetectionBox &d)
    {
        return cv::Rect2f(float(d.x), float(d.y), float(d.w), float(d.h));
    }
    static cv::Point2f centerOf(const cv::Rect2f &r)
    {
        return cv::Point2f(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
    }
    static float iouOf(const cv::Rect2f &a, const cv::Rect2f &b)
    {
        const float x1 = std::max(a.x, b.x);
        const float y1 = std::max(a.y, b.y);
        const float x2 = std::min(a.x + a.width, b.x + b.width);
        const float y2 = std::min(a.y + a.height, b.y + b.height);
        const float w = x2 - x1;
        const float h = y2 - y1;
        if (w <= 0 || h <= 0)
            return 0.f;
        const float inter = w * h;
        const float uni = a.width * a.height + b.width * b.height - inter;
        return uni > 0 ? inter / uni : 0.f;
    }
    void applyMatch(ObjectTrack &t, const DetectionBox &d)
    {
        const cv::Rect2f nb = rectOf(d);
        const cv::Point2f nc = centerOf(nb);
        // 速度平滑：单帧抖动不至于把预测甩出去
        t.velocity.x = 0.5f * (nc.x - t.center.x) + 0.5f * t.velocity.x;
        t.velocity.y = 0.5f * (nc.y - t.center.y) + 0.5f * t.velocity.y;
        t.box = nb;
        t.center = nc;
        t.missed = 0;
        ++t.hits;
        t.className = d.className;
        t.score = float(d.confidence);
    }

    Config m_cfg;
    QVector<ObjectTrack> m_tracks;
    int m_nextId = 1;
};

/// 过线计数器：线段 a-b 为计数线，目标中心从一侧穿到另一侧即计 1（同一 ID 只计一次）
class LineCounter
{
public:
    struct Event {
        int trackId = -1;
        QString className;
        int direction = 0;   // +1 = 由负侧到正侧
    };

    LineCounter() = default;
    LineCounter(const cv::Point2f &a, const cv::Point2f &b) : m_a(a), m_b(b) {}

    void setLine(const cv::Point2f &a, const cv::Point2f &b)
    {
        m_a = a;
        m_b = b;
        m_lastSide.clear();
        m_counted.clear();
    }
    bool valid() const { return std::hypot(m_b.x - m_a.x, m_b.y - m_a.y) > 1.0; }

    /// allowedDirection: 0=双向；+1 只接受"负侧→正侧"；-1 只接受"正侧→负侧"。
    /// 2026-10-28 复核 P2-2：**未接受的方向不能写入已计集合**，否则"先反向（被过滤）
    /// 再正向（同 ID 已在已计里）"会永久漏计，回流/返工的工位就是这么少数的。
    QVector<Event> update(const QVector<ObjectTrack> &tracks, bool confirmedOnly,
                          const std::function<bool(const ObjectTrack &)> &isConfirmed,
                          int allowedDirection = 0)
    {
        QVector<Event> events;
        if (!valid())
            return events;
        for (const ObjectTrack &t : tracks) {
            if (confirmedOnly && isConfirmed && !isConfirmed(t))
                continue;
            const float side = sideOf(t.center);
            auto it = m_lastSide.find(t.id);
            if (it == m_lastSide.end()) {
                m_lastSide.insert(t.id, side);
                continue;
            }
            const float prev = it.value();
            // 压线(0) 时不更新侧记录：否则"0 → 正侧"会被当成没有前值而漏计（2026-10-28 实测）
            if (side != 0.f)
                it.value() = side;
            // 只认"明确换边"且未计过：同侧抖动、压线(0) 都不算
            if (m_counted.contains(t.id) || prev == 0.f || side == 0.f
                || (prev > 0) == (side > 0))
                continue;
            Event e;
            e.trackId = t.id;
            e.className = t.className;
            e.direction = (side > 0) ? +1 : -1;
            // 方向过滤在记账**之前**：被过滤掉的穿越不留痕迹，
            // 目标之后换方向再穿仍应正常计数（2026-10-28 P2-2）
            if (allowedDirection != 0 && e.direction != allowedDirection) {
                // 不更新 m_lastSide 已在上面写过；这里只跳过计数
                continue;
            }
            m_counted.insert(t.id);   // 计数器自己记账：同一 ID 只计一次
            events.append(e);
        }
        // 清理已消失轨迹的侧记录（长跑内存不涨）
        if (m_lastSide.size() > 512) {
            QSet<int> alive;
            for (const ObjectTrack &t : tracks)
                alive.insert(t.id);
            for (auto it = m_lastSide.begin(); it != m_lastSide.end();) {
                if (!alive.contains(it.key()))
                    it = m_lastSide.erase(it);
                else
                    ++it;
            }
            for (auto it = m_counted.begin(); it != m_counted.end();) {
                if (!alive.contains(*it))
                    it = m_counted.erase(it);
                else
                    ++it;
            }
        }
        return events;
    }

private:
    float sideOf(const cv::Point2f &p) const
    {
        const float dx = m_b.x - m_a.x;
        const float dy = m_b.y - m_a.y;
        const float len = std::hypot(dx, dy);
        if (len <= 0.f)
            return 0.f;
        const float cross = ((p.x - m_a.x) * dy - (p.y - m_a.y) * dx) / len;
        if (std::fabs(cross) < 1.0f)   // 1 像素容差：压线不算换边
            return 0.f;
        return cross;
    }

    QSet<int> m_counted;   /// 已计过的轨迹 ID（同一目标只计一次）
    cv::Point2f m_a{0, 0};
    cv::Point2f m_b{0, 0};
    QMap<int, float> m_lastSide;
};
