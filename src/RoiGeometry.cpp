#include "RoiGeometry.h"

#include <cmath>

// ROI 交互判定面（词条切片 B 图元族首抽，从 HalconWindow 抽出）：
// 手柄命中与手柄拖拽两段几何判定，逐式复刻原体（含八像素平方阈值、整体移动逐形状
// 判定、旋转矩形局部坐标、线段投影距离与最小二像素夹持），控件坐标取整语义原样。

RoiHandle RoiGeometry::hitTest(const RoiShape &shape, const QPoint &widgetPos,
                                double scaleFactor, qreal imageOffsetX, qreal imageOffsetY,
                                bool hasImage)
{
    if (shape.type == RoiType::None)
        return RoiHandle::None;

    QVector<RoiHandle> hs;
    switch (shape.type) {
    case RoiType::Rect:
        hs = {RoiHandle::C0, RoiHandle::C1, RoiHandle::C2, RoiHandle::C3};
        break;
    case RoiType::RotatedRect:
        hs = {RoiHandle::Rotate, RoiHandle::C0, RoiHandle::C1, RoiHandle::C2, RoiHandle::C3};
        break;
    case RoiType::Line:
        hs = {RoiHandle::End1, RoiHandle::End2};
        break;
    case RoiType::Circle:
        hs = {RoiHandle::C0};
        break;
    case RoiType::Point:
        hs = {RoiHandle::C0};
        break;
    default:
        break;
    }

    const double thresh2 = 8.0 * 8.0;
    for (RoiHandle h : hs) {
        const QPoint w = widgetPosFromImage(handleImagePos(shape, h), scaleFactor,
                                             imageOffsetX, imageOffsetY);
        const double dx = double(w.x() - widgetPos.x());
        const double dy = double(w.y() - widgetPos.y());
        if (dx * dx + dy * dy <= thresh2)
            return h;
    }

    if (!hasImage || scaleFactor <= 1e-9)
        return RoiHandle::None;
    const QPointF img((widgetPos.x() - imageOffsetX) / scaleFactor,
                      (widgetPos.y() - imageOffsetY) / scaleFactor);

    if (shape.type == RoiType::Rect) {
        if (roiAxisAlignedBounds(shape).contains(img))
            return RoiHandle::Move;
    } else if (shape.type == RoiType::RotatedRect && shape.width > 1 && shape.height > 1) {
        const double a = qDegreesToRadians(shape.angleDeg);
        const double c = std::cos(a);
        const double sn = std::sin(a);
        const double dx = img.x() - shape.p1.x();
        const double dy = img.y() - shape.p1.y();
        const double lx = c * dx + sn * dy;
        const double ly = -sn * dx + c * dy;
        if (std::abs(lx) <= shape.width * 0.5 && std::abs(ly) <= shape.height * 0.5)
            return RoiHandle::Move;
    } else if (shape.type == RoiType::Line) {
        const QPoint a = widgetPosFromImage(shape.p1, scaleFactor, imageOffsetX, imageOffsetY);
        const QPoint b = widgetPosFromImage(shape.p2, scaleFactor, imageOffsetX, imageOffsetY);
        const QPointF ab(b.x() - a.x(), b.y() - a.y());
        const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
        if (len2 > 1.0) {
            const QPointF ap(widgetPos.x() - a.x(), widgetPos.y() - a.y());
            const double t = qBound(0.0, (ap.x() * ab.x() + ap.y() * ab.y()) / len2, 1.0);
            const QPointF proj(a.x() + ab.x() * t, a.y() + ab.y() * t);
            if (QLineF(QPointF(widgetPos), proj).length() <= 8.0)
                return RoiHandle::Move;
        }
    } else if (shape.type == RoiType::Circle) {
        const double r = QLineF(shape.p1, shape.p2).length();
        if (QLineF(shape.p1, img).length() <= r)
            return RoiHandle::Move;
    }
    return RoiHandle::None;
}

