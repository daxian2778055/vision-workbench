#pragma once

#include <QMetaType>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QtMath>
#include <cmath>

/// ROI 形状类型
enum class RoiType {
    None,
    Rect,          /// 轴对齐矩形（p1=左上, p2=右下）
    Circle,        /// 圆（p1=圆心, p2=圆上一点）
    Line,          /// 直线（p1→p2）
    Point,         /// 点（p1）
    Polygon,       /// 多边形（points）
    RotatedRect,   /// 旋转矩形（p1=中心, width/height, angleDeg）
};

/// ROI 形状（图像坐标系，x=列, y=行）
struct RoiShape {
    RoiType type = RoiType::None;
    QPointF p1;
    QPointF p2;
    QVector<QPointF> points; /// Polygon 时使用
    double angleDeg = 0.0;   /// RotatedRect：逆时针角度（度）
    double width = 0.0;      /// RotatedRect：宽
    double height = 0.0;     /// RotatedRect：高
};
Q_DECLARE_METATYPE(RoiShape)

/// ROI 手柄（交互判定族的分派单位；原为显示窗类内嵌套枚举，为使判定面不依赖控件头
/// 而提出为自由枚举——值域与序不变，仅去嵌套）
enum class RoiHandle {
    None, Move,
    C0, C1, C2, C3,
    Rotate,
    End1, End2
};

inline QVector<QPointF> rotatedRectCorners(const RoiShape &s)
{
    const double a = qDegreesToRadians(s.angleDeg);
    const double c = std::cos(a);
    const double sn = std::sin(a);
    const double hw = s.width * 0.5;
    const double hh = s.height * 0.5;
    const double xs[4] = {-hw, hw, hw, -hw};
    const double ys[4] = {-hh, -hh, hh, hh};
    QVector<QPointF> pts;
    pts.reserve(4);
    for (int i = 0; i < 4; ++i) {
        pts.append(QPointF(s.p1.x() + c * xs[i] - sn * ys[i],
                           s.p1.y() + sn * xs[i] + c * ys[i]));
    }
    return pts;
}

inline QRectF roiAxisAlignedBounds(const RoiShape &s)
{
    if (s.type == RoiType::RotatedRect && s.width > 1 && s.height > 1) {
        const auto pts = rotatedRectCorners(s);
        double x0 = pts[0].x(), x1 = pts[0].x(), y0 = pts[0].y(), y1 = pts[0].y();
        for (const QPointF &p : pts) {
            x0 = qMin(x0, p.x()); x1 = qMax(x1, p.x());
            y0 = qMin(y0, p.y()); y1 = qMax(y1, p.y());
        }
        return QRectF(QPointF(x0, y0), QPointF(x1, y1));
    }
    if (s.type == RoiType::Rect) {
        return QRectF(QPointF(qMin(s.p1.x(), s.p2.x()), qMin(s.p1.y(), s.p2.y())),
                      QPointF(qMax(s.p1.x(), s.p2.x()), qMax(s.p1.y(), s.p2.y())));
    }
    return QRectF();
}
