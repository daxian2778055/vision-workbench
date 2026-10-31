#pragma once
// 图形 ↔ 参数 双向绑定（2026-10-31，VM 手感批 A 第 1 步）。
//
// 现状：HalconWindow 已有 VM 级的几何与交互（RoiShape 支持矩形/旋转矩形/多边形/掩膜；
// drawRoiHandles 有拖拽句柄；setOverlay 支持多图形叠加），RoiShape 也有 rotatedRectCorners。
// 缺的只有一层「图形改了回填参数、参数改了重绘图形」——工具各自手写、且只有数值框。
//
// 这一层做的就是那件事：
//   · 一个绑定 = 一个图形（RoiShape）+ 若干参数名（cx/cy/w/h/angle 或多边形点列）；
//   · 图形被拖动 → 同步到参数（setParam），面板实时看到数值；
//   · 参数被改 → 反向更新图形 → 画布重绘；
//   · 工具只需"声明有哪些绑定"，不再自己写同步代码。
//
// 用法（示意）：
//   ShapeBinding b;
//   b.name = QStringLiteral("模板框");
//   b.shape.kind = RoiShape::RotatedRect;
//   b.params = {ShapeBinding::CenterCol, ShapeBinding::CenterRow,
//               ShapeBinding::Width, ShapeBinding::Height, ShapeBinding::Angle};
//   b.setParamSink([&](const QString &k, const QVariant &v) { node.setParam(k, v); });
//   b.syncToParams();          // 首次：图形 → 参数
//   // ... 画布拖动结束 → b.onShapeEdited() 会回填参数
//   // ... 面板改了参数 → b.onParamChanged(k) 会把 shape 拉回来
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QPointF>
#include <QVector>
#include <functional>

#include "RoiShape.h"

/// 图形与参数之间的绑定（VM 手感的核心结构）
struct ShapeBinding {
    /// 图形对应的参数槽位（每种图形用到其中一部分）
    enum ParamSlot {
        CenterCol,   // 旋转矩形中心列
        CenterRow,   // 旋转矩形中心行
        Width,       // 宽
        Height,      // 高
        Angle,       // 角度（度，逆时针）
        Left,        // 轴对齐矩形：左上列
        Top,         // 轴对齐矩形：左上行
        PointCount,  // 多边形点数（可选）
    };

    QString name;                    // 供日志/面板标题用
    RoiShape shape;                  // 图形本体（已在 RoiShape.h：points/angleDeg/width/height）
    QVector<ParamSlot> slots;        // 这个图形绑哪些槽位（顺序与 paramsNames 一一对应）
    QStringList paramsNames;         // 与 slots 等长：每个槽位对应的参数名

    /// 写参数（工具注入：一般是 node->setParam）
    std::function<void(const QString &, const QVariant &)> setParamSink;
    /// 读参数（工具注入：一般是 node->getParam）
    std::function<QVariant(const QString &)> getParamSink;
    /// 图形改动后要求重绘（工具注入：一般是画布 update）
    std::function<void()> repaintSink;

    /// 图形 → 参数（拖动结束/形状编辑后调用）
    void syncToParams() const
    {
        if (!setParamSink)
            return;
        for (int i = 0; i < slots.size() && i < paramsNames.size(); ++i) {
            QVariant v;
            switch (slots[i]) {
            case CenterCol: v = shape.points.isEmpty() ? 0.0 : shape.points.first().x(); break;
            case CenterRow: v = shape.points.isEmpty() ? 0.0 : shape.points.first().y(); break;
            case Width:     v = shape.width;  break;
            case Height:    v = shape.height; break;
            case Angle:     v = shape.angleDeg; break;
            case Left:      v = shape.points.isEmpty() ? 0.0 : shape.points.first().x(); break;
            case Top:       v = shape.points.isEmpty() ? 0.0 : shape.points.first().y(); break;
            case PointCount:v = shape.points.size(); break;
            }
            setParamSink(paramsNames[i], v);
        }
    }

    /// 参数 → 图形（面板改参数后调用）
    void onParamChanged(const QString &paramName)
    {
        if (!getParamSink)
            return;
        for (int i = 0; i < slots.size() && i < paramsNames.size(); ++i) {
            if (paramsNames[i] != paramName)
                continue;
            const QVariant v = getParamSink(paramName);
            if (shape.points.isEmpty())
                shape.points.append(QPointF(0.0, 0.0));
            switch (slots[i]) {
            case CenterCol: shape.points[0].setX(v.toDouble()); break;
            case CenterRow: shape.points[0].setY(v.toDouble()); break;
            case Width:     shape.width = v.toDouble(); break;
            case Height:    shape.height = v.toDouble(); break;
            case Angle:     shape.angleDeg = v.toDouble(); break;
            case Left:      shape.points[0].setX(v.toDouble()); break;
            case Top:       shape.points[0].setY(v.toDouble()); break;
            case PointCount: break;   // 点数由图形侧决定
            }
            break;
        }
        if (repaintSink)
            repaintSink();
    }

    /// 画布侧图形被拖动/旋转后调用（回填参数）
    void onShapeEdited()
    {
        syncToParams();
        if (repaintSink)
            repaintSink();
    }
};