RoiShape RoiGeometry::dragShape(const RoiShape &startShape, RoiHandle handle,
                                 const QPointF &imgPos, const QPointF &startImgPos)
{
    const QPointF delta = imgPos - startImgPos;
    RoiShape s = startShape;

    if (s.type == RoiType::Rect) {
        QPointF tl(qMin(s.p1.x(), s.p2.x()), qMin(s.p1.y(), s.p2.y()));
        QPointF br(qMax(s.p1.x(), s.p2.x()), qMax(s.p1.y(), s.p2.y()));
        if (handle == RoiHandle::Move) {
            tl += delta;
            br += delta;
        } else if (handle == RoiHandle::C0) {
            tl = imgPos;
        } else if (handle == RoiHandle::C1) {
            tl.setY(imgPos.y());
            br.setX(imgPos.x());
        } else if (handle == RoiHandle::C2) {
            br = imgPos;
        } else if (handle == RoiHandle::C3) {
            tl.setX(imgPos.x());
            br.setY(imgPos.y());
        }
        if (br.x() < tl.x() + 2.0)
            br.setX(tl.x() + 2.0);
        if (br.y() < tl.y() + 2.0)
            br.setY(tl.y() + 2.0);
        s.p1 = tl;
        s.p2 = br;
    } else if (s.type == RoiType::RotatedRect) {
        if (handle == RoiHandle::Move) {
            s.p1 += delta;
        } else if (handle == RoiHandle::Rotate) {
            s.angleDeg = qRadiansToDegrees(std::atan2(imgPos.x() - s.p1.x(),
                                                      s.p1.y() - imgPos.y()));
        } else if (handle == RoiHandle::C0 || handle == RoiHandle::C1
                   || handle == RoiHandle::C2 || handle == RoiHandle::C3) {
            const int i = int(handle) - int(RoiHandle::C0);
            const auto corners = rotatedRectCorners(s);
            if (corners.size() == 4) {
                const QPointF opp = corners[(i + 2) % 4];
                const double a = qDegreesToRadians(s.angleDeg);
                const double c = std::cos(a);
                const double sn = std::sin(a);
                const QPointF vec = imgPos - opp;
                s.width = qMax(2.0, std::abs(c * vec.x() + sn * vec.y()));
                s.height = qMax(2.0, std::abs(-sn * vec.x() + c * vec.y()));
                s.p1 = (opp + imgPos) * 0.5;
            }
        }
    } else if (s.type == RoiType::Line) {
        if (handle == RoiHandle::End1)
            s.p1 = imgPos;
        else if (handle == RoiHandle::End2)
            s.p2 = imgPos;
        else if (handle == RoiHandle::Move) {
            s.p1 += delta;
            s.p2 += delta;
        }
    } else if (s.type == RoiType::Circle) {
        if (handle == RoiHandle::Move) {
            s.p1 += delta;
            s.p2 += delta;
        } else if (handle == RoiHandle::C0) {
            s.p2 = imgPos;
        }
    } else if (s.type == RoiType::Point) {
        s.p1 = imgPos;
    }
    return s;
}

QPointF RoiGeometry::handleImagePos(const RoiShape &shape, RoiHandle handle)
{
    if (shape.type == RoiType::Rect) {
        const QRectF r = roiAxisAlignedBounds(shape);
        if (handle == RoiHandle::C0) return r.topLeft();
        if (handle == RoiHandle::C1) return r.topRight();
        if (handle == RoiHandle::C2) return r.bottomRight();
        if (handle == RoiHandle::C3) return r.bottomLeft();
        if (handle == RoiHandle::Move) return r.center();
    } else if (shape.type == RoiType::RotatedRect) {
        const auto c = rotatedRectCorners(shape);
        if (c.size() == 4) {
            if (handle == RoiHandle::C0) return c[0];
            if (handle == RoiHandle::C1) return c[1];
            if (handle == RoiHandle::C2) return c[2];
            if (handle == RoiHandle::C3) return c[3];
        }
        if (handle == RoiHandle::Move) return shape.p1;
        if (handle == RoiHandle::Rotate) {
            const double a = qDegreesToRadians(shape.angleDeg);
            const double lift = shape.height * 0.5 + 28.0;
            return QPointF(shape.p1.x() + std::sin(a) * lift,
                           shape.p1.y() - std::cos(a) * lift);
        }
    } else if (shape.type == RoiType::Line) {
        if (handle == RoiHandle::End1) return shape.p1;
        if (handle == RoiHandle::End2) return shape.p2;
        if (handle == RoiHandle::Move) return (shape.p1 + shape.p2) * 0.5;
    } else if (shape.type == RoiType::Circle) {
        if (handle == RoiHandle::Move) return shape.p1;
        if (handle == RoiHandle::C0) return shape.p2;
    } else if (shape.type == RoiType::Point) {
        if (handle == RoiHandle::Move || handle == RoiHandle::C0) return shape.p1;
    }
    return QPointF();
}

QPoint RoiGeometry::widgetPosFromImage(const QPointF &imgPos, double scaleFactor,
                                        qreal imageOffsetX, qreal imageOffsetY)
{
    return QPoint(static_cast<int>(imageOffsetX + imgPos.x() * scaleFactor),
                  static_cast<int>(imageOffsetY + imgPos.y() * scaleFactor));
}
