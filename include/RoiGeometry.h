#pragma once

#include "RoiShape.h"

#include <QLineF>
#include <QPoint>
#include <QPointF>

/// ROI 交互判定面（词条切片 B 图元族首抽，从 HalconWindow 抽出）：
/// 手柄命中与手柄拖拽两段几何判定——**纯几何**（只吃形状、点、缩放与偏移），
/// 不触控件、不触画布、不触图像，可脱离 GUI 座化。
///
/// 契约钉（与原体逐行同语义）：
///  · hitTest：先按形状分派手柄集（矩形四角、旋转矩形含旋转柄、线段两端、圆与点各一），
///    再以控件坐标下的八像素平方阈值命中最近手柄（首个命中即返回，序不变）；
///    手柄未命中才判「整体移动」——矩形用外接框内含、旋转矩形用局部坐标内含、
///    线段用投影距离八像素内、圆用半径内含；无图像或缩放退化时不给整体移动；
///  · dragShape：手柄 → 新形状（矩形四角分轴与最小二像素夹持、旋转矩形角点重算宽高与
///    中心与旋转柄改角、线段端点、圆圆心与半径点、点单点）；
///  · 坐标变换：图像坐标 → 控件坐标为偏移加缩放后取整（截断语义原样）。
class RoiGeometry final {
public:
    /// 手柄命中（返回命中的手柄；无命中为 RoiHandle::None）。
    /// hasImage 供原体的「无图像则不给整体移动」守卫用。
    static RoiHandle hitTest(const RoiShape &shape, const QPoint &widgetPos,
                             double scaleFactor, qreal imageOffsetX, qreal imageOffsetY,
                             bool hasImage);

    /// 手柄拖拽映射：给定起始形状与起始点，返回拖拽后的新形状。
    static RoiShape dragShape(const RoiShape &startShape, RoiHandle handle,
                               const QPointF &imgPos, const QPointF &startImgPos);

    /// 手柄在图像坐标系的位置（未知手柄返回原点）。
    static QPointF handleImagePos(const RoiShape &shape, RoiHandle handle);

    /// 图像坐标 → 控件坐标（偏移加缩放后取整）。
    static QPoint widgetPosFromImage(const QPointF &imgPos, double scaleFactor,
                                     qreal imageOffsetX, qreal imageOffsetY);
};
