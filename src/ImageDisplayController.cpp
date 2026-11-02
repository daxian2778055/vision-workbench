#include "ImageDisplayController.h"

#include "NodeBase.h"
#include "HalconNode.h"
#include "FlowScene.h"
#include "DataObject.h"
#include "FindLineNode.h"
#include "FindCircleNode.h"
#include "CaliperMeasureNode.h"

#include <QLabel>
#include <cmath>

using namespace HalconCpp;

ImageDisplayController::ImageDisplayController(HalconWindow *view, QLabel *sourceLabel,
                                               QObject *parent)
    : QObject(parent)
    , m_imageView(view)
    , m_sourceLabel(sourceLabel)
{
}

NodeBase *ImageDisplayController::resolveDisplayNode(NodeBase *fallbackNode) const
{
    // 最高优先级：下拉框选择（按场景独立记忆）
    if (m_sceneProvider) {
        if (FlowScene *scene = m_sceneProvider()) {
            if (m_selectedOutputNodes.contains(scene))
                return m_selectedOutputNodes.value(scene);
        }
    }
    // 次高优先级：画布选中节点
    if (m_canvasSelectionProvider) {
        if (NodeBase *canvasSelected = m_canvasSelectionProvider())
            return canvasSelected;
    }
    // 兜底
    return fallbackNode;
}

void ImageDisplayController::showImage(const HalconCpp::HImage &image, const QString &sourceName,
                                       NodeBase *overlaySource)
{
    if (!m_imageView || !image.IsInitialized())
        return;
    m_imageView->setImage(image, sourceName);
    if (overlaySource)
        m_imageView->setOverlay(collectOverlayFromNode(overlaySource));
    if (m_sourceLabel)
        m_sourceLabel->setText(QStringLiteral("图像来源: %1").arg(sourceName));
}

bool ImageDisplayController::displayNodeOutput(NodeBase *node)
{
    if (!node || !m_imageView)
        return false;
    auto outputData = node->getOutputData(0);
    if (!outputData)
        return false;
    const HImage image = outputData->getHImage();
    if (!image.IsInitialized())
        return false;
    showImage(image, node->fullName(), node);
    return true;
}

