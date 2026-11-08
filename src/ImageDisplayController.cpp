#include "ImageDisplayController.h"

#include "NodeBase.h"
#include "HalconNode.h"
#include "FlowScene.h"
#include "DataObject.h"
#include "NodeResultFields.h"
#include "GlobalVariableManager.h"
#include "FindLineNode.h"
#include "FindCircleNode.h"
#include "CaliperMeasureNode.h"

#include <QLabel>
#include <QImage>
#include <QRegularExpression>
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

void ImageDisplayController::setDualViewEnabled(bool on)
{
    m_dualViewEnabled = on;
}

bool ImageDisplayController::showInputOnPrimary(NodeBase *moduleNode)
{
    if (!m_imageView || !moduleNode)
        return false;
    auto input = moduleNode->getInputData(0);
    if (!input)
        return false;
    const HImage image = input->getHImage();
    if (!image.IsInitialized())
        return false;
    m_imageView->setImage(image, tr("输入 - %1").arg(moduleNode->fullName()));
    m_imageView->clearOverlay();
    return true;
}

void ImageDisplayController::showImage(const HalconCpp::HImage &image, const QString &sourceName,
                                       NodeBase *overlaySource)
{
    if (!m_imageView || !image.IsInitialized())
        return;

    // 双画面（实装 4.4 口径）：右窗=完整结果，左窗=当前模块输入源；不再做镜像同步
    if (m_dualViewEnabled && m_secondaryView) {
        m_secondaryView->setImage(image, sourceName);
        if (overlaySource)
            m_secondaryView->setOverlay(collectOverlayFromNode(overlaySource));
        else
            m_secondaryView->clearOverlay();

        NodeBase *current = m_canvasSelectionProvider ? m_canvasSelectionProvider() : overlaySource;
        if (!showInputOnPrimary(current))
            m_imageView->clear();
        if (m_sourceLabel)
            m_sourceLabel->setText(QStringLiteral("图像来源: %1").arg(sourceName));
        return;
    }

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

    if (m_dualViewEnabled && m_secondaryView) {
        m_secondaryView->setImage(image, node->fullName());
        m_secondaryView->setOverlay(collectOverlayFromNode(node));
        NodeBase *current = m_canvasSelectionProvider ? m_canvasSelectionProvider() : node;
        if (!showInputOnPrimary(current))
            m_imageView->clear();
        if (m_sourceLabel)
            m_sourceLabel->setText(QStringLiteral("图像来源: %1").arg(node->fullName()));
        return true;
    }

    showImage(image, node->fullName(), node);
    return true;
}

namespace {
/// 参数开关（默认开启）：未注册该键时按默认值走，避免老方案全被关掉
bool showLayer(NodeBase *node, const char *name, bool def = true)
{
    const QVariant v = node->getParam(QString::fromUtf8(name));
    return v.isValid() ? v.toBool() : def;
}

/// 结果文本里的单值格式化：bool 保留 true/false；数值按字段精度（未声明精度用 6 位有效数字）。
QString formatResultValue(const QVariant &value, int decimals)
{
    if (!value.isValid() || value.isNull())
        return QStringLiteral("-");
    if (value.typeId() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.typeId() == QMetaType::QString)
        return value.toString();
    bool numeric = false;
    const double number = value.toDouble(&numeric);
    if (numeric)
        return decimals >= 0 ? QString::number(number, 'f', decimals)
                             : QString::number(number, 'g', 6);
    return value.toString();
}

/// 结果键取值：执行器本轮快照优先，其次节点参数（结果类算子通常两者都有）。
QVariant resultValueByName(NodeBase *node, const QString &key, const QVariantMap &vars)
{
    auto it = vars.constFind(key);
    if (it != vars.constEnd() && it.value().isValid())
        return it.value();
    return node->getParam(key);
}

/// VM「结果显示 → 文本显示」占位符解析（2026-11-06）。
/// 支持 {module} / {status} / {elapsed} / {global.变量名} / {结果键}；
/// 解析不到的键**保持原样**（{key}），现场能一眼区分"变量名写错"和"值为空"。
QString resolveResultText(NodeBase *node, const QString &content, const QVariantMap &vars)
{
    if (!node || content.isEmpty())
        return content;

    const QVector<NodeResultField> spec = node->resultDisplayFields();
    auto decimalsOf = [&spec](const QString &key) {
        for (const NodeResultField &field : spec) {
            if (field.key == key)
                return field.decimals;
        }
        return -1;
    };

    static const QRegularExpression placeholderRe(QStringLiteral(R"(\{([^{}]+)\})"));
    QString out;
    out.reserve(content.size());
    int lastEnd = 0;
    QRegularExpressionMatchIterator it = placeholderRe.globalMatch(content);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        out += content.mid(lastEnd, match.capturedStart() - lastEnd);
        const QString token = match.captured(1).trimmed();
        bool resolved = true;
        QString replacement;
        if (token == QLatin1String("module")) {
            replacement = node->name();
        } else if (token == QLatin1String("status")) {
            const QVariant ms = node->getParam(QStringLiteral("moduleStatus"));
            replacement = (ms.isValid() && ms.toBool()) ? QStringLiteral("OK")
                                                        : QStringLiteral("NG");
        } else if (token == QLatin1String("elapsed")) {
            replacement = formatResultValue(
                resultValueByName(node, QStringLiteral("elapsedMs"), vars), 0);
        } else if (token.startsWith(QLatin1String("global."))) {
            const QString variableName = token.mid(7).trimmed();
            GlobalVariableManager *gvm = GlobalVariableManager::instance();
            if (gvm && gvm->variableExists(variableName))
                replacement = formatResultValue(gvm->getVariable(variableName), -1);
            else
                resolved = false;
        } else {
            const QVariant value = resultValueByName(node, token, vars);
            if (value.isValid() && !value.isNull())
                replacement = formatResultValue(value, decimalsOf(token));
            else
                resolved = false;
        }
        out += resolved ? replacement : match.captured(0);
        lastEnd = match.capturedEnd();
    }
    out += content.mid(lastEnd);
    return out;
}
}   // namespace

