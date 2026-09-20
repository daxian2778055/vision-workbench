#include "ImageDisplayController.h"

#include "NodeBase.h"
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
    for (int p = 1; p < node->outputPorts().size(); ++p) {
        auto data = node->getOutputData(p);
        if (!data || data->getType() != DataObject::DataType::Measure) continue;
        MeasureResult mr = data->getMeasureResult();
        if (!mr.valid) continue;

        OverlayShape s;
        s.color = QColor(0, 255, 0);
        if (mr.type == QLatin1String("line")) {
            s.type = OverlayShape::Type::Line;
            s.p1 = mr.point1;
            s.p2 = mr.point2;
        } else if (mr.type == QLatin1String("circle")) {
            s.type = OverlayShape::Type::Circle;
            s.p1 = mr.point1;
            s.radius = mr.value;
            s.text = QStringLiteral("r=%1").arg(mr.value, 0, 'f', 2);
        } else if (mr.type == QLatin1String("template")) {
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

bool ImageDisplayController::startRoiPick(NodeBase *node)
{
    if (!m_imageView || !node)
        return false;
    m_roiPickNode = node;
    if (qobject_cast<FindCircleNode *>(node)) {
        m_imageView->setRoiEditable(true, RoiType::Circle);
    } else {
        m_imageView->setRoiEditable(true, RoiType::Line);
    }
    return true;
}

NodeBase *ImageDisplayController::writeRoiToNode(const RoiShape &shape)
{
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
