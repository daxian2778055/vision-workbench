#pragma once

#include <QPointF>
#include <QSharedPointer>
#include <QString>
#include <QVector>

class DataObject;

/// 拟合点集来源（VM「图形生成 → 直线 / 圆」的输入点集订阅）。
///
/// 组（group）= 一段连续点集：XLD 的一条轮廓 / Region 的一条外边界 / 卡尺的一批边缘点 / 单个点。
/// 「全局法」把全部组合并成一个点集拟合，「局部最优」逐组拟合后取残差最小的那一组。
/// 两个拟合节点共用本文件的取点与分组口径，避免各写一套导致行为漂移。
namespace FitPointSource {

/// 订阅逻辑槽名（直线拟合 / 圆拟合共用；文档与测试按此名引用）
QString slotName();

/// 「按坐标」订阅的两个数值槽名（VM 输入方式 = 按坐标）
QString pointXSlotName();
QString pointYSlotName();

/// 订阅槽的原始输入（由节点按绑定状态填充；未绑定的槽给空指针）
struct SubscriptionInputs
{
    bool pointBound = false;
    bool xBound = false;
    bool yBound = false;
    QSharedPointer<DataObject> point;
    QSharedPointer<DataObject> x;
    QSharedPointer<DataObject> y;
};

/// 按「输入方式」把订阅槽解析成候选点组（VM「输入方式 = 按点 / 按坐标」）。
///   inputMode 0 = 按点：只用 `point` 槽（X/Y 有绑定视为配置冲突）；
///   inputMode 1 = 按坐标：点集X 与 点集Y 必须同时绑定且数量一致，按序配成点。
/// 返回 false 时 reason 必非空（可传 nullptr）；返回 true 时 groups 非空。
bool collectSubscribedGroups(const SubscriptionInputs &inputs, int inputMode,
                             QVector<QVector<QPointF>> *groups, QString *reason);

/// 一组数值（Number / Array / 逗号分号分隔的字符串）→ QVector<double>。
/// 用于「按坐标」订阅；解析失败时 reason 给出可读原因。
bool collectNumberSeries(const QSharedPointer<DataObject> &data, QVector<double> *values,
                         QString *reason);

/// 从订阅数据中取候选点组（QPointF.x = 列 col，QPointF.y = 行 row，均为整图坐标）。
///
/// 支持的数据形态：
///   - XLD：每个 XLD 对象一组（多边形对象按顶点取点）；
///   - Region：每个 Region 的外边界一组；
///   - Point：单点一组；
///   - 卡尺类 Measure（type == "caliper"）：extraValues 的 [col,row,...] 一组。
///
/// B1 统一输出端口上的"图像 + 具名子结果"对象：主类型不是点集时，取其中携带的
/// XLD / Region / Point / 卡尺点集子结果（只下探一层，避免递归进无关的聚合对象）。
///
/// 返回 false 时 reason 必被写入（可传 nullptr）；返回 true 时 groups 非空。
bool collectGroups(const QSharedPointer<DataObject> &data,
                   QVector<QVector<QPointF>> *groups, QString *reason);

/// 多组按点数从多到少排序后只保留前 maxGroups 组（<= 0 表示不截取）。
void limitGroups(QVector<QVector<QPointF>> *groups, int maxGroups);

/// 合并为一个点集（「全局法」）。
QVector<QPointF> mergeGroups(const QVector<QVector<QPointF>> &groups);

// ---- VM「模块结果」逐点字段的格式化（两个拟合节点共用，避免各写一套口径）----

/// 逐点结果进入「模块结果」表的行数上限：超过时表格只给一行摘要，
/// 参数里的逐点串仍完整写入（避免上万行 QTreeWidgetItem 拖死界面）。
int maxTablePointRows();

/// 超限时的摘要文本；未超限返回空串
QString pointRowsHint(int inputCount);

/// 数值数组 → "d0,d1,..."（固定小数位）
QString joinNumbers(const QVector<double> &values, int decimals);

/// 匹配掩码 → "1,0,..."
QString joinMask(const QVector<bool> &mask);

/// 点数组 → "col,row;col,row;..."（官方「输出拟合点」）
QString joinPoints(const QVector<QPointF> &points, int decimals);

} // namespace FitPointSource
