#include "OpencvTemplateMatchNode.h"
#include "NodeResultFields.h"
#include "OpencvUtil.h"
#include "ModelCache.h"
#include "DataObject.h"
#include "AppLog.h"
#include "FlowScene.h"
#include "ProjectPaths.h"   // 179 行：文件路径参数执行侧锚定方案目录
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QWidget>
#include <QFileInfo>
#include <QDir>
#include <QVector>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

QJsonObject shapeToJsonObj(const RoiShape &shape)
{
    QJsonObject s;
    switch (shape.type) {
    case RoiType::Rect:
        s[QStringLiteral("type")] = QStringLiteral("rect");
        s[QStringLiteral("x1")] = shape.p1.x();
        s[QStringLiteral("y1")] = shape.p1.y();
        s[QStringLiteral("x2")] = shape.p2.x();
        s[QStringLiteral("y2")] = shape.p2.y();
        break;
    case RoiType::RotatedRect:
        s[QStringLiteral("type")] = QStringLiteral("rot");
        s[QStringLiteral("cx")] = shape.p1.x();
        s[QStringLiteral("cy")] = shape.p1.y();
        s[QStringLiteral("w")] = shape.width;
        s[QStringLiteral("h")] = shape.height;
        s[QStringLiteral("angle")] = shape.angleDeg;
        break;
    case RoiType::Circle:
        s[QStringLiteral("type")] = QStringLiteral("circle");
        s[QStringLiteral("cx")] = shape.p1.x();
        s[QStringLiteral("cy")] = shape.p1.y();
        s[QStringLiteral("ex")] = shape.p2.x();
        s[QStringLiteral("ey")] = shape.p2.y();
        break;
    case RoiType::Polygon: {
        s[QStringLiteral("type")] = QStringLiteral("poly");
        QJsonArray pts;
        for (const QPointF &p : shape.points) {
            QJsonArray xy;
            xy.append(p.x());
            xy.append(p.y());
            pts.append(xy);
        }
        s[QStringLiteral("points")] = pts;
        break;
    }
    default:
        s[QStringLiteral("type")] = QStringLiteral("none");
        break;
    }
    return s;
}

RoiShape shapeFromJsonObj(const QJsonObject &s)
{
    RoiShape shape;
    const QString type = s.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("rect")) {
        shape.type = RoiType::Rect;
        shape.p1 = QPointF(s.value(QStringLiteral("x1")).toDouble(),
                           s.value(QStringLiteral("y1")).toDouble());
        shape.p2 = QPointF(s.value(QStringLiteral("x2")).toDouble(),
                           s.value(QStringLiteral("y2")).toDouble());
    } else if (type == QLatin1String("rot")) {
        shape.type = RoiType::RotatedRect;
        shape.p1 = QPointF(s.value(QStringLiteral("cx")).toDouble(),
                           s.value(QStringLiteral("cy")).toDouble());
        shape.width = s.value(QStringLiteral("w")).toDouble();
        shape.height = s.value(QStringLiteral("h")).toDouble();
        shape.angleDeg = s.value(QStringLiteral("angle")).toDouble();
    } else if (type == QLatin1String("circle")) {
        shape.type = RoiType::Circle;
        shape.p1 = QPointF(s.value(QStringLiteral("cx")).toDouble(),
                           s.value(QStringLiteral("cy")).toDouble());
        shape.p2 = QPointF(s.value(QStringLiteral("ex")).toDouble(),
                           s.value(QStringLiteral("ey")).toDouble());
    } else if (type == QLatin1String("poly")) {
        shape.type = RoiType::Polygon;
        for (const QJsonValue &v : s.value(QStringLiteral("points")).toArray()) {
            const QJsonArray xy = v.toArray();
            if (xy.size() == 2)
                shape.points.append(QPointF(xy[0].toDouble(), xy[1].toDouble()));
        }
    }
    return shape;
}

}   // namespace

QJsonObject TemplateEntry::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("image")] = imagePath;
    const QVector<RoiShape> list = allShapes();
    QJsonArray arr;
    for (const RoiShape &s : list)
        arr.append(shapeToJsonObj(s));
    obj[QStringLiteral("shapes")] = arr;
    // 老读取方只看 shape；保持它 = 第一块形状
    obj[QStringLiteral("shape")] =
        arr.isEmpty() ? QJsonObject{{QStringLiteral("type"), QStringLiteral("none")}}
                      : arr.first().toObject();
    QJsonArray exArr;
    for (const RoiShape &s : excludes)
        exArr.append(shapeToJsonObj(s));
    obj[QStringLiteral("excludes")] = exArr;
    obj[QStringLiteral("contrastThreshold")] = contrastThreshold;
    obj[QStringLiteral("autoThreshold")] = autoThreshold;
    obj[QStringLiteral("featureScale")] = featureScale;
    obj[QStringLiteral("speedScale")] = speedScale;
    obj[QStringLiteral("minChainLength")] = minChainLength;
    obj[QStringLiteral("chainMode")] = chainModeAuto;
    obj[QStringLiteral("anchorX")] = anchorDx;
    obj[QStringLiteral("anchorY")] = anchorDy;
    obj[QStringLiteral("anchorLocked")] = anchorLocked;
    return obj;
}

TemplateEntry TemplateEntry::fromJson(const QJsonObject &obj)
{
    TemplateEntry e;
    e.name = obj.value(QStringLiteral("name")).toString();
    e.imagePath = obj.value(QStringLiteral("image")).toString();
    // 新格式：shapes 数组；老格式：单个 shape
    for (const QJsonValue &v : obj.value(QStringLiteral("shapes")).toArray())
        if (v.isObject())
            e.shapes.append(shapeFromJsonObj(v.toObject()));
    if (e.shapes.isEmpty()) {
        const RoiShape s = shapeFromJsonObj(obj.value(QStringLiteral("shape")).toObject());
        if (s.type != RoiType::None)
            e.shapes.append(s);
    }
    if (!e.shapes.isEmpty())
        e.shape = e.shapes.first();
    for (const QJsonValue &v : obj.value(QStringLiteral("excludes")).toArray())
        if (v.isObject())
            e.excludes.append(shapeFromJsonObj(v.toObject()));
    e.autoThreshold = obj.value(QStringLiteral("autoThreshold")).toBool(false);
    // VM 口径字段；老 v1 JSON 只有 edgeThreshold/aperture/coarseLongSide，做一次迁移。
    // 注意：老 edgeThreshold 是 Sobel 幅值，要 /4 换成灰度差才不改变老工程行为；
    // 老 aperture 是 Sobel 核、老 coarseLongSide 是像素长边，与新量纲不同，一律按"不过滤/不稀疏"落地。
    if (obj.contains(QStringLiteral("contrastThreshold")))
        e.contrastThreshold = qBound(1, obj.value(QStringLiteral("contrastThreshold")).toInt(5), 255);
    else
        e.contrastThreshold =
            qBound(1, int(obj.value(QStringLiteral("edgeThreshold")).toDouble(20.0) * 0.25 + 0.5), 255);
    if (obj.contains(QStringLiteral("featureScale")))
        e.featureScale = qBound(1, obj.value(QStringLiteral("featureScale")).toInt(1), 20);
    else
        e.featureScale = 1;                 // 老工程不稀疏（=原行为）
    if (obj.contains(QStringLiteral("speedScale")))
        e.speedScale = qBound(1, obj.value(QStringLiteral("speedScale")).toInt(3), 20);
    e.minChainLength = qBound(1, obj.value(QStringLiteral("minChainLength")).toInt(1), 200);
    e.chainModeAuto = obj.value(QStringLiteral("chainMode")).toBool(false);
    e.anchorDx = obj.value(QStringLiteral("anchorX")).toDouble(0.0);
    e.anchorDy = obj.value(QStringLiteral("anchorY")).toDouble(0.0);
    e.anchorLocked = obj.value(QStringLiteral("anchorLocked")).toBool(false);
    return e;
}