QVector<OverlayShape> ImageDisplayController::collectOverlayFromNode(NodeBase *node) const
{
    QVector<OverlayShape> overlay;
    if (!node) return overlay;

    // B1 统一输出：结果不再挂在端口 1..N，而是与图像同一个对象（附加结果表）。
    // 仍然兼容尚未聚合的节点（例如条件判断的分支端口 / 老插件）：端口 1..N 也扫一遍。
    QVector<MeasureResult> results;
    QSharedPointer<DataObject> mainData = node->getOutputData(0);
    if (mainData)
        results = mainData->measureResults();
    for (int p = 1; p < node->logicalOutputPortCount(); ++p) {
        auto data = node->getOutputData(p);
        if (!data) continue;
        for (const MeasureResult &mr : data->measureResults())
            results.append(mr);
    }

    for (const MeasureResult &mr : results) {
        // 未命中也画**灰框候选**（r19 批 A 第 3 步）：VM 的「总能找到，给出分数」——
        // 原先把 !mr.valid 整条丢掉，现场只剩红节点、没有框，无从判断差多少。
        const bool hit = mr.valid;

        OverlayShape s;
        // 命中绿框；未命中灰框候选（r19 批 A 第 3 步）
        s.color = hit ? QColor(0, 255, 0) : QColor(170, 170, 170);
        if (mr.type == QLatin1String("line")) {
            s.type = OverlayShape::Type::Line;
            s.p1 = mr.point1;
            s.p2 = mr.point2;
        } else if (mr.type == QLatin1String("circle")) {
            s.type = OverlayShape::Type::Circle;
            s.p1 = mr.point1;
            s.radius = mr.value;
            s.text = QStringLiteral("r=%1").arg(mr.value, 0, 'f', 2);
        } else if (mr.type == QLatin1String("template") || mr.type == QLatin1String("feature")) {
            if (mr.extraValues.size() >= 7 && mr.extraValues[5] > 1 && mr.extraValues[6] > 1) {
                s.type = OverlayShape::Type::RotatedRect;
                s.p1 = QPointF(mr.extraValues[1], mr.extraValues[0]);
                s.angleDeg = mr.extraValues[3];
                s.width = mr.extraValues[5];
                s.height = mr.extraValues[6];
                s.text = QStringLiteral("score=%1 a=%2°")
                             .arg(mr.value, 0, 'f', 2)
                             .arg(mr.extraValues[3], 0, 'f', 1);
            } else {
                s.type = OverlayShape::Type::Point;
                s.p1 = mr.point1;
                s.text = QStringLiteral("score=%1").arg(mr.value, 0, 'f', 2);
            }
            // 多目标（r19）：matchList 里除第一实例外，其余也画出来（同色同尺寸）
            const QString listText = node->getParam(QStringLiteral("matchList")).toString();
            if (!listText.isEmpty()) {
                const QStringList items = listText.split(QLatin1Char(';'), Qt::SkipEmptyParts);
                for (int k = 1; k < items.size(); ++k) {
                    const QStringList f = items[k].split(QLatin1Char(','));
                    if (f.size() != 3)
                        continue;
                    OverlayShape extra;
                    extra.type = OverlayShape::Type::RotatedRect;
                    extra.p1 = QPointF(f[1].toDouble(), f[0].toDouble());
                    extra.width = s.width;
                    extra.height = s.height;
                    extra.angleDeg = s.angleDeg;
                    extra.color = s.color;
                    extra.text = QStringLiteral("%1").arg(f[2].toDouble(), 0, 'f', 2);
                    overlay.append(extra);
                }
            }
        } else if (mr.type == QLatin1String("defect")) {
            for (int i = 0; i + 3 < mr.extraValues.size(); i += 4) {
                OverlayShape box;
                box.type = OverlayShape::Type::RotatedRect;
                box.width = mr.extraValues[i + 2];
                box.height = mr.extraValues[i + 3];
                box.p1 = QPointF(mr.extraValues[i] + box.width * 0.5,
                                 mr.extraValues[i + 1] + box.height * 0.5);
                box.angleDeg = 0;
                box.color = QColor(255, 60, 60);
                overlay.append(box);
            }
            s.type = OverlayShape::Type::Text;
            s.p1 = mr.point1;
            s.text = QStringLiteral("缺陷面积=%1").arg(mr.value, 0, 'f', 0);
            s.color = QColor(255, 60, 60);
        } else if (mr.type == QLatin1String("caliper")) {
            s.type = OverlayShape::Type::Points;
            for (int i = 0; i + 1 < mr.extraValues.size(); i += 2) {
                s.points.append(QPointF(mr.extraValues[i], mr.extraValues[i + 1]));
            }
        } else {
            s.type = OverlayShape::Type::Point;
            s.p1 = mr.point1;
            s.text = QStringLiteral("%1=%2").arg(mr.valueName).arg(mr.value, 0, 'f', 3);
        }
        overlay.append(s);
    }

    // 检测区域（搜索区域）：模板匹配类工具必须能一眼看到自己的搜索范围。
    // VM 把这块放在图像区配置；我们同样画在主画布上（青框），可拖与否由编辑对象开关决定。
    if (HalconNode *hn = qobject_cast<HalconNode *>(node)) {
        if (hn->hasParam(QStringLiteral("searchRegionWidth"))
            && hn->hasParam(QStringLiteral("searchRegionHeight"))) {
            const QString poly =
                hn->getParam(QStringLiteral("searchRegionPolygon")).toString().trimmed();
            if (!poly.isEmpty()) {
                QVector<QPointF> pts;
                for (const QString &it : poly.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                    const QStringList xy = it.split(QLatin1Char(','));
                    if (xy.size() == 2)
                        pts.append(QPointF(xy[0].toDouble(), xy[1].toDouble()));
                }
                for (int i = 0; i + 1 < pts.size(); ++i) {
                    OverlayShape e;
                    e.type = OverlayShape::Type::Line;
                    e.p1 = pts[i];
                    e.p2 = pts[i + 1];
                    e.color = QColor(0, 200, 255);
                    overlay.append(e);
                }
                if (pts.size() >= 3) {
                    OverlayShape e;
                    e.type = OverlayShape::Type::Line;
                    e.p1 = pts.last();
                    e.p2 = pts.first();
                    e.color = QColor(0, 200, 255);
                    overlay.append(e);
                }
            } else {
                const double col = hn->getParam(QStringLiteral("searchRegionCol")).toDouble();
                const double row = hn->getParam(QStringLiteral("searchRegionRow")).toDouble();
                const double w = hn->getParam(QStringLiteral("searchRegionWidth")).toDouble();
                const double h = hn->getParam(QStringLiteral("searchRegionHeight")).toDouble();
                if (w > 1.0 && h > 1.0) {
                    OverlayShape box;
                    box.type = OverlayShape::Type::RotatedRect;
                    box.p1 = QPointF(col + w * 0.5, row + h * 0.5);
                    box.width = w;
                    box.height = h;
                    box.angleDeg = 0.0;
                    box.color = QColor(0, 200, 255);
                    overlay.append(box);
                }
            }
        }
    }
    return overlay;
}