QVector<OverlayShape> ImageDisplayController::collectOverlayFromNode(NodeBase *node) const
{
    QVector<OverlayShape> overlay;
    if (!node) return overlay;

    // 本轮结果变量快照（{elapsed} 只在执行器推送的 vars 里，节点参数表没有该键）
    const QVariantMap vars = m_resultVarsProvider ? m_resultVarsProvider(node) : QVariantMap();

    // ── VM「结果显示 → 图像显示」（2026-11-06）：勾选/颜色/透明度/线宽真正作用到结果图元 ──
    // 未注册这些键的老工程保持原语义（命中绿/未命中灰/判 NG 红），不会被默认值改写。
    const QVariant showImageParam = node->getParam(QStringLiteral("resShowImage"));
    const bool drawResultGraphics = !showImageParam.isValid() || showImageParam.toBool();
    QColor configuredColor;
    const QVariant colorParam = node->getParam(QStringLiteral("resImageColor"));
    if (colorParam.isValid()) {
        const QColor color(colorParam.toString());
        if (color.isValid())
            configuredColor = color;
    }
    const QVariant opacityParam = node->getParam(QStringLiteral("resImageOpacity"));
    const int resultOpacity =
        opacityParam.isValid() ? qBound(0, opacityParam.toInt(), 100) : 100;
    const QVariant lineWidthParam = node->getParam(QStringLiteral("resImageLineWidth"));
    const int resultLineWidth =
        lineWidthParam.isValid() ? qBound(1, lineWidthParam.toInt(), 10) : 2;

    // VM：模块判 NG 但结果有效时，结果叠加层显示为红色（而不是命中绿）。
    // 只有「判 NG 且保留结果」的节点才染色；异常/执行失败没有结果可画。
    // 红色是语义色，不随「叠加颜色」配置改变；配置色只作用于 OK 结果。
    // 一次原子读整轮状态：分别读 moduleStatus（参数表）与 isResultNg() 会在
    // 「轮首刚复位 NG、参数表还是上一轮值」的窗口里拿到矛盾态，导致染色错一轮。
    const NodeBase::ResultState st = node->resultState();
    const bool ng = st.executed && !st.moduleOk && st.resultNg;
    const QColor resultColor =
        ng ? QColor(255, 60, 60) : (configuredColor.isValid() ? configuredColor : QColor(0, 255, 0));

    // B1 统一输出：结果不再挂在端口 1..N，而是与图像同一个对象（附加结果表）。
    // 仍然兼容尚未聚合的节点（例如条件判断的分支端口 / 老插件）：端口 1..N 也扫一遍。
    QVector<MeasureResult> results;
    QSharedPointer<DataObject> mainData = node->getOutputData(0);
    if (mainData)
        results = mainData->measureResults();
    // 聚合端口已把子输出里的结果全部并入主对象 ⇒ 再扫端口 1..N 只会把同一批结果画两遍。
    // 只有主对象没带结果时（分支类节点 / 尚未聚合的旧插件）才回退按端口扫。
    if (results.isEmpty()) {
        for (int p = 1; p < node->logicalOutputPortCount(); ++p) {
            auto data = node->getOutputData(p);
            if (!data) continue;
            for (const MeasureResult &mr : data->measureResults())
                results.append(mr);
        }
    }
    // 「显示结果图像」未勾选：不画结果图元（ROI/检测区域等编辑参照仍保留，
    // 它们属于搭建期的几何回显，不是模块结果）
    if (!drawResultGraphics)
        results.clear();

    for (const MeasureResult &mr : results) {
        // 未命中也画**灰框候选**（r19 批 A 第 3 步）：VM 的「总能找到，给出分数」——
        // 原先把 !mr.valid 整条丢掉，现场只剩红节点、没有框，无从判断差多少。
        const bool hit = mr.valid;

        OverlayShape s;
        // 命中绿框；未命中灰框候选（r19 批 A 第 3 步）
        s.color = hit ? resultColor : QColor(170, 170, 170);
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
            // 多目标（r19）：matchList 里除第一实例外，其余也画出来。
            // 尺寸按每目标自己的宽/高列表取（XY 尺度模式下两轴独立，P0-5）；
            // 老工程没有这两个列表时退回「按尺度比例缩放第一实例」的旧口径。
            const QString listText = node->getParam(QStringLiteral("matchList")).toString();
            const QStringList boxWidths = node->getParam(QStringLiteral("matchBoxWidths"))
                                              .toString()
                                              .split(QLatin1Char(','), Qt::SkipEmptyParts);
            const QStringList boxHeights = node->getParam(QStringLiteral("matchBoxHeights"))
                                               .toString()
                                               .split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (!listText.isEmpty()) {
                const QStringList items = listText.split(QLatin1Char(';'), Qt::SkipEmptyParts);
                for (int k = 1; k < items.size(); ++k) {
                    const QStringList f = items[k].split(QLatin1Char(','));
                    if (f.size() < 3)
                        continue;
                    OverlayShape extra;
                    extra.type = OverlayShape::Type::RotatedRect;
                    extra.p1 = QPointF(f[1].toDouble(), f[0].toDouble());
                    // 4..7 项是后加的（角度/尺度/匹配点）；老文件只有 3 项时沿用第一实例的尺寸
                    const double instAngle = f.size() >= 7 ? f[3].toDouble() : s.angleDeg;
                    const double instScale = f.size() >= 7 ? f[4].toDouble() : 0.0;
                    const double baseScale = mr.extraValues.size() >= 5 ? mr.extraValues[4] : 0.0;
                    const double ratio = (instScale > 0.0 && baseScale > 0.0) ? instScale / baseScale : 1.0;
                    const double w = k < boxWidths.size() ? boxWidths.at(k).toDouble() : 0.0;
                    const double h = k < boxHeights.size() ? boxHeights.at(k).toDouble() : 0.0;
                    extra.width = w > 0.0 ? w : s.width * ratio;
                    extra.height = h > 0.0 ? h : s.height * ratio;
                    extra.angleDeg = instAngle;
                    extra.color = s.color;
                    extra.text = QStringLiteral("%1").arg(f[2].toDouble(), 0, 'f', 2);
                    overlay.append(extra);
                    if (f.size() >= 7) {
                        OverlayShape anchor;
                        anchor.type = OverlayShape::Type::Point;
                        anchor.p1 = QPointF(f[6].toDouble(), f[5].toDouble());
                        anchor.color = s.color;
                        overlay.append(anchor);
                    }
                }
            }
            // 匹配点（模板左上角在场景中的位置）：VM 结果表里的“匹配点 X/Y”，画布上也标出来
            if (mr.extraValues.size() >= 11 && showLayer(node, "showMatchPoint")) {
                OverlayShape anchor;
                anchor.type = OverlayShape::Type::Point;
                anchor.p1 = QPointF(mr.extraValues[8], mr.extraValues[7]);
                anchor.color = s.color;
                overlay.append(anchor);
            }
        } else if (mr.type == QLatin1String("contour")) {
            // VM「是否获取轮廓」：模板轮廓特征点在每个命中位姿下的落点。
            // 结果对象里是 [x,y,x,y,...]；「显示特征点」层关闭时只保留匹配框。
            if (!showLayer(node, "showFeaturePoints"))
                continue;
            s.type = OverlayShape::Type::Points;
            for (int i = 0; i + 1 < mr.extraValues.size(); i += 2)
                s.points.append(QPointF(mr.extraValues[i], mr.extraValues[i + 1]));
            if (s.points.isEmpty())
                continue;
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
            // VM「结果显示 → 图像显示」：边缘点可单独显隐
            if (!showLayer(node, "showCaliperEdge"))
                continue;
            s.type = OverlayShape::Type::Points;
            for (int i = 0; i + 1 < mr.extraValues.size(); i += 2) {
                s.points.append(QPointF(mr.extraValues[i], mr.extraValues[i + 1]));
            }
        } else if (mr.type == QLatin1String("caliperPair")) {
            // 卡尺边对：extraValues=[col1,row1,col2,row2,width,...]。
            // VM 每个边对都标出自己的宽度（旧实现只在最后一个边对上写一次，
            // 多边对时其余边对的宽度无处可读）。
            if (!showLayer(node, "showCaliperPair"))
                continue;
            for (int i = 0; i + 4 < mr.extraValues.size(); i += 5) {
                OverlayShape link;
                link.type = OverlayShape::Type::Line;
                link.p1 = QPointF(mr.extraValues[i], mr.extraValues[i + 1]);
                link.p2 = QPointF(mr.extraValues[i + 2], mr.extraValues[i + 3]);
                link.color = resultColor;
                overlay.append(link);

                // 序号与「当前结果」的「边对0/边对1…」分组一一对应（0 起）
                OverlayShape widthText;
                widthText.type = OverlayShape::Type::Text;
                widthText.p1 = QPointF((link.p1.x() + link.p2.x()) * 0.5,
                                       (link.p1.y() + link.p2.y()) * 0.5);
                widthText.text = QStringLiteral("边对%1 宽=%2")
                                     .arg(i / 5)
                                     .arg(mr.extraValues[i + 4], 0, 'f', 2);
                widthText.color = resultColor;
                overlay.append(widthText);
            }
            continue;
        } else if (mr.type == QLatin1String("rect")) {
            // Blob 检测框：extraValues=[x,y,w,h]（图像坐标左上角 + 宽高），point1=质心
            if (!showLayer(node, "showBlobBox"))
                continue;
            if (mr.extraValues.size() >= 4 && mr.extraValues[2] > 0 && mr.extraValues[3] > 0) {
                s.type = OverlayShape::Type::RotatedRect;
                s.p1 = QPointF(mr.extraValues[0] + mr.extraValues[2] * 0.5,
                               mr.extraValues[1] + mr.extraValues[3] * 0.5);
                s.width = mr.extraValues[2];
                s.height = mr.extraValues[3];
                s.angleDeg = 0.0;
            } else {
                s.type = OverlayShape::Type::Point;
                s.p1 = mr.point1;
            }
        } else if (mr.type == QLatin1String("angle")) {
            // 角度测量：extraValues=[c1a,r1a,c1b,r1b,c2a,r2a,c2b,r2b,(col,row)交点]
            if (!showLayer(node, "showAngleLines"))
                continue;
            if (mr.extraValues.size() >= 8) {
                OverlayShape l1;
                l1.type = OverlayShape::Type::Line;
                l1.p1 = QPointF(mr.extraValues[0], mr.extraValues[1]);
                l1.p2 = QPointF(mr.extraValues[2], mr.extraValues[3]);
                l1.color = resultColor;
                overlay.append(l1);
                OverlayShape l2;
                l2.type = OverlayShape::Type::Line;
                l2.p1 = QPointF(mr.extraValues[4], mr.extraValues[5]);
                l2.p2 = QPointF(mr.extraValues[6], mr.extraValues[7]);
                l2.color = QColor(0, 200, 255);
                overlay.append(l2);
                if (mr.extraValues.size() >= 10) {
                    OverlayShape ix;
                    ix.type = OverlayShape::Type::Point;
                    ix.p1 = QPointF(mr.extraValues[8], mr.extraValues[9]);
                    ix.color = QColor(255, 220, 0);
                    overlay.append(ix);
                }
            }
            s.type = OverlayShape::Type::Text;
            s.p1 = QPointF((mr.extraValues.size() >= 8
                                ? (mr.extraValues[0] + mr.extraValues[2]) * 0.5
                                : mr.point1.x()),
                           (mr.extraValues.size() >= 8
                                ? (mr.extraValues[1] + mr.extraValues[3]) * 0.5
                                : mr.point1.y()));
            s.text = QStringLiteral("夹角=%1°").arg(mr.value, 0, 'f', 2);
        } else {
            s.type = OverlayShape::Type::Point;
            s.p1 = mr.point1;
            s.text = QStringLiteral("%1=%2").arg(mr.valueName).arg(mr.value, 0, 'f', 3);
        }
        overlay.append(s);
    }

    // 结果图元统一套用「图像显示」的线宽/透明度；颜色按上面 resultColor 逐图元定
    // （语义色——未命中灰、角度副线青、交点黄、缺陷红——不被配置色覆盖）
    const int resultShapeCount = int(overlay.size());
    for (int i = 0; i < resultShapeCount; ++i) {
        overlay[i].lineWidth = resultLineWidth;
        overlay[i].opacity = resultOpacity;
    }

    // ── VM「结果显示 → 文本显示」（2026-11-06）──
    // 只有显式配置过（保存了 resShowText 或 resTextContent）才叠加：
    // 老工程没有这些键，不能平白多出一行文字。
    const QVariant showTextParam = node->getParam(QStringLiteral("resShowText"));
    const QVariant contentParam = node->getParam(QStringLiteral("resTextContent"));
    const bool textConfigured = showTextParam.isValid() || contentParam.isValid();
    if (drawResultGraphics && textConfigured
        && (!showTextParam.isValid() || showTextParam.toBool())) {
        const QString content = contentParam.isValid()
                                    ? contentParam.toString()
                                    : QStringLiteral("{module}: {status}  {elapsed}ms");
        const QString resolved = resolveResultText(node, content, vars);
        if (!resolved.trimmed().isEmpty()) {
            OverlayShape text;
            text.type = OverlayShape::Type::Text;
            text.text = resolved;
            const QVariant pos = node->getParam(QStringLiteral("resTextPosition"));
            text.textPosition = qBound(0, pos.isValid() ? pos.toInt() : 0, 3);
            const QVariant textColor = node->getParam(QStringLiteral("resTextColor"));
            if (textColor.isValid()) {
                const QColor color(textColor.toString());
                if (color.isValid())
                    text.color = color;
            } else {
                text.color = QColor(255, 255, 255);   // VM 默认白字
            }
            const QVariant size = node->getParam(QStringLiteral("resTextFontSize"));
            text.fontSize = size.isValid() ? qBound(8, size.toInt(), 48) : 14;
            const QVariant alpha = node->getParam(QStringLiteral("resTextOpacity"));
            text.opacity = alpha.isValid() ? qBound(0, alpha.toInt(), 100) : 100;
            overlay.append(text);
        }
    }

    // VM「结果显示 → 图像显示」各层可单独显隐（截图 4 的眼睛开关）

    // 模板框（只读绿框）：画布上编辑检测区域时，模板在哪必须仍然看得见。
    // 只给"同时有检测区域"的工具画，避免与别的算子的几何 ROI 重复叠一层。
    if (HalconNode *hn = qobject_cast<HalconNode *>(node)) {
        const bool showRegion = showLayer(hn, "showSearchRegion");
        if (showRegion && hn->hasParam(QStringLiteral("searchRegionWidth"))
            && hn->geometryRoiType() != RoiType::None) {
            const RoiShape g = hn->geometryRoi();
            OverlayShape box;
            box.color = QColor(0, 220, 120);
            if (g.type == RoiType::RotatedRect && g.width > 1.0 && g.height > 1.0) {
                box.type = OverlayShape::Type::RotatedRect;
                box.p1 = g.p1;
                box.width = g.width;
                box.height = g.height;
                box.angleDeg = g.angleDeg;
                overlay.append(box);
            } else if (g.type == RoiType::Rect) {
                const QRectF r = QRectF(g.p1, g.p2).normalized();
                if (r.width() > 1.0 && r.height() > 1.0) {
                    box.type = OverlayShape::Type::RotatedRect;
                    box.p1 = r.center();
                    box.width = r.width();
                    box.height = r.height();
                    box.angleDeg = 0.0;
                    overlay.append(box);
                }
            }
        }
    }

    // 检测区域（搜索区域）：模板匹配类工具必须能一眼看到自己的搜索范围。
    // VM 把这块放在图像区配置；我们同样画在主画布上（青框），可拖与否由编辑对象开关决定。
    if (HalconNode *hn = qobject_cast<HalconNode *>(node)) {
        if (showLayer(hn, "showSearchRegion")
            && hn->hasParam(QStringLiteral("searchRegionWidth"))
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

void ImageDisplayController::forgetNode(NodeBase *node)
{
    if (!node)
        return;
    // 显式显示源指向被删节点时必须**删键**而不是置空：resolveDisplayNode 对“键存在”
    // 直接返回其值，置空会让该场景此后一直解析到 nullptr、选中新节点也不再显示。
    for (auto it = m_selectedOutputNodes.begin(); it != m_selectedOutputNodes.end();) {
        if (it.value() == node)
            it = m_selectedOutputNodes.erase(it);
        else
            ++it;
    }

    const bool wasPicking = (m_roiPickNode == node);
    if (wasPicking)
        m_roiPickNode = nullptr;
    if (m_geometryRoiNode == node)
        m_geometryRoiNode = nullptr;
    if (m_searchRegionNode == node)
        m_searchRegionNode = nullptr;
    if (m_maskRegionNode == node)
        m_maskRegionNode = nullptr;
    if (m_coordinatePickNode == node)
        m_coordinatePickNode = nullptr;
    const bool wasMaskEditing = (m_maskEditNode == node);
    if (wasMaskEditing)
        stopMaskEdit();

    if (!m_imageView)
        return;
    if (wasPicking)
        m_imageView->setRoiEditable(false);
    if (wasMaskEditing)
        m_imageView->setMaskImage(QImage());
    // 当前画布上的 ROI 正属于被删节点：收起句柄与模板特征点，避免继续对着悬垂数据编辑
    if (!m_geometryRoiNode && !m_searchRegionNode && !m_maskRegionNode) {
        m_imageView->setRoiHandleEditEnabled(false);
        m_imageView->setFeaturePoints({});
        m_imageView->clearRoi();
    }
}

bool ImageDisplayController::showNodeGeometry(NodeBase *node, bool preferSearchRegion)
{
    if (!m_imageView)
        return false;
    stopMaskEdit();   // 切换选中对象/几何编辑时结束掩膜涂抹，避免写到上一个节点的掩膜
    HalconNode *hn = qobject_cast<HalconNode *>(node);
    if (!hn || hn->geometryRoiType() == RoiType::None) {
        m_geometryRoiNode = nullptr;
        m_searchRegionNode = nullptr;
        m_coordinatePickNode = nullptr;
        m_imageView->clearRoi();
        m_imageView->setFeaturePoints({});
        m_imageView->setRoiHandleEditEnabled(false);
        return false;
    }

    // VM 口径：检测区域（搜索区域）是画布上的主编辑对象，模板框只作只读参照。
    // 只有真的设了检测区域（宽高 > 1）才走这条；没设=整图搜索，退回编辑模板框。
    if (preferSearchRegion && hasSearchRegion(node)) {
        const double col = hn->getParam(QStringLiteral("searchRegionCol")).toDouble();
        const double row = hn->getParam(QStringLiteral("searchRegionRow")).toDouble();
        const double w = hn->getParam(QStringLiteral("searchRegionWidth")).toDouble();
        const double h = hn->getParam(QStringLiteral("searchRegionHeight")).toDouble();
        if (w > 1.0 && h > 1.0) {
            m_searchRegionNode = node;
            m_maskRegionNode = nullptr;
            m_geometryRoiNode = nullptr;
            m_roiPickNode = nullptr;
            m_coordinatePickNode = nullptr;
            RoiShape s;
            s.type = RoiType::Rect;
            s.p1 = QPointF(col, row);
            s.p2 = QPointF(col + w, row + h);
            m_imageView->setRoiEditable(false);
            m_imageView->setRoiColor(QColor(0, 200, 255));
            m_imageView->setRoiShape(s);
            m_imageView->setFeaturePoints(hn->editorFeaturePoints());
            m_imageView->setRoiHandleEditEnabled(true);
            return true;
        }
    }
    m_geometryRoiNode = node;
    m_searchRegionNode = nullptr;
    m_maskRegionNode = nullptr;
    m_roiPickNode = nullptr;                 // 与"取点"互斥
    m_coordinatePickNode = nullptr;
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
    m_coordinatePickNode = nullptr;
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
    stopMaskEdit();
    m_roiPickNode = node;
    m_geometryRoiNode = nullptr;             // 取点期间不再同时编辑几何
    m_searchRegionNode = nullptr;
    m_maskRegionNode = nullptr;
    m_coordinatePickNode = nullptr;
    if (qobject_cast<FindCircleNode *>(node)) {
        m_imageView->setRoiEditable(true, RoiType::Circle);
    } else {
        m_imageView->setRoiEditable(true, RoiType::Line);
    }
    return true;
}

bool ImageDisplayController::startSearchRegionPick(NodeBase *node, bool polygon)
{
    if (!m_imageView || !hasSearchRegion(node))
        return false;
    // 与「取点」「几何编辑」互斥：三者只允许一个在画布上活跃，否则句柄互相抢点击。
    stopMaskEdit();
    m_roiPickNode = nullptr;
    m_geometryRoiNode = nullptr;
    m_maskRegionNode = nullptr;
    m_searchRegionNode = node;
    m_coordinatePickNode = nullptr;
    m_imageView->setRoiColor(QColor(0, 200, 255));   // 青色 = 检测区域（绿框是模板）
    m_imageView->clearRoi();
    m_imageView->setRoiEditable(true, polygon ? RoiType::Polygon : RoiType::Rect);
    return true;
}

bool ImageDisplayController::startMaskRegionPick(NodeBase *node)
{
    if (!m_imageView || !node)
        return false;
    HalconNode *hn = qobject_cast<HalconNode *>(node);
    if (!hn)
        return false;
    bool hasParam = false;
    for (const ParamSpec &s : hn->paramSpecs()) {
        if (s.name == QLatin1String("maskRegionPolygon")) {
            hasParam = true;
            break;
        }
    }
    if (!hasParam)
        return false;
    stopMaskEdit();
    m_roiPickNode = nullptr;
    m_geometryRoiNode = nullptr;
    m_searchRegionNode = nullptr;
    m_coordinatePickNode = nullptr;
    m_maskRegionNode = node;
    m_imageView->setRoiColor(QColor(230, 80, 80));   // 红色 = 屏蔽区
    m_imageView->clearRoi();
    m_imageView->setRoiEditable(true, RoiType::Polygon);
    return true;
}

bool ImageDisplayController::startTemplateRoiPick(NodeBase *node, bool rotatedRect)
{
    if (!m_imageView)
        return false;
    HalconNode *hn = qobject_cast<HalconNode *>(node);
    if (!hn || hn->geometryRoiType() == RoiType::None)
        return false;
    stopMaskEdit();
    m_roiPickNode = nullptr;
    m_searchRegionNode = nullptr;
    m_maskRegionNode = nullptr;
    m_geometryRoiNode = node;
    m_coordinatePickNode = nullptr;
    m_imageView->setRoiColor(QColor(0, 200, 80));    // 绿色 = 模板框
    m_imageView->clearRoi();
    m_imageView->setRoiEditable(true, rotatedRect ? RoiType::RotatedRect : RoiType::Rect);
    return true;
}

bool ImageDisplayController::startCoordinatePick(NodeBase *node, int field)
{
    if (!m_imageView || !hasSearchRegion(node))
        return false;
    stopMaskEdit();
    m_roiPickNode = nullptr;
    m_geometryRoiNode = nullptr;
    m_maskRegionNode = nullptr;
    m_searchRegionNode = node;
    m_coordinatePickNode = node;
    m_coordinatePickField = qBound(0, field, 3);
    m_imageView->setRoiColor(QColor(0, 200, 255));
    m_imageView->clearRoi();
    m_imageView->setRoiEditable(true, RoiType::Point);
    return true;
}

bool ImageDisplayController::startGeometryPick(NodeBase *node)
{
    if (!m_imageView || !node)
        return false;
    HalconNode *hn = qobject_cast<HalconNode *>(node);
    if (!hn || hn->geometryRoiType() == RoiType::None)
        return false;
    stopMaskEdit();
    m_roiPickNode = nullptr;
    m_searchRegionNode = nullptr;
    m_maskRegionNode = nullptr;
    m_coordinatePickNode = nullptr;
    m_geometryRoiNode = node;
    // ROI 坐标基于输入图像；先切到输入图，避免"输出图尺寸不同导致框错位"。
    showInputOnPrimary(node);
    m_imageView->setMaskEditable(false);
    m_imageView->setRoiColor(QColor(0, 200, 80));
    m_imageView->clearRoi();
    m_imageView->setRoiEditable(true, hn->geometryRoiType());
    return true;
}

bool ImageDisplayController::startMaskEdit(NodeBase *node)
{
    if (!m_imageView || !node)
        return false;
    HalconNode *hn = qobject_cast<HalconNode *>(node);
    if (!hn || !hn->supportsMaskEdit())
        return false;
    m_roiPickNode = nullptr;
    m_geometryRoiNode = nullptr;
    m_searchRegionNode = nullptr;
    m_maskRegionNode = nullptr;
    m_coordinatePickNode = nullptr;
    m_maskEditNode = node;
    showInputOnPrimary(node);   // 掩膜与输入图同尺寸、同坐标系
    m_imageView->setRoiEditable(false);
    m_imageView->clearRoi();
    m_imageView->setMaskImage(hn->editMask());
    m_imageView->ensureMaskForCurrentImage();
    m_imageView->setMaskEditable(true);
    return true;
}

void ImageDisplayController::stopMaskEdit()
{
    if (m_imageView)
        m_imageView->setMaskEditable(false);
    m_maskEditNode = nullptr;
}

bool ImageDisplayController::writeMaskBack()
{
    if (!m_maskEditNode || !m_imageView)
        return false;
    HalconNode *hn = qobject_cast<HalconNode *>(m_maskEditNode);
    if (!hn)
        return false;
    hn->setEditMask(m_imageView->maskImage());
    return true;
}

NodeBase *ImageDisplayController::writeRoiToNode(const RoiShape &shape)
{
    // 检测区域单字段取点（VM 每个输入框右侧的取点按钮）：单击图像 → 按字段写回。
    if (m_coordinatePickNode && shape.type == RoiType::Point) {
        NodeBase *node = m_coordinatePickNode;
        m_coordinatePickNode = nullptr;
        HalconNode *hn = qobject_cast<HalconNode *>(node);
        if (!hn)
            return nullptr;
        int col = hn->getParam(QStringLiteral("searchRegionCol")).toInt();
        int row = hn->getParam(QStringLiteral("searchRegionRow")).toInt();
        int w = hn->getParam(QStringLiteral("searchRegionWidth")).toInt();
        int h = hn->getParam(QStringLiteral("searchRegionHeight")).toInt();
        const int x = int(shape.p1.x() + 0.5);
        const int y = int(shape.p1.y() + 0.5);
        switch (m_coordinatePickField) {
        case 0:   // 左列
            col = x;
            break;
        case 1:   // 上行
            row = y;
            break;
        case 2:   // 宽度：点在左边时把左列一起挪过去，保证宽高为正
            if (x < col) {
                w = col - x;
                col = x;
            } else {
                w = x - col;
            }
            w = qMax(1, w);
            break;
        default:  // 高度
            if (y < row) {
                h = row - y;
                row = y;
            } else {
                h = y - row;
            }
            h = qMax(1, h);
            break;
        }
        if (FlowScene *fs = hn->flowSceneRef())
            fs->recordUndo();
        hn->setParam(QStringLiteral("searchRegionCol"), col);
        hn->setParam(QStringLiteral("searchRegionRow"), row);
        hn->setParam(QStringLiteral("searchRegionWidth"), w);
        hn->setParam(QStringLiteral("searchRegionHeight"), h);
        if (m_imageView) {
            m_imageView->setRoiEditable(false);
            if (w > 1 && h > 1) {
                RoiShape rect;
                rect.type = RoiType::Rect;
                rect.p1 = QPointF(col, row);
                rect.p2 = QPointF(col + w, row + h);
                m_imageView->setRoiShape(rect);
                m_imageView->setRoiHandleEditEnabled(true);
            } else {
                m_imageView->clearRoi();
                m_imageView->setRoiHandleEditEnabled(false);
            }
        }
        return node;
    }

    // P1-8：主画布绘制 ROI 级屏蔽区——每次画一块，追加到 maskRegionPolygon（';' 分隔）。
    if (m_maskRegionNode && shape.type == RoiType::Polygon) {
        HalconNode *hn = qobject_cast<HalconNode *>(m_maskRegionNode);
        if (hn && shape.points.size() >= 3) {
            if (FlowScene *fs = hn->flowSceneRef())
                fs->recordUndo();
            QStringList pts;
            pts.reserve(shape.points.size());
            for (const QPointF &p : shape.points)
                pts << QStringLiteral("%1,%2").arg(int(p.x() + 0.5)).arg(int(p.y() + 0.5));
            QString all =
                hn->getParam(QStringLiteral("maskRegionPolygon")).toString().trimmed();
            if (!all.isEmpty())
                all += QLatin1Char(';');
            all += pts.join(QLatin1Char(' '));
            hn->setParam(QStringLiteral("maskRegionPolygon"), all);
            if (m_imageView) {
                m_imageView->setRoiEditable(false);
                m_imageView->setRoiShape(shape);
                m_imageView->setRoiHandleEditEnabled(false);
            }
        }
        return hn;
    }

    // B3：主画布"搜索区域"编辑——多边形与矩形都写回 searchRegion*（只写参数，
    // 不打断执行线程，下一轮生效）。VM 的「检测区域」是多边形优先于矩形。
    if (m_searchRegionNode && shape.type == RoiType::Polygon) {
        HalconNode *hn = qobject_cast<HalconNode *>(m_searchRegionNode);
        if (hn && shape.points.size() >= 3) {
            if (FlowScene *fs = hn->flowSceneRef())
                fs->recordUndo();
            QStringList pts;
            pts.reserve(shape.points.size());
            for (const QPointF &p : shape.points)
                pts << QStringLiteral("%1,%2").arg(int(p.x() + 0.5)).arg(int(p.y() + 0.5));
            hn->setParam(QStringLiteral("searchRegionPolygon"), pts.join(QLatin1Char(' ')));
            hn->setParam(QStringLiteral("searchRegionWidth"), 0);
            hn->setParam(QStringLiteral("searchRegionHeight"), 0);
            if (m_imageView) {
                m_imageView->setRoiEditable(false);
                m_imageView->setRoiShape(shape);
                m_imageView->setRoiHandleEditEnabled(false);   // 顶点不拖：重画走「绘制」
            }
        }
        return hn;
    }
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
                // 画矩形 = 放弃旧多边形：留着它会让"多边形优先"把刚画的矩形完全遮掉。
                hn->setParam(QStringLiteral("searchRegionPolygon"), QString());
                if (m_imageView) {
                    m_imageView->setRoiShape(shape);
                    m_imageView->setRoiEditable(false);
                    m_imageView->setRoiHandleEditEnabled(true);   // 画完继续拖角点微调
                }
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
            if (m_imageView) {
                m_imageView->setRoiEditable(false);
                m_imageView->setRoiShape(hn->geometryRoi());
                m_imageView->setRoiHandleEditEnabled(true);
            }
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
