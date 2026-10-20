#pragma once

#include "FlowFixture.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

/// 夹具校验面（词条切片 B 图元族第二抽，从场景图抽出）：
/// 命名夹具的**载入侧校验与降级**与**写入侧两道闸**的唯一口径面。
///
/// 契约钉（与原体逐行同语义）：
///  · 名字闸：去空白后为空即拒收（写入侧静默拒、载入侧整条跳过并回报原因）——两侧同一口径；
///  · 矩阵闸：**恰好六项**（写入侧静默拒、载入侧不合形即**降级**为「没有矩阵」并回报原因，
///    原因含实际项数）；逐项须为数值且有限（载入侧降级并回报第几项不合）；
///  · 位姿闸：四键口径——行与列与角三键必需，缩放键可缺（缺则回落一点零，兼容手写方案）；
///    值须为数值且有限（载入侧不合形即**降级**为「没有位姿」并回报原因）；
///  · 降级语义：不合形的部分单独作废，**不整条丢弃**（位姿坏一键不连累矩阵），且原因必回报
///    （静默丢弃的表现是「方案打开后夹具的矩阵不见了」，现场无从下手）。
class FixtureValidation final {
public:
    /// 载入侧：从一个夹具条目（键为夹具名）装出夹具。
    /// 返回 false = 名字为空（整条跳过）；返回 true = 已装出（矩阵或位姿可能已降级）。
    /// 降级原因逐条追加到 reasons（可为 nullptr）。
    static bool loadFixture(const QString &rawName, const QJsonObject &entry,
                            FlowFixture *out, QStringList *reasons);

    /// 名字闸（写入侧与载入侧同口径）：去空白后非空即受理。
    static bool isNameAccepted(const QString &rawName);

    /// 矩阵项数闸（写入侧与载入侧同口径）：恰好六项才受理。
    static bool isHomographyCountAccepted(int count);
};