void ImageDisplayController::setSelectedOutputNode(FlowScene *scene, NodeBase *node)
{
    if (!scene)
        return;
    if (node)
        m_selectedOutputNodes[scene] = node;
    else
        m_selectedOutputNodes.remove(scene);
}

NodeBase *ImageDisplayController::selectedOutputNode(FlowScene *scene) const
{
    return m_selectedOutputNodes.value(scene, nullptr);
}

void ImageDisplayController::clearSelection()
{
    m_selectedOutputNodes.clear();
}

void ImageDisplayController::removeScene(FlowScene *scene)
{
    if (!scene)
        return;
    m_selectedOutputNodes.remove(scene);
}

bool ImageDisplayController::showNodeGeometry(NodeBase *node)
{
    if (!m_imageView)
        return false;
    HalconNode *hn = qobject_cast<HalconNode *>(node);
    if (!hn || hn->geometryRoiType() == RoiType::None) {
        m_geometryRoiNode = nullptr;
        m_searchRegionNode = nullptr;
        m_imageView->clearRoi();
        m_imageView->setFeaturePoints({});
        m_imageView->setRoiHandleEditEnabled(false);
        return false;
    }
    m_geometryRoiNode = node;
    m_searchRegionNode = nullptr;
    m_roiPickNode = nullptr;                 // 与"取点"互斥
    m_imageView->setRoiEditable(false);      // 只拖句柄，不进入重画模式
    m_imageView->setRoiColor(QColor(0, 200, 80));
    m_imageView->setRoiShape(hn->geometryRoi());
    m_imageView->setFeaturePoints(hn->editorFeaturePoints());   // 主画布也能看到模板特征点
    m_imageView->setRoiHandleEditEnabled(true);
    return true;
}

bool ImageDisplayController::hasSearchRegion(NodeBase *node)
{
    HalconNode *hn = qobject_cast<HalconNode *>(node);
    return hn && hn->hasParam(QStringLiteral("searchRegionWidth"))
           && hn->hasParam(QStringLiteral("searchRegionHeight"));
}

bool ImageDisplayController::showNodeSearchRegion(NodeBase *node)
{
    if (!m_imageView || !hasSearchRegion(node))
        return false;
    HalconNode *hn = qobject_cast<HalconNode *>(node);
    m_searchRegionNode = node;
    m_geometryRoiNode = nullptr;
    m_roiPickNode = nullptr;
    const double col = hn->getParam(QStringLiteral("searchRegionCol")).toDouble();
    const double row = hn->getParam(QStringLiteral("searchRegionRow")).toDouble();
    const double w = hn->getParam(QStringLiteral("searchRegionWidth")).toDouble();
    const double h = hn->getParam(QStringLiteral("searchRegionHeight")).toDouble();
    RoiShape s;
    s.type = RoiType::Rect;
    s.p1 = QPointF(col, row);
    s.p2 = QPointF(col + qMax(0.0, w), row + qMax(0.0, h));
    m_imageView->setRoiEditable(false);
    m_imageView->setRoiColor(QColor(0, 200, 255));
    m_imageView->setRoiShape(s);
    m_imageView->setRoiHandleEditEnabled(true);
    return true;
}