namespace {

/// GOC 强梯度阈值的默认值（=原写死常量）。运行期以对比度阈值为准，
/// 自动模式按模板梯度分位数覆盖；可视化与打分都必须走 effectiveEdgeThreshold()。
constexpr double kGocEdgeThreshold = 20.0;

/// VM「最小链长」+「特征尺度」的等效实现：
///  ① 只保留连通长度 ≥ minChain 的特征链（短链是噪声/杂点，建模时丢弃）；
///  ② 按 step×step 网格稀疏化（VM：特征尺度越大特征点越稀疏、抗形变越强）。
/// 接受 CV_32F(1/0) 或 CV_8U，原地改写。
void refineFeatureMask(cv::Mat &bin, int minChain, int featureScale)
{
    if (bin.empty())
        return;
    cv::Mat m8;
    if (bin.type() == CV_32F)
        bin.convertTo(m8, CV_8U, 255.0);
    else {
        m8 = bin.clone();
        cv::threshold(m8, m8, 0, 255, cv::THRESH_BINARY);
    }

    if (minChain > 1) {
        cv::Mat labels, stats, centroids;
        const int n = cv::connectedComponentsWithStats(m8, labels, stats, centroids, 8, CV_32S);
        for (int i = 1; i < n; ++i) {
            if (stats.at<int>(i, cv::CC_STAT_AREA) < minChain)
                m8.setTo(cv::Scalar(0), labels == i);
        }
    }

    if (featureScale > 1) {
        for (int y = 0; y < m8.rows; y += featureScale) {
            for (int x = 0; x < m8.cols; x += featureScale) {
                bool seen = false;
                for (int j = 0; j < featureScale && y + j < m8.rows; ++j) {
                    uchar *row = m8.ptr<uchar>(y + j);
                    for (int i = 0; i < featureScale && x + i < m8.cols; ++i) {
                        if (row[x + i]) {
                            if (seen)
                                row[x + i] = 0;
                            else
                                seen = true;
                        }
                    }
                }
            }
        }
    }

    if (bin.type() == CV_32F)
        m8.convertTo(bin, CV_32F, 1.0 / 255.0);
    else
        bin = m8;
}

cv::Mat warpTemplate(const cv::Mat &tmpl, double angleDeg, double scale, cv::Point2f *centerOut)
{
    if (qAbs(angleDeg) < 1e-6 && qAbs(scale - 1.0) < 1e-6) {
        if (centerOut)
            *centerOut = cv::Point2f(tmpl.cols * 0.5f, tmpl.rows * 0.5f);
        return tmpl;
    }
    const cv::Point2f center(tmpl.cols * 0.5f, tmpl.rows * 0.5f);
    cv::Mat M = cv::getRotationMatrix2D(center, angleDeg, scale);
    const double cosA = std::abs(M.at<double>(0, 0));
    const double sinA = std::abs(M.at<double>(0, 1));
    const int nw = std::max(1, int(std::ceil(tmpl.rows * sinA + tmpl.cols * cosA)));
    const int nh = std::max(1, int(std::ceil(tmpl.rows * cosA + tmpl.cols * sinA)));
    M.at<double>(0, 2) += nw * 0.5 - center.x;
    M.at<double>(1, 2) += nh * 0.5 - center.y;
    cv::Mat out;
    cv::warpAffine(tmpl, out, M, cv::Size(nw, nh), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    if (centerOut)
        *centerOut = cv::Point2f(nw * 0.5f, nh * 0.5f);
    return out;
}
namespace {

/// 旋转/缩放后的模板缓存（模板不变时跨帧复用；LRU 32 条）。
/// 连 centerInWarp 一起缓存——它由 warpTemplate 按旋转后的包围盒算出，不能事后猜。
std::pair<cv::Mat, cv::Point2f> warpTemplateCached(const cv::Mat &tmpl, const QString &keyBase,
                                                   double angle, double scale)
{
        // 恒等快路径（r19）：角度 0、尺度 1 时直接用原模板 —— warpAffine 即便
        // 恒等变换也会重采样，导致自匹配到不了 1.0、位置偏约 2px。
        if (qAbs(angle) < 1e-6 && qAbs(scale - 1.0) < 1e-6) {
            return {tmpl, cv::Point2f(float(tmpl.cols) * 0.5f, float(tmpl.rows) * 0.5f)};
        }

    static QMutex mu;
    static QHash<QString, std::pair<cv::Mat, cv::Point2f>> cache;
    static QStringList order;
    const QString key = keyBase + QStringLiteral("|%1|%2")
                                       .arg(angle, 0, 'f', 3)
                                       .arg(scale, 0, 'f', 3);
    {
        QMutexLocker<QMutex> locker(&mu);
        auto it = cache.constFind(key);
        if (it != cache.constEnd()) {
            order.removeAll(key);
            order.append(key);
            return it.value();
        }
    }
    cv::Point2f c;
    cv::Mat warped = warpTemplate(tmpl, angle, scale, &c);
    {
        QMutexLocker<QMutex> locker(&mu);
        cache.insert(key, {warped, c});
        order.append(key);
        while (cache.size() > 32 && !order.isEmpty()) {
            const QString victim = order.takeFirst();
            if (victim != key)
                cache.remove(victim);
        }
    }
    return {warped, c};
}

}   // namespace


void collectSearchValues(double mn, double mx, double step, int maxCount, QVector<double> &out)
{
    if (mx < mn)
        std::swap(mn, mx);
    if (step <= 1e-9 || qAbs(mx - mn) < 1e-9) {
        out.append((mn + mx) * 0.5);
        return;
    }
    int n = int(std::floor((mx - mn) / step + 0.5)) + 1;
    if (n < 1)
        n = 1;
    if (n > maxCount) {
        step = (mx - mn) / double(maxCount - 1);
        n = maxCount;
    }
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.append(mn + step * i);
}

} // namespace

OpencvTemplateMatchNode::OpencvTemplateMatchNode(QObject *parent) : HalconNode(parent, false)
{
    setName(QStringLiteral("模板匹配"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvTemplateMatchNode::init()
{
    HalconNode::init();   // 先建「输入图像」（必须是 0 号），再加 ROI 掩膜输入
    // VM 基本参数页的「ROI区域类型=图像类型」：第二个图像输入当 ROI 掩膜
    // （白色=允许匹配，黑色=排除）。图形类型仍走 searchRegion* 参数。
    addInputPort(QStringLiteral("ROI掩膜"));
    addOutputPort(QStringLiteral("匹配结果"), PortDataType::Measure);
    registerParams({
        makeFilePathParam(QStringLiteral("templatePath"), QString(),
                          QStringLiteral("模板图像文件 (png/bmp/jpg)")),
        makeBoolParam(QStringLiteral("trainFromImage"), false,
                      QStringLiteral("从图像 ROI 训练并保存模板（训练落盘后自动取消勾选）")),
        makeIntParam(QStringLiteral("method"), 1, 0, 2,
                     QStringLiteral("匹配方法：0=归一化平方差，1=归一化相关系数，2=边缘梯度")),
        makeIntParam(QStringLiteral("polarity"), 1, 0, 1,
                     QStringLiteral("是否考虑极性：1=考虑（默认），0=忽略（亮标/暗标反色也能找到）")),
        makeIntParam(QStringLiteral("maxMatches"), 1, 1, 16,
                     QStringLiteral("最多匹配个数（1=只输出最优；多件场景调大）")),
        makeBoolParam(QStringLiteral("matchAllTemplates"), false,
                      QStringLiteral("多模板同时匹配（模板列表逐个跑，取最高分）")),
        makeIntParam(QStringLiteral("minChainLength"), 1, 1, 200,
                     QStringLiteral("最小链长（特征链短于此不保留）")),
        makeBoolParam(QStringLiteral("chainMode"), false,
                      QStringLiteral("链长模式：自动（关=用上面的最小链长）")),
        makeIntParam(QStringLiteral("roiType"), 0, 0, 1,
                     QStringLiteral("ROI区域类型：0=图形（searchRegion*），1=图像（第2个输入当掩膜）")),
        makeBoolParam(QStringLiteral("outputMask"), false,
                      QStringLiteral("输出掩膜（把最终搜索区域作为具名子结果输出）")),
        makeIntParam(QStringLiteral("roiSource"), 0, 0, 1,
                     QStringLiteral("ROI创建：0=绘制（searchRegion*），1=继承上游命中框")),
        makeIntParam(QStringLiteral("scaleMode"), 0, 0, 1,
                     QStringLiteral("尺度模式：0=手动（scaleMin/Max/Step），1=自动按模板占比估范围")),
        // VM「结果显示 → 图像显示」：每一层可单独显隐（截图 4 的四个眼睛开关）
        makeBoolParam(QStringLiteral("showSearchRegion"), true, QStringLiteral("显示检测区域")),
        makeBoolParam(QStringLiteral("showMatchResult"), true, QStringLiteral("显示匹配结果")),
        makeBoolParam(QStringLiteral("showMatchPoint"), true, QStringLiteral("显示匹配点")),
        makeBoolParam(QStringLiteral("showFeaturePoints"), true, QStringLiteral("显示特征点")),
        makeDoubleParam(QStringLiteral("minScore"), 0.6, 0.0, 1.0,
                        QStringLiteral("最低匹配度")),
        // VM「配置参数」对齐（2026-11-02）：把原先写死的边缘参数放出来。
        // 默认值 = 原常量，老方案行为逐字节不变。
        makeIntParam(QStringLiteral("contrastThreshold"), 5, 1, 255,
                     QStringLiteral("对比度阈值（特征点与背景的灰度差，越大淘汰越多）"),
                     QStringLiteral("灰阶")),
        makeBoolParam(QStringLiteral("autoEdgeThreshold"), false,
                      QStringLiteral("阈值模式：自动（按模板对比度自动生成）")),
        makeIntParam(QStringLiteral("featureScale"), 1, 1, 20,
                     QStringLiteral("特征尺度（金字塔底层粒度，越大特征点越稀疏；1 最精细）")),
        makeIntParam(QStringLiteral("speedScale"), 3, 1, 20,
                     QStringLiteral("速度尺度（金字塔顶层降采样倍率，越大越快越易漏检）")),
        makeIntParam(QStringLiteral("roiRow"), 0, 0, 100000,
                     QStringLiteral("模板区行（旧工程：左上；0=未设置）"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiCol"), 0, 0, 100000,
                     QStringLiteral("模板区列（旧工程：左上）"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiWidth"), 0, 0, 100000,
                     QStringLiteral("模板区宽（0=自动中心50%）"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiHeight"), 0, 0, 100000,
                     QStringLiteral("模板区高（0=自动）"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("roiCenterCol"), 0.0, 0.0, 100000.0,
                        QStringLiteral("旋转框中心列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("roiCenterRow"), 0.0, 0.0, 100000.0,
                        QStringLiteral("旋转框中心行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("roiAngle"), 0.0, -180.0, 180.0,
                        QStringLiteral("模板框角度"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("angleMin"), -30.0, -180.0, 180.0,
                        QStringLiteral("搜索角度下限"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("angleMax"), 30.0, -180.0, 180.0,
                        QStringLiteral("搜索角度上限"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("angleStep"), 2.0, 0.5, 45.0,
                        QStringLiteral("搜索角度步进"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("scaleMin"), 1.0, 0.3, 3.0,
                        QStringLiteral("搜索尺度下限")),
        makeDoubleParam(QStringLiteral("scaleMax"), 1.0, 0.3, 3.0,
                        QStringLiteral("搜索尺度上限")),
        makeDoubleParam(QStringLiteral("scaleStep"), 0.05, 0.01, 0.5,
                        QStringLiteral("搜索尺度步进")),
        makeStringParam(QStringLiteral("writeFixtureName"), QString(),
                        QStringLiteral("匹配位姿写入 Fixture 名（空=不写）")),
        // 搜索区域（r19 批 A 第 2 步）：匹配只在这个矩形内搜——既是 VM 的「检测区域」，
    // 也是提速手段（区域越小 matchTemplate 越快）。0 = 整图（默认行为不变）。
    makeIntParam(QStringLiteral("searchRegionCol"), 0, 0, 100000,
                 QStringLiteral("搜索区域左列")),
    makeIntParam(QStringLiteral("searchRegionRow"), 0, 0, 100000,
                 QStringLiteral("搜索区域上行")),
    makeIntParam(QStringLiteral("searchRegionWidth"), 0, 0, 100000,
                 QStringLiteral("搜索区域宽")),
    makeIntParam(QStringLiteral("searchRegionHeight"), 0, 0, 100000,
                 QStringLiteral("搜索区域高")),
    // 搜索区域多边形（r19）："x1,y1 x2,y2 ..."，>=3 点生效，优先于矩形。
    makeStringParam(QStringLiteral("searchRegionPolygon"), QString(),
                    QStringLiteral("搜索区域多边形(空=用矩形)")),
});

    // VM 4.4 口径分组（对应工具窗四个页签）。此前所有参数都落在默认的「参数」一组里，
    // 一长条平铺下来就是"看着乱"的根源；分组后每个 QGroupBox 归位到对应页签。
    for (auto &s : m_paramSpecs) {
        const QString &n = s.name;
        if (n == QLatin1String("templatePath") || n == QLatin1String("trainFromImage")
            || n == QLatin1String("method"))
            s.group = QStringLiteral("图像输入");
        else if (n == QLatin1String("outputMask") || n == QLatin1String("roiType")
                 || n == QLatin1String("roiSource")
                 || n.startsWith(QLatin1String("searchRegion")))
            s.group = QStringLiteral("ROI区域");
        else if (n == QLatin1String("scaleMode") || n == QLatin1String("contrastThreshold")
                 || n == QLatin1String("autoEdgeThreshold")
                 || n == QLatin1String("featureScale")
                 || n == QLatin1String("speedScale")
                 || n == QLatin1String("minChainLength")
                 || n == QLatin1String("chainMode"))
            s.group = QStringLiteral("模板配置");
        else if (n.startsWith(QLatin1String("show")))
            s.group = QStringLiteral("结果显示");
        else
            s.group = QStringLiteral("运行参数");
    }

    m_params[QStringLiteral("matchRow")] = 0.0;
    m_params[QStringLiteral("matchCol")] = 0.0;
    m_params[QStringLiteral("matchScore")] = 0.0;
    m_params[QStringLiteral("matchAngle")] = 0.0;
    m_params[QStringLiteral("matchScale")] = 1.0;
    m_params[QStringLiteral("matchPointRow")] = 0.0;
    m_params[QStringLiteral("matchPointCol")] = 0.0;
    m_params[QStringLiteral("matchScaleX")] = 1.0;
    m_params[QStringLiteral("matchScaleY")] = 1.0;
    m_params[QStringLiteral("matchTemplateIndex")] = -1;
    m_params[QStringLiteral("matchTemplateName")] = QString();
    m_params[QStringLiteral("trainStatus")] = QString();
}

cv::Mat OpencvTemplateMatchNode::extractTemplatePatch(const cv::Mat &gray, const RoiShape &roi)
{
    if (gray.empty())
        return {};
    if (roi.type == RoiType::RotatedRect && roi.width > 1 && roi.height > 1) {
        const int w = qMax(1, int(roi.width + 0.5));
        const int h = qMax(1, int(roi.height + 0.5));
        // 零角度直裁（r19）：warpAffine 即使角度 0 也会重采样，抠出的模板与源窗口
        // 不一致 → 自匹配永远到不了 1.0、位置恒偏约 2px。整数直裁保证像素级一致。
        if (qAbs(roi.angleDeg) < 1e-6) {
            const int x0 = qBound(0, int(std::floor(roi.p1.x() - w * 0.5 + 0.5)), gray.cols - 1);
            const int y0 = qBound(0, int(std::floor(roi.p1.y() - h * 0.5 + 0.5)), gray.rows - 1);
            const int ww = qBound(1, w, gray.cols - x0);
            const int hh = qBound(1, h, gray.rows - y0);
            return gray(cv::Rect(x0, y0, ww, hh)).clone();
        }
        const cv::Point2f c(float(roi.p1.x()), float(roi.p1.y()));
        cv::Mat M = cv::getRotationMatrix2D(c, roi.angleDeg, 1.0);
        M.at<double>(0, 2) += w * 0.5 - c.x;
        M.at<double>(1, 2) += h * 0.5 - c.y;
        cv::Mat patch;
        cv::warpAffine(gray, patch, M, cv::Size(w, h), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        return patch;
    }
    // 轴对齐的几类（矩形/多边形/圆）统一走包围盒直裁；
    // 多边形/圆的角外内容由打分时的形状掩膜排除（见 runSingle 的 shapeMask）。
    const QRectF r = roiAxisAlignedBounds(roi);
    if (r.width() > 1 && r.height() > 1) {
        const int x = qBound(0, int(r.x()), gray.cols - 1);
        const int y = qBound(0, int(r.y()), gray.rows - 1);
        const int w = qBound(1, int(r.width() + 0.5), gray.cols - x);
        const int h = qBound(1, int(r.height() + 0.5), gray.rows - y);
        return gray(cv::Rect(x, y, w, h)).clone();
    }
    return {};
}

namespace {
/// 边缘幅值图：Sobel 3x3 -> 幅值 -> 8U。
/// 为什么需要它：灰度 NCC 在旋转插值与光照变化下会大幅掉分（现场出现过"找到了但只有
/// 17% 分"）；VM / VisionPro 的模板匹配都建立在边缘上——边缘幅值对整体亮度不敏感、
/// 对结构更敏感，同样姿态下分数区分度更好。
cv::Mat edgeMagnitudeOf(const cv::Mat &gray)
{
    if (gray.empty())
        return cv::Mat();
    cv::Mat gx, gy, mag;
    cv::Sobel(gray, gx, CV_32F, 1, 0, 3);
    cv::Sobel(gray, gy, CV_32F, 0, 1, 3);
    cv::magnitude(gx, gy, mag);
    cv::Mat out;
    mag.convertTo(out, CV_8U);
    return out;
}
}   // namespace

namespace {
/// "到最近边沿的距离场"（chamfer 用）：边沿=0，越远离边沿值越大。
/// 稀疏边沿图不能直接做 NCC（实测分数 0.138/0.448 而位置全对），
/// 形状匹配的标准做法是拿模板边沿点落在这个距离场上求均值。
cv::Mat edgeDistanceField(const cv::Mat &edge8u)
{
    if (edge8u.empty())
        return cv::Mat();
    cv::Mat inv;
    cv::bitwise_not(edge8u, inv);          // 边沿变 0，背景变 255
    cv::Mat dist;
    cv::distanceTransform(inv, dist, cv::DIST_L2, 3);
    return dist;                            // CV_32F
}
}   // namespace

void OpencvTemplateMatchNode::run(bool autoSwitch)
{
    // 多模板同时匹配（VM 的「特征模板」页语义）：逐个模板跑完整流程，取分数最高者。
    // 为什么要重跑一次：单模板流程末尾会把结果写成"本次命中"，直接汇总中间态很容易
    // 漏掉叠加/Measurement 的字段；用最优模板再跑一遍，所有产出与单模板路径完全一致。
    // 代价：N 个模板 ≈ N 倍匹配时间（2~3 个模板在 100~500ms 量级，可接受）。
    const QVector<TemplateEntry> entries = templates();
    const bool train = m_params.value(QStringLiteral("trainFromImage"), false).toBool();
    const bool matchAll =
        entries.size() > 1 && !train
        && m_params.value(QStringLiteral("matchAllTemplates"), false).toBool();
    if (!matchAll) {
        runSingle(autoSwitch);
        return;
    }

    const int savedIndex = activeTemplateIndex();
    double bestScore = -1e9;
    int bestIdx = -1;
    for (int i = 0; i < entries.size(); ++i) {
        m_params[QStringLiteral("templateIndex")] = i;
        runSingle(autoSwitch);
        const double score = m_params.value(QStringLiteral("matchScore"), 0.0).toDouble();
        if (m_params.value(QStringLiteral("moduleStatus"), false).toBool() && score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }

    if (bestIdx >= 0) {
        m_params[QStringLiteral("templateIndex")] = bestIdx;
        runSingle(autoSwitch);            // 用命中模板再跑一次：结果/叠加/输出对象一次成型
        m_params[QStringLiteral("matchTemplateIndex")] = bestIdx;
        m_params[QStringLiteral("matchTemplateName")] = entries.at(bestIdx).name;
    } else {
        m_params[QStringLiteral("templateIndex")] = savedIndex;
        m_params[QStringLiteral("matchTemplateIndex")] = -1;
        m_params[QStringLiteral("matchTemplateName")] = QString();
        m_params[QStringLiteral("moduleStatus")] = false;   // 失败原因保留最后一次模板的 lastError
    }
}

void OpencvTemplateMatchNode::runSingle(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat gray = OpencvUtil::himageToMat(input);

        // 只在搜索区域内搜（r19 批 A 第 2 步）：0 = 整图；裁剪后坐标再换算回原图
        // 未裁剪原图（r19 P1-14 ③）：训练抠模板必须用原图坐标，不能用裁过的 gray
        cv::Mat grayFull = gray;
        if (grayFull.channels() == 3)
            cv::cvtColor(grayFull, grayFull, cv::COLOR_BGR2GRAY);
        // ROI 继承（VM「ROI 创建 = 继承」）：上游模板/特征命中框直接当搜索区域。
        // 数据面取（不依赖场景图）：上游结果就在输入对象的 measureResults 里。
        int inheritCol = 0, inheritRow = 0, inheritW = 0, inheritH = 0;
        if (m_params.value(QStringLiteral("roiSource"), 0).toInt() == 1) {
            const QSharedPointer<DataObject> upstream = m_inputData.value(0);
            if (upstream) {
                for (const MeasureResult &mr : upstream->measureResults()) {
                    if ((mr.type == QLatin1String("template")
                         || mr.type == QLatin1String("feature"))
                        && mr.extraValues.size() >= 7) {
                        const double cx = mr.extraValues[1];
                        const double cy = mr.extraValues[0];
                        const double bw = mr.extraValues[5];
                        const double bh = mr.extraValues[6];
                        const double rad =
                            mr.extraValues[3] * 3.14159265358979323846 / 180.0;
                        const double ca = std::cos(rad);
                        const double sa = std::sin(rad);
                        const double corners[4][2] = {{-bw * 0.5, -bh * 0.5},
                                                      {bw * 0.5, -bh * 0.5},
                                                      {bw * 0.5, bh * 0.5},
                                                      {-bw * 0.5, bh * 0.5}};
                        double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
                        for (const auto &c : corners) {
                            const double px = cx + ca * c[0] - sa * c[1];
                            const double py = cy + sa * c[0] + ca * c[1];
                            x0 = qMin(x0, px);
                            x1 = qMax(x1, px);
                            y0 = qMin(y0, py);
                            y1 = qMax(y1, py);
                        }
                        // 外扩 8px：上游框通常正好等于模板大小，不扩会因
                        // "模板尺寸 >= 搜索区域" 被判无效，也留一点位置抖动余量
                        inheritCol = int(x0) - 8;
                        inheritRow = int(y0) - 8;
                        inheritW = qMax(1, int(x1 - x0) + 16);
                        inheritH = qMax(1, int(y1 - y0) + 16);
                        break;
                    }
                }
            }
        }

        cv::Rect searchRect(0, 0, gray.cols, gray.rows);
        {
            int rc = m_params.value(QStringLiteral("searchRegionCol"), 0).toInt();
            int rr = m_params.value(QStringLiteral("searchRegionRow"), 0).toInt();
            int rw = m_params.value(QStringLiteral("searchRegionWidth"), 0).toInt();
            int rh = m_params.value(QStringLiteral("searchRegionHeight"), 0).toInt();
            if (inheritW > 2 && inheritH > 2) {
                rc = inheritCol;
                rr = inheritRow;
                rw = inheritW;
                rh = inheritH;
            }
            if (rw > 2 && rh > 2) {
                searchRect = cv::Rect(qBound(0, rc, gray.cols - 1),
                                     qBound(0, rr, gray.rows - 1),
                                     qMin(rw, gray.cols),
                                     qMin(rh, gray.rows))
                                 & cv::Rect(0, 0, gray.cols, gray.rows);
                if (searchRect.width > 2 && searchRect.height > 2)
                    gray = gray(searchRect);
            }
        }

        // 多边形搜索区域（r19）："x1,y1 x2,y2 ..."；>=3 点生效，优先于矩形
        std::vector<cv::Point> searchPoly;
        {
            const QString polyText =
                m_params.value(QStringLiteral("searchRegionPolygon")).toString().trimmed();
            const QStringList pts = polyText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString &pt : pts) {
                const QStringList xy = pt.split(QLatin1Char(','));
                if (xy.size() == 2) {
                    bool ok1 = false, ok2 = false;
                    const double x = xy[0].toDouble(&ok1);
                    const double y = xy[1].toDouble(&ok2);
                    if (ok1 && ok2)
                        searchPoly.push_back(cv::Point(int(x) - searchRect.x, int(y) - searchRect.y));
                }
            }
            if (searchPoly.size() >= 3) {
                const cv::Rect polyBox = cv::boundingRect(searchPoly)
                                         & cv::Rect(0, 0, gray.cols, gray.rows);
                if (polyBox.width > 2 && polyBox.height > 2) {
                    gray = gray(polyBox);
                    searchRect = polyBox + searchRect.tl();
                    for (auto &pt : searchPoly) {
                        pt.x -= polyBox.x;
                        pt.y -= polyBox.y;
                    }
                }
            } else {
                searchPoly.clear();
            }
        }
        if (gray.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (gray.channels() == 3) {
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
        }

        // 模板对象（VM 对齐）：列表非空时，路径与 4 项边缘参数都取"当前模板"的；
        // 列表为空则退回老的 templatePath + 节点参数（老方案逐字节不变）。
        const TemplateEntry active = activeTemplate();
        const QString templatePath = ProjectPaths::resolveExecutionPath(
            active.imagePath.trimmed().isEmpty()
                ? m_params.value(QStringLiteral("templatePath"), QString()).toString()
                : active.imagePath).trimmed();
        const bool train = m_params.value(QStringLiteral("trainFromImage"), false).toBool();
        const int methodIdx = m_params.value(QStringLiteral("method"), 1).toInt();
        const double minScore = m_params.value(QStringLiteral("minScore"), 0.6).toDouble();
        const int method = (methodIdx == 0) ? cv::TM_SQDIFF_NORMED : cv::TM_CCOEFF_NORMED;

        cv::Mat tmpl;
        if (train) {
            const RoiShape roi = geometryRoi();
            tmpl = extractTemplatePatch(grayFull, roi);   // r19 P1-14 ③：用未裁剪原图
            if (tmpl.empty()) {
                const int y = static_cast<int>(gray.rows * 0.25);
                const int x = static_cast<int>(gray.cols * 0.25);
                const int w = static_cast<int>(gray.cols * 0.50);
                const int h = static_cast<int>(gray.rows * 0.50);
                tmpl = gray(cv::Rect(x, y, w, h)).clone();
            }
            if (!templatePath.isEmpty()) {
                try {
                    QFileInfo fi(templatePath);
                    if (!fi.dir().exists()) QDir().mkpath(fi.absolutePath());
                    if (cv::imwrite(templatePath.toStdString(), tmpl)) {
                        m_params[QStringLiteral("trainStatus")] =
                            QStringLiteral("已训练并保存: %1").arg(templatePath);
                        // 教学是一次性动作：落盘成功后回到匹配模式，此后每轮读文件而不是重教覆写。
                        // 走 setParamDirect：run() 里调 setParam 会重启预览定时器，形成自激。
                        setParamDirect(QStringLiteral("trainFromImage"), false);
                    } else {
                        // 教学要落盘却没落成必须判红（2026-10-26 复核 P3）：原来只写 trainStatus
                        // 就继续匹配——现场以为模板已存，下一轮读不到文件才在别处失败。
                        m_params[QStringLiteral("trainStatus")] =
                            QStringLiteral("模板保存失败: %1").arg(templatePath);
                        m_params[QStringLiteral("lastError")] =
                            m_params[QStringLiteral("trainStatus")];
                        m_params["moduleStatus"] = false;
                        m_outputImage = m_inputImage;
                        setOutputData(1, QSharedPointer<DataObject>());
                        return;
                    }
                } catch (const std::exception &e) {
                    m_params[QStringLiteral("trainStatus")] =
                        QStringLiteral("模板保存异常: %1").arg(QString::fromLocal8Bit(e.what()));
                    m_params[QStringLiteral("lastError")] =
                        m_params[QStringLiteral("trainStatus")];
                    m_params["moduleStatus"] = false;
                    m_outputImage = m_inputImage;
                    setOutputData(1, QSharedPointer<DataObject>());
                    return;
                }
            } else {
                m_params[QStringLiteral("trainStatus")] = QStringLiteral("已训练（未保存文件）");
            }
        } else {
            if (templatePath.isEmpty()) {
                m_params[QStringLiteral("trainStatus")] =
                    QStringLiteral("匹配模式需要设置模板文件路径");
                m_params["moduleStatus"] = false;
                m_outputImage = m_inputImage;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            // 每轮 imread 同一个模板是纯浪费（r19 P1-7）：按 (路径, mtime) 缓存，换文件自动重载
        cv::Mat loaded = ModelCache::imageFor(templatePath, cv::IMREAD_GRAYSCALE);
            if (loaded.empty()) {
                m_params[QStringLiteral("trainStatus")] =
                    QStringLiteral("模板加载失败: %1").arg(templatePath);
                m_params["moduleStatus"] = false;
                m_outputImage = m_inputImage;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            tmpl = loaded;
            m_params[QStringLiteral("trainStatus")] =
                QStringLiteral("已加载模板: %1").arg(templatePath);
        }

        if (tmpl.empty() || tmpl.cols >= gray.cols || tmpl.rows >= gray.rows) {
            m_params[QStringLiteral("trainStatus")] = QStringLiteral("模板尺寸无效");
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }

        // VM 对齐（2026-11-02）：边缘阈值 / 特征尺度（Sobel 核）/ 速度尺度（粗搜长边）
        // 取"当前模板"的参数（列表为空时 = 节点参数）。默认值 = 原常量，老方案行为不变。
        // ── VM 模板配置参数（官方口径，见 docs/VM4.4高精度匹配对齐规范.md）──
        // 速度尺度(1~20) = 搜索降采样倍率；尺度模式=自动时按图像大小定（让顶层约 256px）。
        // 特征尺度(1~20, ≤速度尺度) = 特征点提取粒度（网格稀疏化），1 最精细。
        // 对比度阈值(1~255) = 特征点与背景的灰度差；Sobel(ksize=3) 阶跃响应 = 4×灰度差。
        // 最小链长 = 特征链最短点数，短于此的链在建模时丢弃。
        int speedScale = qBound(1, active.speedScale, 20);
        if (m_params.value(QStringLiteral("scaleMode"), 1).toInt() == 1) {
            // 自动：让粗搜降采样后长边约 640px（≈老版本 coarseLongSide=480 的档位），
            // 再粗位置误差就会超过精修窗口，现场会看到"整体偏一截"。
            speedScale =
                qBound(2, int(std::lround(double(qMax(gray.cols, gray.rows)) / 640.0)), 20);
        }
        const int featureScale = qBound(1, qMin(active.featureScale, speedScale), 20);
        const int sobelKsize = 3;
        const double edgeThreshold = effectiveEdgeThresholdFor(
            tmpl, sobelKsize, active.contrastThreshold, active.autoThreshold);
        const int minChain = active.chainModeAuto
                                 ? qMax(2, featureScale * 2)
                                 : qBound(1, active.minChainLength, 200);

        QVector<double> angles;
        QVector<double> scales;
        collectSearchValues(m_params.value(QStringLiteral("angleMin"), -30.0).toDouble(),
                            m_params.value(QStringLiteral("angleMax"), 30.0).toDouble(),
                            m_params.value(QStringLiteral("angleStep"), 2.0).toDouble(),
                            90, angles);
        double sMin = m_params.value(QStringLiteral("scaleMin"), 1.0).toDouble();
        double sMax = m_params.value(QStringLiteral("scaleMax"), 1.0).toDouble();
        double sStep = m_params.value(QStringLiteral("scaleStep"), 0.05).toDouble();
        if (m_params.value(QStringLiteral("scaleMode"), 0).toInt() == 1) {
            // 尺度模式=自动（VM）：模板占搜索区越大，能缩放的空间越小。
            // 覆盖 >25% 只留 ±10%；否则 ±30%（缩小侧与放大侧等幅度），步长 = 跨度/6。
            const double tArea = double(tmpl.cols) * double(tmpl.rows);
            const double sArea = double(gray.cols) * double(gray.rows);
            const double span = (sArea > 1.0 && tArea / sArea > 0.25) ? 0.10 : 0.30;
            sMin = 1.0 / (1.0 + span);
            sMax = 1.0 + span;
            sStep = (sMax - sMin) / 6.0;
        }
        collectSearchValues(sMin, sMax, sStep, 16, scales);

        const bool singleIdentity = (angles.size() == 1 && qAbs(angles.first()) < 1e-6
                                     && scales.size() == 1 && qAbs(scales.first() - 1.0) < 1e-6);

        // ── 粗到细搜索（r19：对齐 VisionMaster 的做法） ──
        // 实测：1500×1000 + 208px 模板 + 31 角度，全分辨率穷举 ≈16 秒。
        // 现在先在长边≈480 的粗图上搜全部姿态，再只对最优姿态做全分辨率局部精修。
        // 速度尺度 = 粗搜降采样倍率（1=不降采样）；VM：值越大越快越易漏检
        const double coarseFactor = (singleIdentity || speedScale <= 1) ? 1.0
                                                                        : double(speedScale);
        cv::Mat graySearch = gray;
        cv::Mat tmplSearch = tmpl;
        if (coarseFactor > 1.0) {
            cv::resize(gray, graySearch, cv::Size(), 1.0 / coarseFactor, 1.0 / coarseFactor,
                       cv::INTER_AREA);
            cv::resize(tmpl, tmplSearch, cv::Size(), 1.0 / coarseFactor, 1.0 / coarseFactor,
                       cv::INTER_AREA);
        }

        // 形状掩膜：多形状模板 / 多边形 / 圆的包围盒外内容不参与打分
        // （否则圆模板会拿四个角去匹配，等于把"圆"当成了"方"）。
        cv::Mat shapeMask;
        {
            const QVector<RoiShape> shapeList = active.allShapes();
            bool needMask = shapeList.size() > 1 || !active.excludes.isEmpty();
            for (const RoiShape &s : shapeList) {
                if (s.type == RoiType::Polygon || s.type == RoiType::Circle)
                    needMask = true;
            }
            if (needMask) {
                QVector<RoiShape> all = shapeList;
                all += active.excludes;
                shapeMask = shapesMask(shapeList, shapesBounds(all), active.excludes);
            }
        }

        // ROI 图像类型（VM 的「区域类型=图像类型」）：第二个图像输入当掩膜，
        // 白=允许、黑=排除。按粗搜栅格缩放，候选中心必须落在白区。
        cv::Mat roiMask;
        cv::Mat roiMaskFull;   // 原图尺寸（输出掩膜用）
        if (m_params.value(QStringLiteral("roiType"), 0).toInt() == 1) {
            const QSharedPointer<DataObject> maskData = m_inputData.value(1);
            if (maskData && maskData->getHImage().IsInitialized()) {
                cv::Mat m = OpencvUtil::himageToMat(HImage(maskData->getHImage()));
                if (m.channels() == 3)
                    cv::cvtColor(m, m, cv::COLOR_BGR2GRAY);
                if (!m.empty()) {
                    if (m.size() != grayFull.size())
                        cv::resize(m, m, grayFull.size(), 0, 0, cv::INTER_NEAREST);
                    const cv::Rect rr =
                        searchRect & cv::Rect(0, 0, grayFull.cols, grayFull.rows);
                    if (rr.width > 2 && rr.height > 2) {
                        cv::threshold(m, roiMaskFull, 127, 255, cv::THRESH_BINARY);
                        cv::resize(m(rr), roiMask, graySearch.size(), 0, 0, cv::INTER_NEAREST);
                        cv::threshold(roiMask, roiMask, 127, 255, cv::THRESH_BINARY);
                    }
                }
            }
        }

        // 缓存键：模板身份（尺寸 + 路径）+ 角度 + 尺度
        const QString warpKeyBase =
            QStringLiteral("%1x%2:%3")
                .arg(tmpl.cols)
                .arg(tmpl.rows)
                .arg(templatePath.isEmpty() ? QStringLiteral("mem") : templatePath);

        double bestScore = -1e9;
        cv::Point bestLoc(0, 0);
        double bestAngle = 0.0;
        double bestScale = 1.0;
        cv::Point2f bestCenterInWarp(tmpl.cols * 0.5f, tmpl.rows * 0.5f);
        cv::Point2f bestCoarseCenter;   // 粗搜栅格下的中心（多目标抑制用）
        cv::Point2f bestSubpix;         // 亚像素偏移（r19 收尾）
        cv::Point2f subpixTmp;
        const double tmplW = tmpl.cols;
        const double tmplH = tmpl.rows;

        // 多边形按当前搜索栅格换算（r19 P1-14 ②）：粗搜是降采样图、精修是子窗口，
        // 两者与原图的点不是同一栅格，不能直接拿裁剪后的点去过滤。
        // 编辑器掩膜（白=保留、黑=忽略）：裁到 gray 的坐标，方法 2 评分时按它排除
        cv::Mat maskOfGray;
        if (hasEditMask()) {
            const QImage mImg = editMask();
            if (!mImg.isNull()) {
                QImage g = mImg.convertToFormat(QImage::Format_Grayscale8);
                cv::Mat mv(g.height(), g.width(), CV_8UC1,
                           const_cast<uchar *>(g.constBits()), qsizetype(g.bytesPerLine()));
                const cv::Rect want(searchRect.tl(), gray.size());
                const cv::Rect avail(0, 0, mv.cols, mv.rows);
                const cv::Rect r = want & avail;
                if (r.width == gray.cols && r.height == gray.rows)
                    maskOfGray = mv(r).clone();
            }
        }
        cv::Mat curSearchCos, curSearchSin;   // method=2 搜索图方向图（每遍一次）
        cv::Mat curSearchStrong;             // 搜索图强梯度掩膜（遮挡自适应用）
        std::vector<cv::Point> polyCoarse, polyFull;
        const std::vector<cv::Point> *activePoly = nullptr;

        // 多目标（r19）：已找到中心（全分辨率）+ 当前搜索栅格映射
        const double suppressR = 0.6 * qMax(tmplW, tmplH);   // 抑制半径（全分辨率像素）
        std::vector<cv::Point2f> foundCenters;
        double rasterScale = 1.0;        // 搜索图像素 / 全分辨率像素
        cv::Point searchOrigin;          // 搜索图在图像中的左上角（粗搜为 0）

        // 单姿态 + 单尺度时保留原有的"低方差区域抑制"（避免纹理空白处高分）
        auto evalOne = [&](const cv::Mat &search, const cv::Mat &base, double angle,
                           double scale, cv::Point *locOut, cv::Point2f *centerOut,
                           cv::Point2f *subpixOut = nullptr) -> double {
            const auto warpedPair = warpTemplateCached(base, warpKeyBase, angle, scale);
            const cv::Mat &warped = warpedPair.first;
            if (warped.empty() || warped.cols >= search.cols || warped.rows >= search.rows)
                return -1e9;
            cv::Mat result;
            if (methodIdx == 2) {
                // 梯度方向相关（GOC）：Σ cos(θs−θt)·mask / N（模板强梯度点的方向一致性均值）
                if (curSearchCos.empty() || curSearchSin.empty() || warped.empty())
                    return -1e9;
                cv::Mat wgx, wgy, wmag;
                cv::Sobel(warped, wgx, CV_32F, 1, 0, sobelKsize);
                cv::Sobel(warped, wgy, CV_32F, 0, 1, sobelKsize);
                cv::magnitude(wgx, wgy, wmag);
                cv::Mat wm;
                cv::threshold(wmag, wm, edgeThreshold, 1.0, cv::THRESH_BINARY);
                wm.convertTo(wm, CV_32F);
                if (!shapeMask.empty()) {
                    // 掩膜与模板同一套 warp（同一 keyBase + 角度/尺度），保证像素严格对齐
                    const auto warpedMask = warpTemplateCached(
                        shapeMask, warpKeyBase + QStringLiteral("#shapeMask"), angle, scale);
                    if (!warpedMask.first.empty() && warpedMask.first.size() == wm.size()) {
                        cv::Mat keep;
                        cv::threshold(warpedMask.first, keep, 127.0, 1.0, cv::THRESH_BINARY);
                        keep.convertTo(keep, CV_32F);
                        wm = wm.mul(keep);
                    }
                }
                refineFeatureMask(wm, minChain, featureScale);   // 最小链长 + 特征尺度
                const double n = cv::sum(wm)[0];
                if (n < 16.0)
                    return -1e9;
                cv::Mat wc, ws;
                cv::divide(wgx, wmag + 1e-6f, wc);
                cv::divide(wgy, wmag + 1e-6f, ws);
                wc = wc.mul(wm);
                ws = ws.mul(wm);
                cv::Mat r1, r2, rv;
                cv::matchTemplate(curSearchCos, wc, r1, cv::TM_CCORR);
                cv::matchTemplate(curSearchSin, ws, r2, cv::TM_CCORR);
                // 可见点数（分子已按同一掩膜屏蔽，分母也用它）
                cv::matchTemplate(curSearchStrong, wm, rv, cv::TM_CCORR);
                // 可见率门槛：内部常量（非 VM 参数）。少于 40% 的模板边缘可见即判无效，
                // 用来抗遮挡；VM 未暴露这一项，保持原写死值。
                const double need = qMax(16.0, 0.4 * n);
                cv::Mat scoreVis = (r1 + r2) / (rv + 1e-6f);   // cos(Δθ) 的和除以可见点数
                scoreVis.setTo(-1.0, rv < need);           // 无效处给 -1（不会被选为最优）
                if (m_params.value(QStringLiteral("polarity"), 1).toInt() == 0)
                    cv::absdiff(scoreVis, cv::Scalar(0), scoreVis);
                result = scoreVis;
            } else {
                cv::matchTemplate(search, warped, result, method);
            }
            if (singleIdentity && methodIdx != 2) {   // GOC 自带可见性逻辑，不再做低方差清零
                cv::Mat grayF;
                search.convertTo(grayF, CV_32F);
                cv::Mat mean, meanSq, sqF;
                cv::multiply(grayF, grayF, sqF);
                cv::boxFilter(grayF, mean, CV_32F, warped.size());
                cv::boxFilter(sqF, meanSq, CV_32F, warped.size());
                cv::Mat var = meanSq - mean.mul(mean);
                cv::Mat varROI = var(cv::Rect(0, 0, result.cols, result.rows));
                cv::Scalar tMean, tStd;
                cv::meanStdDev(warped, tMean, tStd);
                const double tVar = tStd[0] * tStd[0];
                if (tVar > 1e-6)
                    result.setTo(0.0, varROI < tVar * 0.25);
            }
            // ROI 图像类型：候选中心落在掩膜黑区的一律判无效（与多边形过滤同一层）
            if (!roiMask.empty() && !result.empty() && roiMask.size() == search.size()) {
                cv::Mat candMask = cv::Mat::zeros(result.size(), CV_8U);
                for (int yy = 0; yy < result.rows; ++yy) {
                    for (int xx = 0; xx < result.cols; ++xx) {
                        const int cx = xx + int(warpedPair.second.x);
                        const int cy = yy + int(warpedPair.second.y);
                        if (cx >= 0 && cy >= 0 && cx < roiMask.cols && cy < roiMask.rows
                            && roiMask.at<uchar>(cy, cx) != 0)
                            candMask.at<uchar>(yy, xx) = 255;
                    }
                }
                result.setTo(-1e9, ~candMask);
            }

            // 多边形过滤：候选位置（模板中心）不在多边形内的峰值屏蔽掉
            if (activePoly && !activePoly->empty() && !result.empty()) {
                cv::Mat candMask = cv::Mat::zeros(result.size(), CV_8U);
                for (int yy = 0; yy < result.rows; ++yy) {
                    for (int xx = 0; xx < result.cols; ++xx) {
                        const double cx = xx + warpedPair.second.x;
                        const double cy = yy + warpedPair.second.y;
                        if (cv::pointPolygonTest(*activePoly, cv::Point2f(float(cx), float(cy)),
                                                 false) >= 0.0)
                            candMask.at<uchar>(yy, xx) = 255;
                    }
                }
                if (methodIdx == 0)
                    result.setTo(1e9, ~candMask);
                else
                    result.setTo(-1e9, ~candMask);
            }
            // 已找到的位置附近在 result 上清零，下一次扫描才能看到次优峰
            if (!foundCenters.empty() && !result.empty()) {
                for (const cv::Point2f &fc : foundCenters) {
                    const double rx = fc.x * rasterScale - searchOrigin.x - warpedPair.second.x;
                    const double ry = fc.y * rasterScale - searchOrigin.y - warpedPair.second.y;
                    const double rr = suppressR * rasterScale;
                    if (rr >= 1.0)
                        cv::circle(result, cv::Point(int(rx), int(ry)), int(rr),
                                   (methodIdx == 0) ? cv::Scalar(1e9) : cv::Scalar(-1e9), -1);
                }
            }
            double minVal = 0.0, maxVal = 0.0;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
            if (locOut)
                // 亚像素：峰值邻域抛物线插值（x/y 各一次），VM/VisionPro 都给亚像素
            if (subpixOut) {
                const cv::Point peak = (methodIdx == 0) ? minLoc : maxLoc;
                auto at = [&](int y, int x) -> float {
                    if (y < 0 || x < 0 || y >= result.rows || x >= result.cols)
                        return (methodIdx == 0) ? 1e9f : -1e9f;
                    return result.at<float>(y, x);
                };
                const float vc = at(peak.y, peak.x);
                const float vl = at(peak.y, peak.x - 1), vr = at(peak.y, peak.x + 1);
                const float vu = at(peak.y - 1, peak.x), vd = at(peak.y + 1, peak.x);
                float dx = 0.f, dy = 0.f;
                const float denX = vl - 2.f * vc + vr;
                const float denY = vu - 2.f * vc + vd;
                if (std::fabs(denX) > 1e-12f)
                    dx = 0.5f * (vl - vr) / denX;
                if (std::fabs(denY) > 1e-12f)
                    dy = 0.5f * (vu - vd) / denY;
                *subpixOut = cv::Point2f(qBound(-1.f, dx, 1.f), qBound(-1.f, dy, 1.f));
            }
            *locOut = (methodIdx == 0) ? minLoc : maxLoc;
            if (centerOut)
                *centerOut = warpedPair.second;
            return (methodIdx == 0) ? (1.0 - minVal) : maxVal;
        };

        // 多边形换算到粗搜栅格（r19 P1-14 ②）
        if (!searchPoly.empty() && coarseFactor > 1.0) {
            for (const auto &pt : searchPoly)
                polyCoarse.emplace_back(int(pt.x / coarseFactor), int(pt.y / coarseFactor));
            activePoly = &polyCoarse;
        } else if (!searchPoly.empty()) {
            polyFull = searchPoly;
            activePoly = &polyFull;
        }

        // ① 粗搜：全部角度 × 尺度（粗图上跑）
        // 多目标（r19 批 A 续）：每轮抑制已找到的中心，最多 maxMatches 个
        const int maxMatches =
            qBound(1, m_params.value(QStringLiteral("maxMatches"), 1).toInt(), 16);
        QStringList matchList;
        int matchCount = 0;
        cv::Point mainLoc;
        double mainScore = -1e9, mainAngle = 0.0, mainScale = 1.0;
        cv::Point2f mainCenter;

        for (int inst = 0; inst < maxMatches; ++inst) {
            bestScore = -1e9;
            bestLoc = cv::Point();
            bestAngle = 0.0;
            bestScale = 1.0;
            bestCenterInWarp = cv::Point2f();
            bestCoarseCenter = cv::Point2f();
            bestSubpix = cv::Point2f();

            // 粗搜：全部角度 x 尺度（粗图上跑）
            rasterScale = 1.0 / qMax(1.0, coarseFactor);
            searchOrigin = cv::Point(0, 0);
        for (double scale : scales) {
            for (double angle : angles) {
                cv::Point loc;
                cv::Point2f centerInWarp;
        // method=2：倒角匹配（搜索图先转距离场，模板边沿落场取均值）
        const cv::Mat coarseSearchImg = graySearch;
        const cv::Mat coarseTmplImg = tmplSearch;
        if (methodIdx == 2) {
            cv::Mat sgx, sgy, smag;
            cv::Sobel(graySearch, sgx, CV_32F, 1, 0, sobelKsize);
            cv::Sobel(graySearch, sgy, CV_32F, 0, 1, sobelKsize);
            cv::magnitude(sgx, sgy, smag);
            cv::divide(sgx, smag + 1e-6f, curSearchCos);
            cv::divide(sgy, smag + 1e-6f, curSearchSin);
            cv::threshold(smag, curSearchStrong, edgeThreshold, 1.0, cv::THRESH_BINARY);
            refineFeatureMask(curSearchStrong, minChain, featureScale);
            curSearchStrong.convertTo(curSearchStrong, CV_32F);
            cv::multiply(curSearchCos, curSearchStrong, curSearchCos);   // 分子分母同掩膜
            cv::multiply(curSearchSin, curSearchStrong, curSearchSin);
            if (!maskOfGray.empty()) {   // 掩膜参与评分：忽略区在分子分母都排除
            cv::Mat maskHere;
            cv::resize(maskOfGray, maskHere, curSearchStrong.size());
                cv::Mat keep;
                cv::threshold(maskHere, keep, 127.0, 1.0, cv::THRESH_BINARY);
                keep.convertTo(keep, CV_32F);
                curSearchStrong = curSearchStrong.mul(keep);
                curSearchCos = curSearchCos.mul(keep);
                curSearchSin = curSearchSin.mul(keep);
            }
        }
                const double sc = evalOne(coarseSearchImg, coarseTmplImg, angle, scale, &loc, &centerInWarp,
                                         &subpixTmp);
                if (sc > bestScore) {
                    bestScore = sc;
                    bestLoc = loc;
                    bestAngle = angle;
                    bestScale = scale;
                    bestCenterInWarp = centerInWarp;
                    bestSubpix = subpixTmp;
                    bestCoarseCenter = cv::Point2f(loc.x + centerInWarp.x,
                                                   loc.y + centerInWarp.y);
                }
                if (bestScore >= 0.95)
                    break;   // 足够好就收工（VM 也这么做）
            }
            if (bestScore >= 0.95)
                break;
        }

        // 粗搜坐标**无条件**换算回全分辨率（r19 P1-14 ①）：原来只在精修严格更好时才改写，
        // 分数持平/略低时把降采样像素当成全图坐标 → 位置偏 ≈coarseFactor（现场 3 倍）。
        if (coarseFactor > 1.0 && bestScore > -1e8) {
            bestLoc = cv::Point(int(bestLoc.x * coarseFactor + 0.5),
                                int(bestLoc.y * coarseFactor + 0.5));
            bestCenterInWarp = cv::Point2f(float(bestCenterInWarp.x * coarseFactor),
                                           float(bestCenterInWarp.y * coarseFactor));
            // 精修栅格 = 全分辨率子窗口；多边形也要跟着换算（减窗口偏移在下面做）
            polyFull = searchPoly;
            activePoly = searchPoly.empty() ? nullptr : &polyFull;
        }

        // ② 精修：全分辨率、最优姿态附近（角度 ±1 步 + 位置局部窗口）
        if (coarseFactor > 1.0 && bestScore > -1e8) {
            const int margin = qMax(24, int(qMax(tmplW, tmplH) * 0.6));
            const int cx = bestLoc.x;   // r19 P1-14 ①：bestLoc 已是全分辨率
            const int cy = bestLoc.y;
            const int x0 = qBound(0, cx - margin, qMax(0, gray.cols - 1));
            const int y0 = qBound(0, cy - margin, qMax(0, gray.rows - 1));
            const int w = qMin(gray.cols - x0, int(tmplW) + margin * 2);
            const int h = qMin(gray.rows - y0, int(tmplH) + margin * 2);
            if (w > tmplW + 2 && h > tmplH + 2) {
                const cv::Mat win = gray(cv::Rect(x0, y0, w, h));
                rasterScale = 1.0;
                searchOrigin = cv::Point(x0, y0);
                // method=2：精修同样走边缘幅值（窗口小，代价可忽略）
                const cv::Mat refineWin = win;
                const cv::Mat refineTmpl = tmpl;
                if (methodIdx == 2) {
                    cv::Mat sgx, sgy, smag;
                    cv::Sobel(win, sgx, CV_32F, 1, 0, sobelKsize);
                    cv::Sobel(win, sgy, CV_32F, 0, 1, sobelKsize);
                    cv::magnitude(sgx, sgy, smag);
                    cv::divide(sgx, smag + 1e-6f, curSearchCos);
                    cv::divide(sgy, smag + 1e-6f, curSearchSin);
                    cv::threshold(smag, curSearchStrong, edgeThreshold, 1.0, cv::THRESH_BINARY);
            refineFeatureMask(curSearchStrong, minChain, featureScale);
                    curSearchStrong.convertTo(curSearchStrong, CV_32F);
                    cv::multiply(curSearchCos, curSearchStrong, curSearchCos);   // 分子分母同掩膜
                    cv::multiply(curSearchSin, curSearchStrong, curSearchSin);
                    if (!maskOfGray.empty()) {   // 掩膜参与评分：忽略区在分子分母都排除
                    cv::Mat maskHere = maskOfGray;
                    if (searchOrigin.x + maskHere.cols >= 0 &&
                        searchOrigin.x + curSearchStrong.cols <= maskHere.cols &&
                        searchOrigin.y + curSearchStrong.rows <= maskHere.rows) {
                        maskHere = maskOfGray(cv::Rect(searchOrigin.x, searchOrigin.y,
                                                                                  curSearchStrong.cols, curSearchStrong.rows));
                    }
                        cv::Mat keep;
                        cv::threshold(maskHere, keep, 127.0, 1.0, cv::THRESH_BINARY);
                        keep.convertTo(keep, CV_32F);
                        curSearchStrong = curSearchStrong.mul(keep);
                        curSearchCos = curSearchCos.mul(keep);
                        curSearchSin = curSearchSin.mul(keep);
                    }
                }
                // 多边形换到子窗口栅格（r19 P1-14 ②）
                if (!searchPoly.empty()) {
                    polyFull.clear();
                    for (const auto &pt : searchPoly)
                        polyFull.emplace_back(pt.x - x0, pt.y - y0);
                    activePoly = &polyFull;
                }
                const double stepA =
                    qMax(0.5, m_params.value(QStringLiteral("angleStep"), 2.0).toDouble());
                for (int k = -1; k <= 1; ++k) {
                    const double angle = bestAngle + k * stepA;
                    if (angle < angles.first() - 1e-9 || angle > angles.last() + 1e-9)
                        continue;
                    cv::Point loc;
                    cv::Point2f centerInWarp;
                    const double sc = evalOne(refineWin, refineTmpl, angle, bestScale, &loc, &centerInWarp,
                                         &subpixTmp);
                    if (sc > bestScore) {
                        bestScore = sc;
                        bestLoc = cv::Point(x0 + loc.x, y0 + loc.y);
                        bestAngle = angle;
                        bestCenterInWarp = centerInWarp;
                    }
                }
            }
        }

            if (bestScore < -1e8)
                break;
            if (inst > 0 && bestScore < minScore)
                break;                       // 追加实例必须过阈值（第一实例维持原口径）
            if (inst == 0) {
                mainLoc = bestLoc;
                mainScore = bestScore;
                mainAngle = bestAngle;
                mainScale = bestScale;
                mainCenter = bestCenterInWarp;
            }
            {
                // 列表坐标必须换算回原图（搜索区域裁剪过时，直接用 bestLoc 会整体偏一个 searchRect）。
                // 字段：row,col,score,angle,scale,pointRow,pointCol —— 前 3 项保持老顺序。
                const double centerX = bestLoc.x + bestCenterInWarp.x + searchRect.x;
                const double centerY = bestLoc.y + bestCenterInWarp.y + searchRect.y;
                // 匹配点 = 锚点（模板中心 + 配置的偏移）缩放旋转后的落点
                const double ax = active.anchorDx * bestScale;
                const double ay = active.anchorDy * bestScale;
                const double rad = bestAngle * 3.14159265358979323846 / 180.0;
                const double ca = std::cos(rad);
                const double sa = std::sin(rad);
                const double pCol = centerX + ca * ax - sa * ay;
                const double pRow = centerY + sa * ax + ca * ay;
                matchList << QStringLiteral("%1,%2,%3,%4,%5,%6,%7")
                                 .arg(centerY, 0, 'f', 1)
                                 .arg(centerX, 0, 'f', 1)
                                 .arg(bestScore, 0, 'f', 3)
                                 .arg(bestAngle, 0, 'f', 2)
                                 .arg(bestScale, 0, 'f', 4)
                                 .arg(pRow, 0, 'f', 1)
                                 .arg(pCol, 0, 'f', 1);
            }
            ++matchCount;
            foundCenters.push_back(cv::Point2f(float(bestLoc.x + bestCenterInWarp.x),
                                              float(bestLoc.y + bestCenterInWarp.y)));
            if (bestScore < minScore)
                break;                       // 第一实例也未达标：不再找更多
        }

        // 第一实例写回既有输出口径
        if (matchCount > 0) {
            bestLoc = mainLoc;
            bestScore = mainScore;
            bestAngle = mainAngle;
            bestScale = mainScale;
            bestCenterInWarp = mainCenter;
        }
        m_params[QStringLiteral("matchCount")] = matchCount;
        m_params[QStringLiteral("matchList")] = matchList.join(QLatin1Char(';'));

        if (bestScore < -1e8) {
            m_params[QStringLiteral("trainStatus")] = QStringLiteral("无有效搜索位姿（模板相对图像过大）");
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }

        // 坐标换算回原图（搜索区域裁剪过时）
        const double col = bestLoc.x + searchRect.x + bestCenterInWarp.x;
        const double row = bestLoc.y + searchRect.y + bestCenterInWarp.y;
        const double outW = tmplW * bestScale;
        const double outH = tmplH * bestScale;

        m_params[QStringLiteral("matchRow")] = row + bestSubpix.y;
        m_params[QStringLiteral("matchCol")] = col + bestSubpix.x;
        m_params[QStringLiteral("matchScore")] = bestScore;
        m_params[QStringLiteral("matchAngle")] = bestAngle;
        m_params[QStringLiteral("matchScale")] = bestScale;
        // VM 结果表口径：「匹配点」= 模板的匹配中心（锚点）落到场景里的位置。
        // 锚点默认在模板中心（anchorDx/Dy=0），可在模板配置窗里偏移并锁定。
        {
            const double radA = bestAngle * 3.14159265358979323846 / 180.0;
            const double caA = std::cos(radA);
            const double saA = std::sin(radA);
            const double hx = active.anchorDx * bestScale;
            const double hy = active.anchorDy * bestScale;
            m_params[QStringLiteral("matchPointCol")] = col + bestSubpix.x + caA * hx - saA * hy;
            m_params[QStringLiteral("matchPointRow")] = row + bestSubpix.y + saA * hx + caA * hy;
            m_params[QStringLiteral("matchScaleX")] = bestScale;
            m_params[QStringLiteral("matchScaleY")] = bestScale;
        }
        m_params["moduleStatus"] = (bestScore >= minScore);
        if (bestScore < minScore) {
            // 未匹配也要说明原因（2026-10-26 复核）：只判红，面板上看不到差多少
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("匹配分数 %1 低于阈值 %2").arg(bestScore).arg(minScore);
        }
        const QString fixtureName =
            m_params.value(QStringLiteral("writeFixtureName")).toString().trimmed();
        if (!fixtureName.isEmpty() && bestScore >= minScore) {
            if (FlowScene *fs = flowSceneRef())
                fs->setFixturePose(fixtureName, row, col, bestAngle, bestScale);
        }
        m_outputImage = m_inputImage;

        // 输出掩膜（VM 开关）：最终搜索区域/掩膜作为具名子结果，挂在主结果对象上
        QSharedPointer<DataObject> maskOutObj;
        if (m_params.value(QStringLiteral("outputMask"), false).toBool()) {
            cv::Mat maskOut;
            if (!roiMaskFull.empty()) {
                maskOut = roiMaskFull;
            } else {
                maskOut = cv::Mat::zeros(grayFull.size(), CV_8UC1);
                if (searchPoly.size() >= 3) {
                    std::vector<cv::Point> pts = searchPoly;
                    for (cv::Point &p : pts) {
                        p.x += searchRect.x;
                        p.y += searchRect.y;
                    }
                    cv::fillPoly(maskOut, std::vector<std::vector<cv::Point>>{pts},
                                 cv::Scalar(255));
                } else {
                    cv::rectangle(maskOut, searchRect, cv::Scalar(255), cv::FILLED);
                }
            }
            maskOutObj = QSharedPointer<DataObject>::create();
            maskOutObj->setHImage(OpencvUtil::matToHimage(maskOut));
        }

        MeasureResult res;
        res.type = QStringLiteral("template");
        res.valueName = QStringLiteral("匹配得分");
        res.valid = (bestScore >= minScore);
        res.value = bestScore;
        res.point1 = QPointF(col, row);
        res.extraValues = QVector<double>({row, col, bestScore, bestAngle, bestScale, outW, outH,
                                           m_params.value(QStringLiteral("matchPointRow")).toDouble(),
                                           m_params.value(QStringLiteral("matchPointCol")).toDouble(),
                                           bestScale, bestScale});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        if (maskOutObj)
            resObj->attachResult(QStringLiteral("掩膜"), maskOutObj);
        setOutputData(1, resObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvTemplateMatchNode error:" << e.what();
        m_params[QStringLiteral("lastError")] =
            QString::fromUtf8(e.what());   // 与 catch(...) 对齐（2026-10-26 复核）
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvTemplateMatchNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvTemplateMatchNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

RoiShape OpencvTemplateMatchNode::geometryRoi() const
{
    const double w = m_params.value(QStringLiteral("roiWidth"), 0).toDouble();
    const double h = m_params.value(QStringLiteral("roiHeight"), 0).toDouble();
    if (w <= 1 || h <= 1)
        return RoiShape();
    RoiShape s;
    s.type = RoiType::RotatedRect;
    s.width = w;
    s.height = h;
    s.angleDeg = m_params.value(QStringLiteral("roiAngle"), 0.0).toDouble();
    const double cx = m_params.value(QStringLiteral("roiCenterCol"), 0.0).toDouble();
    const double cy = m_params.value(QStringLiteral("roiCenterRow"), 0.0).toDouble();
    if (cx != 0.0 || cy != 0.0) {
        s.p1 = QPointF(cx, cy);
    } else {
        const double col = m_params.value(QStringLiteral("roiCol"), 0).toDouble();
        const double row = m_params.value(QStringLiteral("roiRow"), 0).toDouble();
        s.p1 = QPointF(col + w * 0.5, row + h * 0.5);
    }
    return s;
}

void OpencvTemplateMatchNode::applyGeometryRoi(const RoiShape &shape)
{
    if (shape.type == RoiType::None) {
        setParam(QStringLiteral("roiWidth"), 0);
        setParam(QStringLiteral("roiHeight"), 0);
        setParam(QStringLiteral("roiAngle"), 0.0);
        setParam(QStringLiteral("roiCenterCol"), 0.0);
        setParam(QStringLiteral("roiCenterRow"), 0.0);
        return;
    }
    if (shape.type == RoiType::RotatedRect && shape.width > 1 && shape.height > 1) {
        const QRectF aabb = roiAxisAlignedBounds(shape);
        setParam(QStringLiteral("roiCenterCol"), shape.p1.x());
        setParam(QStringLiteral("roiCenterRow"), shape.p1.y());
        setParam(QStringLiteral("roiWidth"), qMax(1, int(shape.width + 0.5)));
        setParam(QStringLiteral("roiHeight"), qMax(1, int(shape.height + 0.5)));
        setParam(QStringLiteral("roiAngle"), shape.angleDeg);
        setParam(QStringLiteral("roiCol"), int(aabb.x() + 0.5));
        setParam(QStringLiteral("roiRow"), int(aabb.y() + 0.5));
        setParam(QStringLiteral("trainFromImage"), true);
        return;
    }
    if (shape.type == RoiType::Rect) {
        const QRectF r = roiAxisAlignedBounds(shape);
        if (r.width() <= 1 || r.height() <= 1)
            return;
        setParam(QStringLiteral("roiCol"), int(r.x() + 0.5));
        setParam(QStringLiteral("roiRow"), int(r.y() + 0.5));
        setParam(QStringLiteral("roiWidth"), qMax(1, int(r.width() + 0.5)));
        setParam(QStringLiteral("roiHeight"), qMax(1, int(r.height() + 0.5)));
        setParam(QStringLiteral("roiCenterCol"), r.center().x());
        setParam(QStringLiteral("roiCenterRow"), r.center().y());
        setParam(QStringLiteral("roiAngle"), 0.0);
        setParam(QStringLiteral("trainFromImage"), true);
    }
}

QVector<TemplateEntry> OpencvTemplateMatchNode::templates() const
{
    QVector<TemplateEntry> out;
    const QString json =
        m_params.value(QStringLiteral("templates"), QString()).toString().trimmed();
    if (!json.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (doc.isArray()) {
            for (const QJsonValue &v : doc.array()) {
                if (v.isObject())
                    out.append(TemplateEntry::fromJson(v.toObject()));
            }
        }
    }
    if (out.isEmpty()) {
        // 老方案：只有一个 templatePath。这里合成一条，让调用方永远拿到"至少一个模板"。
        // 注意：不写回参数（migrateLegacyTemplate 才落库），避免只读路径改状态。
        const QString path =
            m_params.value(QStringLiteral("templatePath"), QString()).toString().trimmed();
        if (!path.isEmpty()) {
            TemplateEntry e;
            e.name = QFileInfo(path).fileName();
            e.imagePath = path;
            e.shape = geometryRoi();
            e.contrastThreshold = qBound(
                1, m_params.value(QStringLiteral("contrastThreshold"),
                                  qMax(1, int(m_params.value(QStringLiteral("edgeThreshold"), 20.0)
                                                  .toDouble()
                                          * 0.25
                                          + 0.5)))
                       .toInt(),
                255);
            e.autoThreshold =
                m_params.value(QStringLiteral("autoEdgeThreshold"), false).toBool();
            e.featureScale =
                qBound(1, m_params.value(QStringLiteral("featureScale"), 1).toInt(), 20);
            e.speedScale = qBound(1, m_params.value(QStringLiteral("speedScale"), 3).toInt(), 20);
            e.minChainLength =
                qBound(1, m_params.value(QStringLiteral("minChainLength"), 1).toInt(), 200);
            e.chainModeAuto = m_params.value(QStringLiteral("chainMode"), false).toBool();
            out.append(e);
        }
    }
    return out;
}

void OpencvTemplateMatchNode::setTemplates(const QVector<TemplateEntry> &entries)
{
    QJsonArray arr;
    for (const TemplateEntry &e : entries)
        arr.append(e.toJson());
    m_params[QStringLiteral("templates")] =
        QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

int OpencvTemplateMatchNode::activeTemplateIndex() const
{
    const int count = templates().size();
    if (count <= 0)
        return 0;
    return qBound(0, m_params.value(QStringLiteral("templateIndex"), 0).toInt(), count - 1);
}

void OpencvTemplateMatchNode::setActiveTemplateIndex(int index)
{
    m_params[QStringLiteral("templateIndex")] = qMax(0, index);
}

TemplateEntry OpencvTemplateMatchNode::activeTemplate() const
{
    const QVector<TemplateEntry> list = templates();
    if (list.isEmpty()) {
        // 连 templatePath 都没有：返回一个"空模板"，调用方按原逻辑报"需要模板文件"
        TemplateEntry e;
        e.contrastThreshold = qBound(
            1, m_params.value(QStringLiteral("contrastThreshold"),
                              qMax(1, int(m_params.value(QStringLiteral("edgeThreshold"), 20.0)
                                              .toDouble()
                                      * 0.25
                                      + 0.5)))
                   .toInt(),
            255);
        e.autoThreshold = m_params.value(QStringLiteral("autoEdgeThreshold"), false).toBool();
        e.featureScale = qBound(1, m_params.value(QStringLiteral("featureScale"), 1).toInt(), 20);
        e.speedScale = qBound(1, m_params.value(QStringLiteral("speedScale"), 3).toInt(), 20);
        e.minChainLength =
            qBound(1, m_params.value(QStringLiteral("minChainLength"), 1).toInt(), 200);
        e.chainModeAuto = m_params.value(QStringLiteral("chainMode"), false).toBool();
        return e;
    }
    return list.at(activeTemplateIndex());
}

void OpencvTemplateMatchNode::migrateLegacyTemplate()
{
    if (!m_params.value(QStringLiteral("templates"), QString()).toString().trimmed().isEmpty())
        return;
    const QString path =
        m_params.value(QStringLiteral("templatePath"), QString()).toString().trimmed();
    if (path.isEmpty())
        return;
    setTemplates(templates());   // templates() 已按老参数合成一条
    setActiveTemplateIndex(0);
}

double OpencvTemplateMatchNode::effectiveEdgeThresholdFor(const cv::Mat &tmpl, int aperture,
                                                          double manualGrayDiff, bool autoMode)
{
    // 入参是 VM 口径的「灰度差」(1~255)；Sobel(ksize=3) 的阶跃响应 = 4×灰度差，
    // 统一在这里换算，调用方都按灰度差传值。
    const double manual = manualGrayDiff * 4.0;
    if (!autoMode || tmpl.empty())
        return qBound(1.0, manual, 200.0);

    // 自动模式：取模板梯度强度的 70 分位。模板本身对比度高 → 阈值自动抬高；
    // 低对比工件 → 自动压低，不会因为"固定 20"一个都用不上或全用上。
    cv::Mat gx, gy, mag;
    cv::Sobel(tmpl, gx, CV_32F, 1, 0, aperture);
    cv::Sobel(tmpl, gy, CV_32F, 0, 1, aperture);
    cv::magnitude(gx, gy, mag);
    std::vector<float> vals;
    vals.reserve(size_t(mag.total()));
    for (int y = 0; y < mag.rows; ++y) {
        const float *row = mag.ptr<float>(y);
        vals.insert(vals.end(), row, row + mag.cols);
    }
    if (vals.empty())
        return qBound(1.0, manual, 200.0);
    const size_t k = size_t(double(vals.size()) * 0.7);
    std::nth_element(vals.begin(), vals.begin() + k, vals.end());
    return qBound(6.0, double(vals[k]), 80.0);
}

double OpencvTemplateMatchNode::effectiveEdgeThreshold(const cv::Mat &tmpl, int aperture) const
{
    const TemplateEntry e = activeTemplate();
    return effectiveEdgeThresholdFor(tmpl, aperture, e.contrastThreshold, e.autoThreshold);
}

QVector<QPointF> OpencvTemplateMatchNode::editorFeaturePoints() const
{
    // 绿点 = 模板强梯度点。GOC（method=2）就是拿这些点打分；灰度法（0/1）逐像素算，
    // 这里仍画出来给操作员看模板轮廓质量（训练向导的"角点丰富度"看的就是它）。
    // 模板来源与 run() 同一优先级：先"当前模板"的文件，再当前输入图 + ROI 现抠
    const TemplateEntry active = activeTemplate();
    cv::Mat tmpl;
    const QString path = ProjectPaths::resolveExecutionPath(
        active.imagePath.trimmed().isEmpty()
            ? m_params.value(QStringLiteral("templatePath"), QString()).toString()
            : active.imagePath).trimmed();
    if (!path.isEmpty())
        tmpl = ModelCache::imageFor(path, cv::IMREAD_GRAYSCALE);
    if (tmpl.empty()) {
        const HObject input = getInputImage();
        if (input.IsInitialized()) {
            cv::Mat gray = OpencvUtil::himageToMat(HImage(input));
            if (gray.channels() == 3)
                cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
            tmpl = extractTemplatePatch(gray, geometryRoi());
        }
    }
    if (tmpl.empty())
        return {};

    const int aperture = 3;
    const double threshold =
        effectiveEdgeThresholdFor(tmpl, aperture, active.contrastThreshold, active.autoThreshold);
    const QVector<RoiShape> shapeList = active.allShapes();
    cv::Mat shapeMask;
    bool needMask = shapeList.size() > 1;
    for (const RoiShape &s : shapeList) {
        if (s.type == RoiType::Polygon || s.type == RoiType::Circle)
            needMask = true;
    }
    if (needMask)
        shapeMask = shapesMask(shapeList, shapesBounds(shapeList));
    const int featureScale = qBound(1, active.featureScale, 20);
    const int minChain = active.chainModeAuto ? qMax(2, featureScale * 2)
                                              : qBound(1, active.minChainLength, 200);
    return featurePointsForPatch(tmpl, geometryRoi(), threshold, aperture, shapeMask,
                                 featureScale, minChain);
}

OpencvTemplateMatchNode::ParamSuggestion
OpencvTemplateMatchNode::suggestParams(const cv::Mat &tmpl)
{
    ParamSuggestion s;
    if (tmpl.empty())
        return s;
    // VM 口径：速度尺度/特征尺度默认取官方截图里建模板成功后的 3 / 3；
    // 对比度阈值按模板真实对比度给（灰度差 = Sobel 幅值 / 4 的 70 分位）。
    s.speedScale = 3;
    s.featureScale = 3;
    s.minChainLength = 3;
    cv::Mat gx, gy, mag;
    cv::Sobel(tmpl, gx, CV_32F, 1, 0, 3);
    cv::Sobel(tmpl, gy, CV_32F, 0, 1, 3);
    cv::magnitude(gx, gy, mag);
    std::vector<float> edges;
    edges.reserve(size_t(mag.total()) / 4);
    for (int y = 0; y < mag.rows; ++y) {
        const float *row = mag.ptr<float>(y);
        for (int x = 0; x < mag.cols; ++x) {
            if (row[x] > 2.0f)
                edges.push_back(row[x] * 0.25f);   // Sobel → 灰度差
        }
    }
    double t = 20.0;
    if (!edges.empty()) {
        const size_t k = size_t(double(edges.size()) * 0.7);
        std::nth_element(edges.begin(), edges.begin() + k, edges.end());
        t = edges[k];
    }
    s.contrastThreshold = qBound(1, int(std::round(t)), 255);
    s.autoThreshold = false;          // 给的是具体数字，操作员能看懂也能改
    return s;
}

bool OpencvTemplateMatchNode::applySuggestedParams()
{
    const TemplateEntry active = activeTemplate();
    cv::Mat tmpl;
    const QString path = ProjectPaths::resolveExecutionPath(
        active.imagePath.trimmed().isEmpty()
            ? m_params.value(QStringLiteral("templatePath"), QString()).toString()
            : active.imagePath).trimmed();
    if (!path.isEmpty())
        tmpl = ModelCache::imageFor(path, cv::IMREAD_GRAYSCALE);
    if (tmpl.empty()) {
        const HObject input = getInputImage();
        if (!input.IsInitialized())
            return false;
        cv::Mat gray = OpencvUtil::himageToMat(HImage(input));
        if (gray.channels() == 3)
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
        tmpl = extractTemplatePatch(gray, geometryRoi());
    }
    if (tmpl.empty())
        return false;
    const ParamSuggestion s = suggestParams(tmpl);
    m_params[QStringLiteral("contrastThreshold")] = s.contrastThreshold;
    m_params[QStringLiteral("autoEdgeThreshold")] = s.autoThreshold;
    m_params[QStringLiteral("featureScale")] = s.featureScale;
    m_params[QStringLiteral("speedScale")] = s.speedScale;
    m_params[QStringLiteral("minChainLength")] = s.minChainLength;
    // 当前模板对象也同步：否则列表里的模板仍用旧参数
    QVector<TemplateEntry> list = templates();
    if (!list.isEmpty()) {
        const int idx = activeTemplateIndex();
        if (idx >= 0 && idx < list.size()) {
            list[idx].contrastThreshold = s.contrastThreshold;
            list[idx].autoThreshold = s.autoThreshold;
            list[idx].featureScale = s.featureScale;
            list[idx].speedScale = s.speedScale;
            list[idx].minChainLength = s.minChainLength;
            setTemplates(list);
        }
    }
    return true;
}

QRectF OpencvTemplateMatchNode::shapesBounds(const QVector<RoiShape> &shapes)
{
    QRectF box;
    for (const RoiShape &s : shapes) {
        const QRectF b = roiAxisAlignedBounds(s);
        if (b.width() <= 1.0 || b.height() <= 1.0)
            continue;
        box = box.isNull() ? b : box.united(b);
    }
    return box;
}

cv::Mat OpencvTemplateMatchNode::shapesMask(const QVector<RoiShape> &shapes, const QRectF &box,
                                            const QVector<RoiShape> &excludes)
{
    if (box.width() <= 4.0 || box.height() <= 4.0)
        return {};
    const int w = int(box.width() + 0.5);
    const int h = int(box.height() + 0.5);
    const int ox = int(box.x());
    const int oy = int(box.y());
    cv::Mat mask = cv::Mat::zeros(h, w, CV_8UC1);

    // 单块形状画进掩膜（value = 255 保留 / 0 屏蔽）
    auto paint = [&](const RoiShape &s, unsigned char value) {
        const cv::Scalar v(value);
        if (s.type == RoiType::Circle) {
            const double r = std::hypot(s.p2.x() - s.p1.x(), s.p2.y() - s.p1.y());
            if (r <= 1.0)
                return false;
            cv::circle(mask, cv::Point(int(s.p1.x()) - ox, int(s.p1.y()) - oy), int(r + 0.5), v, -1);
            return true;
        }
        if (s.type == RoiType::Polygon && s.points.size() >= 3) {
            std::vector<cv::Point> pts;
            pts.reserve(size_t(s.points.size()));
            for (const QPointF &p : s.points)
                pts.emplace_back(int(p.x()) - ox, int(p.y()) - oy);
            cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{pts}, v);
            return true;
        }
        if (s.type == RoiType::Rect || s.type == RoiType::RotatedRect) {
            const QRectF b = roiAxisAlignedBounds(s);
            if (b.width() <= 1.0 || b.height() <= 1.0)
                return false;
            cv::rectangle(mask,
                          cv::Rect(int(b.x()) - ox, int(b.y()) - oy, int(b.width()), int(b.height())),
                          v, -1);
            return true;
        }
        return false;
    };

    bool any = false;
    if (shapes.isEmpty() && !excludes.isEmpty()) {
        // 只画了屏蔽形状：默认整块保留，再减掉屏蔽区（"把 logo 挖掉"最常见）
        mask.setTo(cv::Scalar(255));
        any = true;
    }
    for (const RoiShape &s : shapes) {
        if (paint(s, 255))
            any = true;
    }
    for (const RoiShape &s : excludes) {
        paint(s, 0);      // 屏蔽：从保留区里挖掉
    }
    if (!any)
        return {};
    return mask;
}

QVector<QPointF> OpencvTemplateMatchNode::featurePointsForPatch(const cv::Mat &tmpl,
                                                               const RoiShape &roi,
                                                               double threshold, int aperture,
                                                               const cv::Mat &mask,
                                                               int featureScale, int minChain)
{
    if (tmpl.empty())
        return {};
    cv::Mat gx, gy, mag;
    cv::Sobel(tmpl, gx, CV_32F, 1, 0, qBound(3, aperture, 7) | 1);
    cv::Sobel(tmpl, gy, CV_32F, 0, 1, qBound(3, aperture, 7) | 1);
    cv::magnitude(gx, gy, mag);
    // 与打分同一套提纯：最小链长 + 特征尺度稀疏化（否则"看到的点"和"打分的点"又分叉）
    cv::Mat strong;
    cv::threshold(mag, strong, threshold, 1.0, cv::THRESH_BINARY);
    refineFeatureMask(strong, minChain, featureScale);

    const int w = tmpl.cols;
    const int h = tmpl.rows;
    // 点太多时按步长抽稀：视觉上仍能看出轮廓，又不至于每帧画几万个点。
    const int stride = qMax(1, int(std::lround(std::sqrt(double(w) * double(h) / 5000.0))));

    // 模板像素 → 图像坐标：轴对齐走 extractTemplatePatch 的整数直裁同一公式；
    // 旋转框走 getRotationMatrix2D 的逆映射（与 warpTemplate 的口径一致）。
    const bool axisAligned = (roi.type != RoiType::RotatedRect) || qAbs(roi.angleDeg) < 1e-6;
    // 多边形/圆的 p1 不是中心（多边形 p1 未用、圆 p1 是圆心），统一取包围盒中心
    const QRectF shapeBox = roiAxisAlignedBounds(roi);
    const double cx = axisAligned ? shapeBox.center().x() : roi.p1.x();
    const double cy = axisAligned ? shapeBox.center().y() : roi.p1.y();
    const int x0 = int(std::floor(cx - w * 0.5 + 0.5));
    const int y0 = int(std::floor(cy - h * 0.5 + 0.5));
    const double rad = roi.angleDeg * 3.14159265358979323846 / 180.0;
    const double ca = std::cos(rad);
    const double sa = std::sin(rad);

    QVector<QPointF> pts;
    pts.reserve(qMin(6000, (w / stride + 1) * (h / stride + 1)));
    for (int v = 0; v < h; v += stride) {
        const float *strongRow = strong.ptr<float>(v);
        for (int u = 0; u < w; u += stride) {
            if (strongRow[u] <= 0.5f)
                continue;
            // 形状掩膜外（多形状/圆/多边形的角外）不算特征点——与打分口径一致
            if (!mask.empty() && mask.type() == CV_8U && mask.size() == tmpl.size()
                && mask.at<uchar>(v, u) == 0)
                continue;
            if (axisAligned) {
                pts.append(QPointF(x0 + u, y0 + v));
            } else {
                const double lx = u - w * 0.5 + 0.5;
                const double ly = v - h * 0.5 + 0.5;
                pts.append(QPointF(cx + ca * lx - sa * ly, cy + sa * lx + ca * ly));
            }
        }
    }
    return pts;
}