bool ImageDisplayController::startRoiPick(NodeBase *node)
{
    if (!m_imageView || !node)
        return false;
    m_roiPickNode = node;
    m_geometryRoiNode = nullptr;             // 取点期间不再同时编辑几何
    m_searchRegionNode = nullptr;
    if (qobject_cast<FindCircleNode *>(node)) {
        m_imageView->setRoiEditable(true, RoiType::Circle);
    } else {
        m_imageView->setRoiEditable(true, RoiType::Line);
    }
    return true;
}

NodeBase *ImageDisplayController::writeRoiToNode(const RoiShape &shape)
{
    // B3：主画布"搜索区域"编辑——写回 4 个参数（只写参数，不打断执行线程，下一轮生效）
    if (m_searchRegionNode && shape.type == RoiType::Rect) {
        HalconNode *hn = qobject_cast<HalconNode *>(m_searchRegionNode);
        if (hn) {
            const QRectF b = QRectF(shape.p1, shape.p2).normalized();
            if (b.width() > 1.0 && b.height() > 1.0) {
                if (FlowScene *fs = hn->flowSceneRef())
                    fs->recordUndo();
                hn->setParam(QStringLiteral("searchRegionCol"), int(b.x() + 0.5));
                hn->setParam(QStringLiteral("searchRegionRow"), int(b.y() + 0.5));
                hn->setParam(QStringLiteral("searchRegionWidth"), qMax(1, int(b.width() + 0.5)));
                hn->setParam(QStringLiteral("searchRegionHeight"), qMax(1, int(b.height() + 0.5)));
                if (m_imageView)
                    m_imageView->setRoiShape(shape);
            }
            return hn;
        }
    }

    // B3：主画布几何编辑（不是"取点"）——写回 applyGeometryRoi，节点保持可继续拖。
    if (m_geometryRoiNode && shape.type != RoiType::None) {
        HalconNode *hn = qobject_cast<HalconNode *>(m_geometryRoiNode);
        if (hn && hn->geometryRoiType() != RoiType::None) {
            if (FlowScene *fs = hn->flowSceneRef())
                fs->recordUndo();
            hn->applyGeometryRoi(shape);
            if (m_imageView)
                m_imageView->setRoiShape(hn->geometryRoi());
            return hn;
        }
    }

    NodeBase *node = m_roiPickNode;
    m_roiPickNode = nullptr;
    if (m_imageView) {
        m_imageView->setRoiEditable(false);
    }
    if (!node || shape.type == RoiType::None) {
        return nullptr;
    }

    const double r1 = shape.p1.y();
    const double c1 = shape.p1.x();
    if (auto *fl = qobject_cast<FindLineNode *>(node)) {
        fl->setParam(QStringLiteral("row1"), r1);
        fl->setParam(QStringLiteral("col1"), c1);
        fl->setParam(QStringLiteral("row2"), shape.p2.y());
        fl->setParam(QStringLiteral("col2"), shape.p2.x());
    } else if (auto *cc = qobject_cast<CaliperMeasureNode *>(node)) {
        cc->setParam(QStringLiteral("row1"), r1);
        cc->setParam(QStringLiteral("col1"), c1);
        cc->setParam(QStringLiteral("row2"), shape.p2.y());
        cc->setParam(QStringLiteral("col2"), shape.p2.x());
    } else if (auto *fc = qobject_cast<FindCircleNode *>(node)) {
        fc->setParam(QStringLiteral("row"), r1);
        fc->setParam(QStringLiteral("column"), c1);
        const double rad = std::hypot(shape.p2.x() - c1, shape.p2.y() - r1);
        if (rad > 1.0) {
            fc->setParam(QStringLiteral("radius"), rad);
        }
    } else {
        return nullptr;
    }

    return node;
}
