#include "OpencvTemplateMatchNode.h"
#include "NodeResultFields.h"
#include "VmSharedText.h"
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
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
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

/// 屏蔽区多边形串："x1,y1 x2,y2 ..."；多条用 ';' 分隔（VM 屏蔽区可画多块）。
QVector<RoiShape> maskPolygonsFromText(const QString &text)
{
    QVector<RoiShape> out;
    for (const QString &chunk : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        RoiShape s;
        s.type = RoiType::Polygon;
        for (const QString &pt : chunk.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const QStringList xy = pt.split(QLatin1Char(','));
            if (xy.size() != 2)
                continue;
            bool okX = false, okY = false;
            const double x = xy[0].toDouble(&okX);
            const double y = xy[1].toDouble(&okY);
            if (okX && okY)
                s.points.append(QPointF(x, y));
        }
        if (s.points.size() >= 3)
            out.append(s);
    }
    return out;
}

/// 把订阅到的屏蔽区对象转成全图掩膜（8U，255=屏蔽）。支持 Region / 图像 /
/// 具名子结果「掩膜」三种形态；取不到返回空（=没有屏蔽区）。
cv::Mat maskRegionFromData(const QSharedPointer<DataObject> &data, const cv::Size &size,
                           int depth = 0)
{
    if (!data || size.width <= 0 || size.height <= 0 || depth > 1)
        return {};
    if (data->getType() == DataObject::DataType::Region) {
        try {
            const HalconCpp::HRegion region = data->getHRegion();
            if (region.IsInitialized()) {
                cv::Mat m(size, CV_8UC1, cv::Scalar(0));
                HalconCpp::HTuple rows, cols;
                region.GetRegionPoints(&rows, &cols);
                const Hlong n = rows.Length();
                for (Hlong i = 0; i < n; ++i) {
                    const Hlong y = rows[i].L();
                    const Hlong x = cols[i].L();
                    if (x >= 0 && y >= 0 && x < size.width && y < size.height)
                        m.at<uchar>(int(y), int(x)) = 255;
                }
                return m;
            }
        } catch (const HalconCpp::HException &) {
            // 不是 Region（或已失效）：继续尝试图像形态
        }
    }
    if (data->getHImage().IsInitialized()) {
        cv::Mat m = OpencvUtil::himageToMat(HImage(data->getHImage()));
        if (!m.empty()) {
            if (m.channels() == 3)
                cv::cvtColor(m, m, cv::COLOR_BGR2GRAY);
            if (m.size() != size)
                cv::resize(m, m, size, 0, 0, cv::INTER_NEAREST);
            cv::threshold(m, m, 0, 255, cv::THRESH_BINARY);   // 白=屏蔽
            return m;
        }
    }
    if (depth < 1) {
        const QSharedPointer<DataObject> sub = data->attachedResult(QStringLiteral("掩膜"));
        if (sub)
            return maskRegionFromData(sub, size, depth + 1);
    }
    return {};
}

}   // namespace

QJsonObject TemplateEntry::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("image")] = imagePath;
    obj[QStringLiteral("enabled")] = enabled;
    const QVector<RoiShape> list = allShapes();
    QJsonArray arr;
    for (const RoiShape &s : list)
        arr.append(shapeToJsonObj(s));
    obj[QStringLiteral("shapes")] = arr;
    QJsonArray exArr;
    for (const RoiShape &s : excludes)
        exArr.append(shapeToJsonObj(s));
    obj[QStringLiteral("excludes")] = exArr;
    obj[QStringLiteral("contrastThreshold")] = contrastThreshold;
    obj[QStringLiteral("autoThreshold")] = autoThreshold;
    obj[QStringLiteral("featureScale")] = featureScale;
    obj[QStringLiteral("speedScale")] = speedScale;
    obj[QStringLiteral("scaleMode")] = scaleModeAuto;
    obj[QStringLiteral("minChainLength")] = minChainLength;
    obj[QStringLiteral("chainMode")] = chainModeAuto;
    obj[QStringLiteral("anchorX")] = anchorDx;
    obj[QStringLiteral("anchorY")] = anchorDy;
    obj[QStringLiteral("anchorLocked")] = anchorLocked;
    QJsonArray eraseArr;
    for (const RoiShape &s : erases)
        eraseArr.append(shapeToJsonObj(s));
    obj[QStringLiteral("erases")] = eraseArr;
    QJsonArray enhArr;
    for (int idx : enhancedShapes)
        enhArr.append(idx);
    obj[QStringLiteral("enhancedShapes")] = enhArr;
    return obj;
}

TemplateEntry TemplateEntry::fromJson(const QJsonObject &obj)
{
    TemplateEntry e;
    e.name = obj.value(QStringLiteral("name")).toString();
    e.imagePath = obj.value(QStringLiteral("image")).toString();
    e.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    // 模板形状只认 shapes 数组（不兼容旧单 shape 写法）
    for (const QJsonValue &v : obj.value(QStringLiteral("shapes")).toArray())
        if (v.isObject())
            e.shapes.append(shapeFromJsonObj(v.toObject()));
    if (!e.shapes.isEmpty())
        e.shape = e.shapes.first();
    for (const QJsonValue &v : obj.value(QStringLiteral("excludes")).toArray())
        if (v.isObject())
            e.excludes.append(shapeFromJsonObj(v.toObject()));
    e.autoThreshold = obj.value(QStringLiteral("autoThreshold")).toBool(false);
    // VM 口径字段：contrastThreshold / featureScale / speedScale 必须由 toJson 写入。
    e.contrastThreshold =
        qBound(1, obj.value(QStringLiteral("contrastThreshold")).toInt(5), 255);
    e.featureScale = qBound(1, obj.value(QStringLiteral("featureScale")).toInt(1), 20);
    e.speedScale = qBound(1, obj.value(QStringLiteral("speedScale")).toInt(3), 20);
    // 尺度模式：旧模板缺该键 → 按 VM 默认自动（项目未上线，不做旧值兼容）。
    e.scaleModeAuto = obj.value(QStringLiteral("scaleMode")).toBool(true);
    e.minChainLength = qBound(1, obj.value(QStringLiteral("minChainLength")).toInt(1), 200);
    e.chainModeAuto = obj.value(QStringLiteral("chainMode")).toBool(false);
    e.anchorDx = obj.value(QStringLiteral("anchorX")).toDouble(0.0);
    e.anchorDy = obj.value(QStringLiteral("anchorY")).toDouble(0.0);
    e.anchorLocked = obj.value(QStringLiteral("anchorLocked")).toBool(false);
    for (const QJsonValue &v : obj.value(QStringLiteral("erases")).toArray())
        if (v.isObject())
            e.erases.append(shapeFromJsonObj(v.toObject()));
    for (const QJsonValue &v : obj.value(QStringLiteral("enhancedShapes")).toArray())
        if (v.isDouble())
            e.enhancedShapes.append(v.toInt());
    return e;
}

namespace {

/// GOC 强梯度阈值的默认值（=原写死常量）。运行期以对比度阈值为准，
/// 自动模式按模板梯度分位数覆盖；可视化与打分都必须走 effectiveEdgeThreshold()。
constexpr double kGocEdgeThreshold = 20.0;

/// 边缘细化（非极大值抑制）：沿梯度方向只保留局部最大值，把 Sobel 的 2~3 像素宽带
/// 压成 1 像素轮廓线。VM 的模板轮廓点就是这种"细线点集"；直接对幅值二值化会把暗部
/// 纹理整片点亮，看起来是密密麻麻的点、也把打分权重摊到纹理上。
static cv::Mat thinEdgeMask(const cv::Mat &gx, const cv::Mat &gy, const cv::Mat &mag,
                            double threshold)
{
    cv::Mat mask = cv::Mat::zeros(mag.size(), CV_32F);
    if (mag.empty() || gx.empty() || gy.empty()
        || gx.size() != mag.size() || gy.size() != mag.size()) {
        return mask;
    }
    for (int y = 0; y < mag.rows; ++y) {
        const float *mRow = mag.ptr<float>(y);
        const float *gxRow = gx.ptr<float>(y);
        const float *gyRow = gy.ptr<float>(y);
        float *outRow = mask.ptr<float>(y);
        for (int x = 0; x < mag.cols; ++x) {
            const float m = mRow[x];
            if (m < float(threshold))
                continue;
            const float ax = std::abs(gxRow[x]);
            const float ay = std::abs(gyRow[x]);
            // 梯度方向量化为最近的主方向：水平 / 垂直 / 两条对角
            int dx = 0;
            int dy = 0;
            if (ax >= 2.0f * ay) {
                dx = 1;
            } else if (ay >= 2.0f * ax) {
                dy = 1;
            } else {
                dx = 1;
                dy = (gxRow[x] * gyRow[x] > 0.0f) ? 1 : -1;
            }
            const auto at = [&mag](int yy, int xx) -> float {
                if (yy < 0 || yy >= mag.rows || xx < 0 || xx >= mag.cols)
                    return 0.0f;
                return mag.ptr<float>(yy)[xx];
            };
            // 两侧邻居都更强 ⇒ 不是脊线中心，丢掉；保留 1 像素的局部极大
            if (at(y + dy, x + dx) > m || at(y - dy, x - dx) > m)
                continue;
            outRow[x] = 1.0f;
        }
    }
    return mask;
}

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

cv::Mat warpTemplate(const cv::Mat &tmpl, double angleDeg, double scaleX, double scaleY,
                     double crossAngleDeg, cv::Point2f *centerOut)
{
    if (qAbs(angleDeg) < 1e-6 && qAbs(scaleX - 1.0) < 1e-6 && qAbs(scaleY - 1.0) < 1e-6
        && qAbs(crossAngleDeg) < 1e-6) {
        if (centerOut)
            *centerOut = cv::Point2f(tmpl.cols * 0.5f, tmpl.rows * 0.5f);
        return tmpl;
    }
    const cv::Point2f center(tmpl.cols * 0.5f, tmpl.rows * 0.5f);
    cv::Mat M = cv::getRotationMatrix2D(center, angleDeg, 1.0);   // 先拿纯旋转
    // 非均匀缩放：R * diag(scaleX, scaleY) —— 缩放在模板局部系、旋转在其后
    M.at<double>(0, 0) *= scaleX;
    M.at<double>(0, 1) *= scaleY;
    M.at<double>(1, 0) *= scaleX;
    M.at<double>(1, 1) *= scaleY;
    // 横切角度（VM 官方「横切角度范围」）：目标 Y 轴向 X 轴倾斜的剪切角。
    // 在模板局部系、缩放之后应用 H = [[1, tanθ], [0, 1]] ⇒ M = R·S·H。
    if (qAbs(crossAngleDeg) > 1e-9) {
        const double t = std::tan(crossAngleDeg * 3.14159265358979323846 / 180.0);
        M.at<double>(0, 1) += M.at<double>(0, 0) * t;
        M.at<double>(1, 1) += M.at<double>(1, 0) * t;
    }
    // 平移必须按缩放后的矩阵重算（否则 scale≠1 时中心会偏）
    {
        const double a = M.at<double>(0, 0), b = M.at<double>(0, 1);
        const double c = M.at<double>(1, 0), d = M.at<double>(1, 1);
        M.at<double>(0, 2) = center.x - (a * center.x + b * center.y);
        M.at<double>(1, 2) = center.y - (c * center.x + d * center.y);
    }
    const double c00 = std::abs(M.at<double>(0, 0));
    const double c01 = std::abs(M.at<double>(0, 1));
    const double c10 = std::abs(M.at<double>(1, 0));
    const double c11 = std::abs(M.at<double>(1, 1));
    const int nw = std::max(1, int(std::ceil(tmpl.cols * c00 + tmpl.rows * c01)));
    const int nh = std::max(1, int(std::ceil(tmpl.cols * c10 + tmpl.rows * c11)));
    M.at<double>(0, 2) += nw * 0.5 - center.x;
    M.at<double>(1, 2) += nh * 0.5 - center.y;
    cv::Mat out;
    cv::warpAffine(tmpl, out, M, cv::Size(nw, nh), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    if (centerOut)
        *centerOut = cv::Point2f(nw * 0.5f, nh * 0.5f);
    return out;
}
namespace {

/// 旋转/缩放/横切后的模板缓存（模板不变时跨帧复用；LRU 32 条）。
/// 连 centerInWarp 一起缓存——它由 warpTemplate 按变换后的包围盒算出，不能事后猜。
std::pair<cv::Mat, cv::Point2f> warpTemplateCached(const cv::Mat &tmpl, const QString &keyBase,
                                                   double angle, double scaleX, double scaleY,
                                                   double crossAngle)
{
        // 恒等快路径（r19）：角度 0、尺度 1、横切 0 时直接用原模板 —— warpAffine 即便
        // 恒等变换也会重采样，导致自匹配到不了 1.0、位置偏约 2px。
        if (qAbs(angle) < 1e-6 && qAbs(scaleX - 1.0) < 1e-6 && qAbs(scaleY - 1.0) < 1e-6
            && qAbs(crossAngle) < 1e-6) {
            return {tmpl, cv::Point2f(float(tmpl.cols) * 0.5f, float(tmpl.rows) * 0.5f)};
        }

    static QMutex mu;
    static QHash<QString, std::pair<cv::Mat, cv::Point2f>> cache;
    static QStringList order;
    const QString key = keyBase + QStringLiteral("|%1|%2|%3|%4")
                                       .arg(angle, 0, 'f', 3)
                                       .arg(scaleX, 0, 'f', 3)
                                       .arg(scaleY, 0, 'f', 3)
                                       .arg(crossAngle, 0, 'f', 3);
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
    cv::Mat warped = warpTemplate(tmpl, angle, scaleX, scaleY, crossAngle, &c);
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
    setName(QStringLiteral("高精度匹配"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvTemplateMatchNode::init()
{
    HalconNode::init();   // 先建「输入图像」（必须是 0 号）
    // VM 4.4 基本参数页的「ROI区域类型=图像类型」：掩膜图像是**订阅**前序模块，
    // 不是第二个输入点（帮助文档：基本参数 → ROI区域 → 图像类型 → 掩膜图像）。
    // 因此这里只登记逻辑槽（下标 1，与老方案端口号一致），画布上始终只有 1 个输入点。
    // 数据由 FlowExecutor 在执行前按订阅绑定注入 m_inputData[1]。
    addInputSlot(QStringLiteral("掩膜图像"), PortDataType::Image);
    // VM「屏蔽区创建=继承」：屏蔽区（Region 或白=屏蔽的掩膜图）同样走订阅，不占画布输入点。
    addInputSlot(QStringLiteral("屏蔽区"), PortDataType::Any);
    addOutputPort(QStringLiteral("匹配结果"), PortDataType::Measure);
    registerParams({
        makeFilePathParam(QStringLiteral("templatePath"), QString(),
                          QStringLiteral("模板图像文件 (png/bmp/jpg)")),
        makeBoolParam(QStringLiteral("trainFromImage"), false,
                      QStringLiteral("从图像 ROI 训练并保存模板（训练落盘后自动取消勾选）")),
        makeIntParam(QStringLiteral("method"), 2, 0, 2,
                     QStringLiteral("匹配方式：0=灰度匹配（平方差）1=灰度匹配（相关系数）2=高精度匹配（边缘梯度）")),
        makeIntParam(QStringLiteral("polarity"), 1, 0, 1,
                     QStringLiteral("是否考虑极性：1=考虑（默认），0=忽略（亮标/暗标反色也能找到）")),
        makeIntParam(QStringLiteral("maxMatches"), 1, 1, 1000,
                     QStringLiteral("最多匹配个数（1~1000，默认 1；多件场景调大）")),
        makeBoolParam(QStringLiteral("matchAllTemplates"), false,
                      QStringLiteral("多模板同时匹配（模板列表逐个跑，取最高分）")),
        makeIntParam(QStringLiteral("minChainLength"), 1, 1, 200,
                     QStringLiteral("最小链长（特征链短于此不保留）")),
        makeBoolParam(QStringLiteral("chainMode"), false,
                      QStringLiteral("链长模式：自动（关=用上面的最小链长）")),
        // ── VM「运行参数」补齐（官方口径，见 docs/VM4.4高精度匹配对齐规范.md）──
        makeIntParam(QStringLiteral("thresholdType"), 2, 0, 2,
                     QStringLiteral("阈值类型：0=自动（按目标图像）1=模板 2=手动")),
        makeIntParam(QStringLiteral("matchContrastThreshold"), 5, 1, 255,
                     QStringLiteral("手动阈值（灰度差 1~255，阈值类型=手动时用）")),
        // VM「是否考虑噪点」：搜索图里不在边缘链上的特征点（毛刺）参与评分，评分降低。
        makeBoolParam(QStringLiteral("considerNoise"), false,
                      QStringLiteral("是否考虑噪点（开启后匹配图中不在边缘链上的特征点会降低评分）")),
        makeIntParam(QStringLiteral("sortType"), 0, 0, 5,
                     QStringLiteral("排序类型：0=按分数降序 1=按角度降序 2=按X升序 3=按Y升序 4=按X再Y 5=按Y再X")),
        makeIntParam(QStringLiteral("maxOverlap"), 50, 0, 100,
                     QStringLiteral("最大重叠率（0~100，默认 50；重叠超此值时丢低分目标）")),
        // VM「多模板重叠率」：仅「全部搜索模式」开启时参与跨模板结果合并。
        makeIntParam(QStringLiteral("multiTemplateOverlap"), 50, 0, 100,
                     QStringLiteral("多模板重叠率（全部搜索模式：不同模板命中重叠超此值时只保留高分者）")),
        makeIntParam(QStringLiteral("extendThreshold"), 0, 0, 100,
                     QStringLiteral("延拓阈值（目标被图像边缘截断时允许缺失的特征比例 %）")),
        makeIntParam(QStringLiteral("timeoutMs"), 0, 0, 10000,
                     QStringLiteral("超时控制（搜索超时毫秒数，0=关闭；超时不返回任何结果）")),
        // VM「是否获取轮廓 / 是否输出轮廓」（高精度匹配·运行参数）：
        // 获取=计算模板轮廓特征点并在画布上显示（关闭只显示匹配框）；输出=把轮廓点写进模块结果。
        makeBoolParam(QStringLiteral("getContour"), true,
                      QStringLiteral("是否获取轮廓（开启则计算并显示轮廓特征点；关闭只显示匹配框）")),
        makeBoolParam(QStringLiteral("outputContour"), false,
                      QStringLiteral("是否输出轮廓（打开则在模块结果中输出轮廓点；依赖是否获取轮廓）")),
        makeEnumParam(QStringLiteral("roiType"), 0,
                      QStringList{QStringLiteral("图形类型"), QStringLiteral("图像类型")},
                      QStringLiteral("ROI区域类型")),
        makeBoolParam(QStringLiteral("outputMask"), false,
                      QStringLiteral("输出掩膜（把最终搜索区域作为具名子结果输出）")),
        makeEnumParam(QStringLiteral("roiSource"), 0,
                      QStringList{QStringLiteral("绘制"), QStringLiteral("继承")},
                      QStringLiteral("ROI创建")),
        // VM「检测区域」第一行：把检测区域本身作为掩膜输出（与 ROI 区域掩膜是两个独立开关）
        makeBoolParam(QStringLiteral("outputSearchMask"), false,
                      QStringLiteral("输出掩膜（把此区域作为算子掩膜输入）")),
        // VM「尺度模式」只作用于模板配置的速度尺度（默认自动）；搜索尺度范围只有显式 min/max。
        makeIntParam(QStringLiteral("scaleMode"), 1, 0, 1,
                     QStringLiteral("尺度模式：0=手动（用速度尺度）1=自动（按图像大小定速度尺度）")),
        // VM「结果显示 → 图像显示」：每一层可单独显隐（截图 4 的四个眼睛开关）
        makeBoolParam(QStringLiteral("showSearchRegion"), true, QStringLiteral("显示检测区域")),
        makeBoolParam(QStringLiteral("showMatchResult"), true, QStringLiteral("显示匹配结果")),
        makeBoolParam(QStringLiteral("showMatchPoint"), true, QStringLiteral("显示匹配点")),
        makeBoolParam(QStringLiteral("showFeaturePoints"), true, QStringLiteral("显示特征点")),
        makeDoubleParam(QStringLiteral("minScore"), 0.5, 0.0, 1.0,
                        QStringLiteral("最低匹配度")),
        // VM「配置参数」对齐（2026-11-02）：把原先写死的边缘参数放出来。
        // 默认值 = 原常量，老方案行为逐字节不变。
        makeIntParam(QStringLiteral("contrastThreshold"), 5, 1, 255,
                     QStringLiteral("对比度阈值（特征点与背景的灰度差，越大淘汰越多）"),
                     QStringLiteral("灰阶")),
        makeBoolParam(QStringLiteral("autoContrastThreshold"), false,
                      QStringLiteral("阈值模式：自动（按模板对比度自动生成）")),
        makeIntParam(QStringLiteral("featureScale"), 1, 1, 20,
                     QStringLiteral("特征尺度（金字塔底层粒度，越大特征点越稀疏；1 最精细）")),
        makeIntParam(QStringLiteral("speedScale"), 3, 1, 20,
                     QStringLiteral("速度尺度（金字塔顶层降采样倍率，越大越快越易漏检）")),
        makeIntParam(QStringLiteral("roiWidth"), 0, 0, 100000,
                     QStringLiteral("模板框宽（0=未设置）"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiHeight"), 0, 0, 100000,
                     QStringLiteral("模板框高（0=未设置）"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("roiCenterCol"), 0.0, 0.0, 100000.0,
                        QStringLiteral("旋转框中心列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("roiCenterRow"), 0.0, 0.0, 100000.0,
                        QStringLiteral("旋转框中心行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("roiAngle"), 0.0, -180.0, 180.0,
                        QStringLiteral("模板框角度"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("angleMin"), -45.0, -180.0, 180.0,
                        QStringLiteral("搜索角度下限"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("angleMax"), 45.0, -180.0, 180.0,
                        QStringLiteral("搜索角度上限"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("angleStep"), 2.0, 0.5, 45.0,
                        QStringLiteral("搜索角度步进"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("scaleMin"), 1.0, 0.1, 10.0,
                        QStringLiteral("搜索尺度下限")),
        makeDoubleParam(QStringLiteral("scaleMax"), 1.0, 0.1, 10.0,
                        QStringLiteral("搜索尺度上限")),
        makeDoubleParam(QStringLiteral("scaleStep"), 0.05, 0.01, 0.5,
                        QStringLiteral("搜索尺度步进")),
        makeIntParam(QStringLiteral("scaleRangeType"), 0, 0, 1,
                     QStringLiteral("尺度范围类型：0=整体（scaleMin/Max），1=XY（X/Y 独立）")),
        makeDoubleParam(QStringLiteral("scaleMinX"), 1.0, 0.1, 10.0,
                        QStringLiteral("X 尺度起（scaleRangeType=1）")),
        makeDoubleParam(QStringLiteral("scaleMaxX"), 1.0, 0.1, 10.0,
                        QStringLiteral("X 尺度止（scaleRangeType=1）")),
        makeDoubleParam(QStringLiteral("scaleMinY"), 1.0, 0.1, 10.0,
                        QStringLiteral("Y 尺度起（scaleRangeType=1）")),
        makeDoubleParam(QStringLiteral("scaleMaxY"), 1.0, 0.1, 10.0,
                        QStringLiteral("Y 尺度止（scaleRangeType=1）")),
        // VM「尺度范围XY → 横切角度范围」：默认 0~0（只搜无横切目标），有效 -45~45。
        makeDoubleParam(QStringLiteral("crossAngleMin"), 0.0, -45.0, 45.0,
                        QStringLiteral("横切角度下限（scaleRangeType=1）"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("crossAngleMax"), 0.0, -45.0, 45.0,
                        QStringLiteral("横切角度上限（scaleRangeType=1）"), QStringLiteral("°")),
        makeStringParam(QStringLiteral("writeFixtureName"), QString(),
                        QStringLiteral("匹配位姿写入 Fixture 名（空=不写）")),
        // 搜索区域（r19 批 A 第 2 步）：匹配只在这个矩形内搜——既是 VM 的「检测区域」，
    // 也是提速手段（区域越小 matchTemplate 越快）。0 = 整图（默认行为不变）。
    makeIntParam(QStringLiteral("searchRegionCol"), 0, 0, 100000,
                 QStringLiteral("搜索区域左列")),
    makeIntParam(QStringLiteral("searchRegionRow"), 0, 0, 100000,
                 QStringLiteral("搜索区域上行")),
    makeIntParam(QStringLiteral("searchRegionWidth"), 0, 0, 100000,
                 QStringLiteral("搜索区域宽度")),
    makeIntParam(QStringLiteral("searchRegionHeight"), 0, 0, 100000,
                 QStringLiteral("搜索区域高度")),
    // 搜索区域多边形（r19）："x1,y1 x2,y2 ..."，>=3 点生效，优先于矩形。
    makeStringParam(QStringLiteral("searchRegionPolygon"), QString(),
                    QStringLiteral("搜索区域多边形（空=矩形）")),
    // VM「ROI创建=继承」的继承方式：按区域（上游命中框/区域）或按参数（X/Y/宽/高/角度，
    // 可用「参数订阅」逐项订阅上游结果）。
    makeEnumParam(QStringLiteral("roiInheritMode"), 0,
                  QStringList{QStringLiteral("按区域"), QStringLiteral("按参数")},
                  QStringLiteral("继承方式（继承时）")),
    makeDoubleParam(QStringLiteral("roiInheritX"), 0.0, -100000.0, 100000.0,
                    QStringLiteral("继承中心X（按参数）"), QStringLiteral("px")),
    makeDoubleParam(QStringLiteral("roiInheritY"), 0.0, -100000.0, 100000.0,
                    QStringLiteral("继承中心Y（按参数）"), QStringLiteral("px")),
    makeDoubleParam(QStringLiteral("roiInheritWidth"), 0.0, 0.0, 100000.0,
                    QStringLiteral("继承宽度（按参数，0=未设置）"), QStringLiteral("px")),
    makeDoubleParam(QStringLiteral("roiInheritHeight"), 0.0, 0.0, 100000.0,
                    QStringLiteral("继承高度（按参数，0=未设置）"), QStringLiteral("px")),
    makeDoubleParam(QStringLiteral("roiInheritAngle"), 0.0, -180.0, 180.0,
                    QStringLiteral("继承角度（按参数）"), QStringLiteral("°")),
    // VM「屏蔽区创建」：绘制（本节点画屏蔽多边形）/ 继承（订阅上游屏蔽区 Region/掩膜图）。
    makeEnumParam(QStringLiteral("maskSource"), 0,
                  QStringList{QStringLiteral("绘制"), QStringLiteral("继承")},
                  QStringLiteral("屏蔽区创建")),
    // 多条屏蔽区用 ';' 分隔；每条 "x1,y1 x2,y2 ..."（>=3 点）。
    makeStringParam(QStringLiteral("maskRegionPolygon"), QString(),
                    QStringLiteral("屏蔽区多边形（多条用 ; 分隔）")),
    // VM「位置修正」（基本参数·图形类型）：基准点 = 模板 ROI 中心/角度，
    // 运行点来自 Fixture（按信息）或 5 个可订阅参数（按点/按坐标）。
    makeEnumParam(QStringLiteral("positionCorrectMode"), 0,
                  QStringList{QStringLiteral("不启用"), QStringLiteral("按信息"),
                              QStringLiteral("按点"), QStringLiteral("按坐标")},
                  QStringLiteral("位置修正方式")),
    makeStringParam(QStringLiteral("positionFixtureName"), QString(),
                    QStringLiteral("位置修正 Fixture 名（按信息）")),
    makeDoubleParam(QStringLiteral("positionRunX"), 0.0, -100000.0, 100000.0,
                    QStringLiteral("运行点X"), QStringLiteral("px")),
    makeDoubleParam(QStringLiteral("positionRunY"), 0.0, -100000.0, 100000.0,
                    QStringLiteral("运行点Y"), QStringLiteral("px")),
    makeDoubleParam(QStringLiteral("positionRunAngle"), 0.0, -180.0, 180.0,
                    QStringLiteral("运行点角度"), QStringLiteral("°")),
    makeDoubleParam(QStringLiteral("positionRunScaleX"), 1.0, 0.1, 10.0,
                    QStringLiteral("运行点X尺度")),
    makeDoubleParam(QStringLiteral("positionRunScaleY"), 1.0, 0.1, 10.0,
                    QStringLiteral("运行点Y尺度")),
});

    // VM 4.4 口径分组（对应工具窗四个页签）。此前所有参数都落在默认的「参数」一组里，
    // 一长条平铺下来就是"看着乱"的根源；分组后每个 QGroupBox 归位到对应页签。
    for (auto &s : m_paramSpecs) {
        const QString &n = s.name;
        if (n == QLatin1String("templatePath") || n == QLatin1String("trainFromImage")
            || n == QLatin1String("method"))
            s.group = QStringLiteral("特征模板");   // VM：模板源/匹配方式属于模板页，不在基本参数

        else if (n == QLatin1String("outputMask") || n == QLatin1String("outputSearchMask")
                 || n == QLatin1String("roiType")
                 || n == QLatin1String("roiSource")
                 || n == QLatin1String("maskSource")
                 || n == QLatin1String("maskRegionPolygon")
                 || n == QLatin1String("positionFixtureName")
                 || n.startsWith(QLatin1String("positionCorrect"))
                 || n.startsWith(QLatin1String("positionRun"))
                 || n.startsWith(QLatin1String("roiInherit"))
                 || n.startsWith(QLatin1String("searchRegion")))
            s.group = QStringLiteral("ROI区域");
        else if (n == QLatin1String("scaleMode") || n == QLatin1String("contrastThreshold")
                 || n == QLatin1String("autoContrastThreshold")
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

    // VM 4.4「运行参数」页字段顺序（帮助文档 GUID-3EEB6D91「参数配置」）：
    // 全部搜索模式(+多模板重叠率) → 最小匹配分数 → 最大匹配个数 → 匹配极性 →
    // 角度范围 → 尺度范围类型/尺度 → 最大重叠率 → 排序类型 → 阈值类型 →
    // 延拓阈值 → 超时控制。只调整 UI 顺序，参数名/默认值/存盘格式不变。
    {
        static const QStringList kRunOrder = {
            QStringLiteral("matchAllTemplates"), QStringLiteral("maxOverlap"),
            QStringLiteral("multiTemplateOverlap"),
            QStringLiteral("minScore"), QStringLiteral("maxMatches"),
            QStringLiteral("polarity"),
            QStringLiteral("angleMin"), QStringLiteral("angleMax"),
            QStringLiteral("scaleRangeType"),
            QStringLiteral("scaleMin"), QStringLiteral("scaleMax"), QStringLiteral("scaleStep"),
            QStringLiteral("scaleMinX"), QStringLiteral("scaleMaxX"),
            QStringLiteral("scaleMinY"), QStringLiteral("scaleMaxY"),
            QStringLiteral("crossAngleMin"), QStringLiteral("crossAngleMax"),
            QStringLiteral("sortType"),
            QStringLiteral("thresholdType"), QStringLiteral("matchContrastThreshold"),
            QStringLiteral("considerNoise"),
            QStringLiteral("extendThreshold"), QStringLiteral("timeoutMs"),
            QStringLiteral("getContour"), QStringLiteral("outputContour"),
            QStringLiteral("writeFixtureName"),
        };
        QVector<ParamSpec> runSpecs;
        QVector<int> runPositions;
        for (int i = 0; i < m_paramSpecs.size(); ++i) {
            if (m_paramSpecs.at(i).group == QStringLiteral("运行参数")) {
                runSpecs.append(m_paramSpecs.at(i));
                runPositions.append(i);
            }
        }
        std::stable_sort(runSpecs.begin(), runSpecs.end(),
                         [](const ParamSpec &a, const ParamSpec &b) {
                             const int ra = kRunOrder.indexOf(a.name);
                             const int rb = kRunOrder.indexOf(b.name);
                             return (ra < 0 ? 999 : ra) < (rb < 0 ? 999 : rb);
                         });
        for (int i = 0; i < runPositions.size(); ++i)
            m_paramSpecs[runPositions.at(i)] = runSpecs.at(i);
    }

    // VM「基本参数 → ROI区域」字段顺序：输出掩膜 → ROI区域类型 → ROI创建 → 检测区域。
    {
        static const QStringList kRoiOrder = {
            QStringLiteral("outputMask"), QStringLiteral("roiType"), QStringLiteral("roiSource"),
            QStringLiteral("outputSearchMask"),
            QStringLiteral("searchRegionCol"), QStringLiteral("searchRegionRow"),
            QStringLiteral("searchRegionWidth"), QStringLiteral("searchRegionHeight"),
            QStringLiteral("searchRegionPolygon"),
        };
        QVector<ParamSpec> roiSpecs;
        QVector<int> roiPositions;
        for (int i = 0; i < m_paramSpecs.size(); ++i) {
            if (m_paramSpecs.at(i).group == QStringLiteral("ROI区域")) {
                roiSpecs.append(m_paramSpecs.at(i));
                roiPositions.append(i);
            }
        }
        std::stable_sort(roiSpecs.begin(), roiSpecs.end(),
                         [](const ParamSpec &a, const ParamSpec &b) {
                             const int ra = kRoiOrder.indexOf(a.name);
                             const int rb = kRoiOrder.indexOf(b.name);
                             return (ra < 0 ? 999 : ra) < (rb < 0 ? 999 : rb);
                         });
        for (int i = 0; i < roiPositions.size(); ++i)
            m_paramSpecs[roiPositions.at(i)] = roiSpecs.at(i);
    }

    // VM 参数名一律短标签；长解释移到 tooltip（面板不再被括号说明挤得松散）。
    {
        static const QMap<QString, QPair<QString, QString>> kLabelTips = {
            {QStringLiteral("matchAllTemplates"), {QStringLiteral("多模板同时匹配"),
                QStringLiteral("模板列表逐个跑完整流程，取最高分")}},
            {QStringLiteral("maxOverlap"), {QStringLiteral("最大重叠率"),
                QStringLiteral("0~100；两个命中的框重叠超过该值时只保留分数高的")}},
            {QStringLiteral("multiTemplateOverlap"), {QStringLiteral("多模板重叠率"),
                QStringLiteral("0~100；全部搜索模式下不同模板命中的目标重叠超此值时只保留高分者")}},
            {QStringLiteral("minScore"), {QStringLiteral("最小匹配分数"),
                QStringLiteral("分数低于此值的命中判为无效（0~1）")}},
            {QStringLiteral("maxMatches"), {QStringLiteral("最大匹配个数"),
                QStringLiteral("1=只输出最优；多件场景调大")}},
            {QStringLiteral("polarity"), {QStringLiteral("匹配极性"),
                QStringLiteral("1=考虑极性（默认）；0=忽略，亮标/暗标反色也能匹配")}},
            {QStringLiteral("angleMin"), {QStringLiteral("搜索角度下限"),
                QStringLiteral("模板允许的最小旋转角")}},
            {QStringLiteral("angleMax"), {QStringLiteral("搜索角度上限"),
                QStringLiteral("模板允许的最大旋转角")}},
            {QStringLiteral("angleStep"), {QStringLiteral("搜索角度步进"),
                QStringLiteral("角度采样间隔，越小越慢")}},
            {QStringLiteral("scaleRangeType"), {QStringLiteral("尺度范围类型"),
                QStringLiteral("0=整体（上下限）；1=X/Y 独立")}},
            {QStringLiteral("scaleMin"), {QStringLiteral("搜索尺度下限"),
                QStringLiteral("整体尺度范围下限（1=原尺寸）")}},
            {QStringLiteral("scaleMax"), {QStringLiteral("搜索尺度上限"),
                QStringLiteral("整体尺度范围上限（1=原尺寸）")}},
            {QStringLiteral("scaleStep"), {QStringLiteral("搜索尺度步进"),
                QStringLiteral("尺度采样间隔，越小越慢")}},
            {QStringLiteral("scaleMinX"), {QStringLiteral("X 尺度下限"),
                QStringLiteral("尺度范围类型=1 时生效")}},
            {QStringLiteral("scaleMaxX"), {QStringLiteral("X 尺度上限"),
                QStringLiteral("尺度范围类型=1 时生效")}},
            {QStringLiteral("scaleMinY"), {QStringLiteral("Y 尺度下限"),
                QStringLiteral("尺度范围类型=1 时生效")}},
            {QStringLiteral("scaleMaxY"), {QStringLiteral("Y 尺度上限"),
                QStringLiteral("尺度范围类型=1 时生效")}},
            {QStringLiteral("crossAngleMin"), {QStringLiteral("横切角度下限"),
                QStringLiteral("尺度范围类型=1 时生效；目标 Y 轴向 X 轴倾斜的剪切角（-45~45°）")}},
            {QStringLiteral("crossAngleMax"), {QStringLiteral("横切角度上限"),
                QStringLiteral("尺度范围类型=1 时生效；目标 Y 轴向 X 轴倾斜的剪切角（-45~45°）")}},
            {QStringLiteral("sortType"), {QStringLiteral("排序类型"),
                QStringLiteral("0=分数降序 1=角度降序 2=X 升序 3=Y 升序")}},
            {QStringLiteral("thresholdType"), {QStringLiteral("阈值类型"),
                QStringLiteral("0=自动（按目标图像）1=模板 2=手动")}},
            {QStringLiteral("matchContrastThreshold"), {QStringLiteral("手动阈值"),
                QStringLiteral("灰度差 1~255；阈值类型=手动时使用")}},
            {QStringLiteral("considerNoise"), {QStringLiteral("是否考虑噪点"),
                QStringLiteral("开启后匹配图中不在边缘链上的特征点（毛刺）会降低评分；默认关闭")}},
            {QStringLiteral("extendThreshold"), {QStringLiteral("延拓阈值"),
                QStringLiteral("目标被图像边缘截断时允许缺失的特征比例（%）")}},
            {QStringLiteral("timeoutMs"), {QStringLiteral("超时控制"),
                QStringLiteral("毫秒；0=不限制")}},
            {QStringLiteral("getContour"), {QStringLiteral("是否获取轮廓"),
                QStringLiteral("开启则计算并显示模板轮廓特征点；关闭只显示匹配框，可降低匹配耗时")}},
            {QStringLiteral("outputContour"), {QStringLiteral("是否输出轮廓"),
                QStringLiteral("打开则在模块结果中输出轮廓点（个数/X/Y）；关闭则不输出")}},
            {QStringLiteral("writeFixtureName"), {QStringLiteral("位置修正写入"),
                QStringLiteral("把本次匹配位置写给下游位置修正（空=不写）")}},
            {QStringLiteral("roiWidth"), {QStringLiteral("模板框宽"),
                QStringLiteral("0=未设置；在画布上框选后自动回填")}},
            {QStringLiteral("roiHeight"), {QStringLiteral("模板框高"),
                QStringLiteral("0=未设置；在画布上框选后自动回填")}},
            {QStringLiteral("roiCenterCol"), {QStringLiteral("模板框中心X"),
                QStringLiteral("旋转框中心的图像列坐标")}},
            {QStringLiteral("roiCenterRow"), {QStringLiteral("模板框中心Y"),
                QStringLiteral("旋转框中心的图像行坐标")}},
            {QStringLiteral("roiAngle"), {QStringLiteral("模板框角度"),
                QStringLiteral("旋转框角度（度，逆时针为正）")}},
            {QStringLiteral("searchRegionCol"), {QStringLiteral("搜索区域左列"),
                QStringLiteral("检测区域左上角 X；右侧准星按钮可在图上单击取点")}},
            {QStringLiteral("searchRegionRow"), {QStringLiteral("搜索区域上行"),
                QStringLiteral("检测区域左上角 Y；右侧准星按钮可在图上单击取点")}},
            {QStringLiteral("searchRegionWidth"), {QStringLiteral("搜索区域宽度"),
                QStringLiteral("检测区域宽度；右侧准星按钮单击处 X − 左列")}},
            {QStringLiteral("searchRegionHeight"), {QStringLiteral("搜索区域高度"),
                QStringLiteral("检测区域高度；右侧准星按钮单击处 Y − 上行")}},
            {QStringLiteral("searchRegionPolygon"), {QStringLiteral("搜索区域多边形"),
                QStringLiteral("空=按矩形；填 \"x,y x,y ...\" 时多边形优先")}},
        };
        for (auto &s : m_paramSpecs) {
            const auto it = kLabelTips.constFind(s.name);
            if (it != kLabelTips.constEnd()) {
                s.label = it->first;
                s.tooltip = it->second;
            }
        }
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
    m_params[QStringLiteral("matchCrossAngle")] = 0.0;
    m_params[QStringLiteral("matchTemplateIndex")] = -1;
    m_params[QStringLiteral("matchTemplateName")] = QString();
    m_params[QStringLiteral("trainStatus")] = QString();
    // ── VM「模块结果」展示用（结果字段，不上面板）──
    setParamDirect(QStringLiteral("elapsedMs"), 0.0);
    // 内部读数（不上结果表）：本轮实际生效的速度尺度与粗搜倍率，用于区分自动/手动。
    setParamDirect(QStringLiteral("matchSpeedScale"), 0);
    setParamDirect(QStringLiteral("matchCoarseFactor"), 0.0);
    setParamDirect(QStringLiteral("matchStatus"), 0);
    setParamDirect(QStringLiteral("matchBoxCenters"), QString());
    setParamDirect(QStringLiteral("matchBoxCenterXs"), QString());
    setParamDirect(QStringLiteral("matchBoxCenterYs"), QString());
    setParamDirect(QStringLiteral("matchBoxWidths"), QString());
    setParamDirect(QStringLiteral("matchBoxHeights"), QString());
    setParamDirect(QStringLiteral("matchBoxAngles"), QString());
    setParamDirect(QStringLiteral("matchPoints"), QString());
    setParamDirect(QStringLiteral("matchPointXs"), QString());
    setParamDirect(QStringLiteral("matchPointYs"), QString());
    setParamDirect(QStringLiteral("matchScales"), QString());
    setParamDirect(QStringLiteral("matchScaleXs"), QString());
    setParamDirect(QStringLiteral("matchScaleYs"), QString());
    setParamDirect(QStringLiteral("matchCrossAngles"), QString());
    setParamDirect(QStringLiteral("matchScores"), QString());
    setParamDirect(QStringLiteral("matchContourCount"), 0);
    setParamDirect(QStringLiteral("matchContourPointXs"), QString());
    setParamDirect(QStringLiteral("matchContourPointYs"), QString());
    setParamDirect(QStringLiteral("searchCenterX"), 0.0);
    setParamDirect(QStringLiteral("searchCenterY"), 0.0);
    setParamDirect(QStringLiteral("searchWidth"), 0.0);
    setParamDirect(QStringLiteral("searchHeight"), 0.0);
    setParamDirect(QStringLiteral("searchAngle"), 0.0);
    // VM「模块结果 → 屏蔽区多边形 / 输出掩膜」：每轮由 runSingle 重写
    setParamDirect(QStringLiteral("maskPolygonCount"), 0);
    setParamDirect(QStringLiteral("maskPolygonPoints"), QString());
    setParamDirect(QStringLiteral("maskPolygonXs"), QString());
    setParamDirect(QStringLiteral("maskPolygonYs"), QString());
    setParamDirect(QStringLiteral("outMaskWidth"), 0);
    setParamDirect(QStringLiteral("outMaskHeight"), 0);
    setParamDirect(QStringLiteral("outMaskPixelFormat"), 0);
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

bool OpencvTemplateMatchNode::resultJudgmentOk(QString *reason) const
{
    // VM「结果显示 → 结果判断」固定条件。字段名与模块窗控件一致：
    // resJudge<Key>Enable / resJudge<Key>Low / resJudge<Key>High。
    auto enabled = [this](const QString &key) {
        return m_params.value(QStringLiteral("resJudge%1Enable").arg(key), false).toBool();
    };
    auto low = [this](const QString &key, double def) {
        return m_params.value(QStringLiteral("resJudge%1Low").arg(key), def).toDouble();
    };
    auto high = [this](const QString &key, double def) {
        return m_params.value(QStringLiteral("resJudge%1High").arg(key), def).toDouble();
    };

    // VM「无目标时判定 OK」：数量为 0 直接通过（不再判其它条件）
    if (m_params.value(QStringLiteral("resJudgeOkWhenZero"), false).toBool()
        && m_params.value(QStringLiteral("matchCount"), 0).toInt() == 0) {
        return true;
    }

    auto checkRange = [&](const QString &key, const QString &label, double value,
                          double defLow, double defHigh) {
        if (!enabled(key))
            return true;
        const double lo = low(key, defLow);
        const double hi = high(key, defHigh);
        if (value < lo - 1e-9 || value > hi + 1e-9) {
            if (reason) {
                *reason = QStringLiteral("%1 %2 超出判定范围 [%3, %4]")
                              .arg(label)
                              .arg(value, 0, 'f', 3)
                              .arg(lo, 0, 'f', 3)
                              .arg(hi, 0, 'f', 3);
            }
            return false;
        }
        return true;
    };

    if (!checkRange(QStringLiteral("Num"), QStringLiteral("匹配数量"),
                    m_params.value(QStringLiteral("matchCount"), 0).toInt(), 1.0, 1000.0))
        return false;
    if (!checkRange(QStringLiteral("Angle"), QStringLiteral("角度"),
                    m_params.value(QStringLiteral("matchAngle"), 0.0).toDouble(), -180.0, 180.0))
        return false;
    if (!checkRange(QStringLiteral("Scale"), QStringLiteral("尺度"),
                    m_params.value(QStringLiteral("matchScale"), 0.0).toDouble(), 0.0, 10.0))
        return false;
    if (!checkRange(QStringLiteral("ScaleX"), QStringLiteral("尺度X"),
                    m_params.value(QStringLiteral("matchScaleX"), 0.0).toDouble(), 0.0, 10.0))
        return false;
    if (!checkRange(QStringLiteral("ScaleY"), QStringLiteral("尺度Y"),
                    m_params.value(QStringLiteral("matchScaleY"), 0.0).toDouble(), 0.0, 10.0))
        return false;
    if (!checkRange(QStringLiteral("Score"), QStringLiteral("匹配分数"),
                    m_params.value(QStringLiteral("matchScore"), 0.0).toDouble(), 0.0, 1.0))
        return false;
    if (!checkRange(QStringLiteral("PointX"), QStringLiteral("匹配点X"),
                    m_params.value(QStringLiteral("matchPointCol"), 0.0).toDouble(), 0.0, 100000.0))
        return false;
    if (!checkRange(QStringLiteral("PointY"), QStringLiteral("匹配点Y"),
                    m_params.value(QStringLiteral("matchPointRow"), 0.0).toDouble(), 0.0, 100000.0))
        return false;
    if (!checkRange(QStringLiteral("BoxX"), QStringLiteral("匹配框中心X"),
                    m_params.value(QStringLiteral("matchCol"), 0.0).toDouble(), 0.0, 100000.0))
        return false;
    if (!checkRange(QStringLiteral("BoxY"), QStringLiteral("匹配框中心Y"),
                    m_params.value(QStringLiteral("matchRow"), 0.0).toDouble(), 0.0, 100000.0))
        return false;
    return true;
}

void OpencvTemplateMatchNode::run(bool autoSwitch)
{
    // 多模板同时匹配（VM 的「特征模板」页语义）：逐个模板跑完整流程，取分数最高者。
    // 为什么要重跑一次：单模板流程末尾会把结果写成"本次命中"，直接汇总中间态很容易
    // 漏掉叠加/Measurement 的字段；用最优模板再跑一遍，所有产出与单模板路径完全一致。
    // 代价：N 个模板 ≈ N 倍匹配时间（2~3 个模板在 100~500ms 量级，可接受）。
    const QVector<TemplateEntry> entries = templates();
    const bool train = m_params.value(QStringLiteral("trainFromImage"), false).toBool();
    // VM「勾选模板」：只有勾选的模板参与匹配（旧工程无 enabled 键 → 默认勾选）。
    // 排序（上移/下移）直接改列表顺序；未开全部搜索模式时按该顺序逐个试。
    QVector<int> enabledIndices;
    for (int i = 0; i < entries.size(); ++i) {
        // 勾选只约束匹配；「训练模板」是创建模板的动作，训练模式下全部模板都参与（保持旧行为）。
        if (entries.at(i).enabled || train)
            enabledIndices.append(i);
    }
    // 逐目标结果每个模板开始前必须清空：runSingle 的早退路径（模板缺失/尺寸无效）
    // 不会写这些键，不清就会把上一个模板的命中误算到本模板名下。
    auto resetMatchFields = [&]() {
        static const char *keys[] = {
            "matchList", "matchBoxCenters", "matchBoxCenterXs", "matchBoxCenterYs",
            "matchBoxWidths", "matchBoxHeights", "matchBoxAngles", "matchPoints",
            "matchPointXs", "matchPointYs", "matchScales", "matchScaleXs", "matchScaleYs",
            "matchCrossAngles", "matchScores", "matchContourPointXs", "matchContourPointYs",
            "matchTemplateName"};
        for (const char *key : keys)
            m_params[QString::fromLatin1(key)] = QString();
        m_params[QStringLiteral("matchCount")] = 0;
        m_params[QStringLiteral("matchCountBeforeOverlap")] = 0;
        m_params[QStringLiteral("matchStatus")] = 0;
        m_params[QStringLiteral("matchRow")] = 0.0;
        m_params[QStringLiteral("matchCol")] = 0.0;
        m_params[QStringLiteral("matchScore")] = 0.0;
        m_params[QStringLiteral("matchAngle")] = 0.0;
        m_params[QStringLiteral("matchScale")] = 1.0;
        m_params[QStringLiteral("matchScaleX")] = 1.0;
        m_params[QStringLiteral("matchScaleY")] = 1.0;
        m_params[QStringLiteral("matchCrossAngle")] = 0.0;
        m_params[QStringLiteral("matchPointRow")] = 0.0;
        m_params[QStringLiteral("matchPointCol")] = 0.0;
        m_params[QStringLiteral("matchContourCount")] = 0;
        m_params[QStringLiteral("matchTemplateIndex")] = -1;
    };
    if (!entries.isEmpty() && enabledIndices.isEmpty()) {
        resetMatchFields();
        m_params[QStringLiteral("lastError")] =
            QStringLiteral("没有勾选任何模板：先在「特征模板」页勾选至少一个模板");
        m_params[QStringLiteral("moduleStatus")] = false;
        m_outputImage = m_inputImage;
        setOutputData(1, QSharedPointer<DataObject>());
        return;
    }
    const bool matchAll =
        enabledIndices.size() > 1 && !train
        && m_params.value(QStringLiteral("matchAllTemplates"), false).toBool();
    if (!matchAll) {
        // VM「全部搜索模式=关」：按模板列表顺序逐个试（跳过未勾选的），达标即停。
        if (entries.isEmpty()) {
            runSingle(autoSwitch);
            return;
        }
        const int savedIndex = activeTemplateIndex();
        for (int i : enabledIndices) {
            m_params[QStringLiteral("templateIndex")] = i;
            runSingle(autoSwitch);
            if (m_params.value(QStringLiteral("moduleStatus"), false).toBool()) {
                m_params[QStringLiteral("matchTemplateIndex")] = i;
                m_params[QStringLiteral("matchTemplateName")] = entries.at(i).name;
                return;                          // 达标即停
            }
        }
        // 全部模板都没命中
        m_params[QStringLiteral("templateIndex")] = savedIndex;
        m_params[QStringLiteral("matchTemplateIndex")] = -1;
        m_params[QStringLiteral("matchTemplateName")] = QString();
        return;
    }

    const int savedIndex = activeTemplateIndex();
    const double multiMinScore = m_params.value(QStringLiteral("minScore"), 0.5).toDouble();

    // P0-2（VM「全部搜索模式」）：逐个模板跑完整流程，收集每个模板的命中，
    // 再按「多模板重叠率」合并、按排序类型重排、按最大匹配个数截断。
    // 逐目标结构：matchList = row,col,score,angle,scale,pointRow,pointCol；
    // 宽/高、尺度 X/Y、横切值各有同序列表。
    struct MultiHit {
        double row = 0.0, col = 0.0, score = 0.0, angle = 0.0;
        double scaleX = 1.0, scaleY = 1.0, cross = 0.0;
        double pointRow = 0.0, pointCol = 0.0;
        double width = 0.0, height = 0.0;
        int templateIndex = -1;
        QString templateName;
    };
    auto listOf = [](const QVariant &v) {
        return v.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    };
    auto captureHits = [&](int tmplIdx, const QString &tmplName) {
        QVector<MultiHit> out;
        const QStringList rows = m_params.value(QStringLiteral("matchList"))
                                     .toString()
                                     .split(QLatin1Char(';'), Qt::SkipEmptyParts);
        if (rows.isEmpty())
            return out;
        const QStringList widths = listOf(m_params.value(QStringLiteral("matchBoxWidths")));
        const QStringList heights = listOf(m_params.value(QStringLiteral("matchBoxHeights")));
        const QStringList scaleXs = listOf(m_params.value(QStringLiteral("matchScaleXs")));
        const QStringList scaleYs = listOf(m_params.value(QStringLiteral("matchScaleYs")));
        const QStringList crosses = listOf(m_params.value(QStringLiteral("matchCrossAngles")));
        for (int k = 0; k < rows.size(); ++k) {
            const QStringList f = rows.at(k).split(QLatin1Char(','));
            if (f.size() < 3)
                continue;
            MultiHit h;
            h.row = f[0].toDouble();
            h.col = f[1].toDouble();
            h.score = f[2].toDouble();
            // 首实例低于阈值时也会留在 matchList（单模板路径的既有行为），合并时要滤掉。
            if (h.score < multiMinScore)
                continue;
            h.angle = f.size() >= 5 ? f[3].toDouble() : 0.0;
            h.scaleX = k < scaleXs.size() ? scaleXs.at(k).toDouble()
                                          : (f.size() >= 5 ? f[4].toDouble() : 1.0);
            h.scaleY = k < scaleYs.size() ? scaleYs.at(k).toDouble() : h.scaleX;
            h.cross = k < crosses.size() ? crosses.at(k).toDouble() : 0.0;
            h.pointRow = f.size() >= 7 ? f[5].toDouble() : h.row;
            h.pointCol = f.size() >= 7 ? f[6].toDouble() : h.col;
            h.width = k < widths.size() ? widths.at(k).toDouble() : 0.0;
            h.height = k < heights.size() ? heights.at(k).toDouble() : 0.0;
            h.templateIndex = tmplIdx;
            h.templateName = tmplName;
            out.append(h);
        }
        return out;
    };

    QVector<MultiHit> allHits;
    double bestScore = -1e9;
    int bestIdx = -1;
    for (int i : enabledIndices) {
        m_params[QStringLiteral("templateIndex")] = i;
        resetMatchFields();
        runSingle(autoSwitch);
        const double score = m_params.value(QStringLiteral("matchScore"), 0.0).toDouble();
        if (m_params.value(QStringLiteral("moduleStatus"), false).toBool() && score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
        // moduleStatus 可能因角度/尺度判断失败而为假，但目标本身有效；
        // 最终判定在合并后的结果上重做，所以这里按分数阈值收命中。
        allHits += captureHits(i, entries.at(i).name);
    }

    if (bestIdx >= 0) {
        m_params[QStringLiteral("templateIndex")] = bestIdx;
        resetMatchFields();
        runSingle(autoSwitch);   // 最优模板重跑：主结果/掩膜/轮廓以它为基面
        QVector<MultiHit> merged;
        for (const MultiHit &h : allHits) {
            if (h.templateIndex != bestIdx)   // 最优模板的旧捕获由重跑结果替代
                merged.append(h);
        }
        merged += captureHits(bestIdx, entries.at(bestIdx).name);
        const int beforeMultiOverlap = merged.size();

        // VM「多模板重叠率」：不同模板命中的目标重叠率超过阈值时只保留高分者。
        // 同模板内的重叠已由「最大重叠率」在 runSingle 内处理。
        const double multiOverlap =
            qBound(0, m_params.value(QStringLiteral("multiTemplateOverlap"), 50).toInt(), 100)
            / 100.0;
        if (multiOverlap < 1.0 && merged.size() > 1) {
            std::stable_sort(merged.begin(), merged.end(),
                             [](const MultiHit &a, const MultiHit &b) { return a.score > b.score; });
            QVector<MultiHit> kept;
            for (const MultiHit &h : merged) {
                bool drop = false;
                for (const MultiHit &k : kept) {
                    if (k.templateIndex == h.templateIndex)
                        continue;
                    const double hw = qMax(4.0, h.width);
                    const double hh = qMax(4.0, h.height);
                    const double kw = qMax(4.0, k.width);
                    const double kh = qMax(4.0, k.height);
                    const double interW = qMax(0.0, (hw + kw) * 0.5 - qAbs(h.col - k.col));
                    const double interH = qMax(0.0, (hh + kh) * 0.5 - qAbs(h.row - k.row));
                    const double inter = interW * interH;
                    const double minArea = qMin(hw * hh, kw * kh);
                    if (minArea > 1.0 && inter / minArea > multiOverlap) {
                        drop = true;
                        break;
                    }
                }
                if (!drop)
                    kept.append(h);
            }
            merged = kept;
        }

        // 排序类型（与单模板结果同口径；并列规则见 TemplateMatchSort）
        const int sortType = qBound(0, m_params.value(QStringLiteral("sortType"), 0).toInt(), 5);
        TemplateMatchSort::stableSortByType(merged, sortType);
        // VM「最大匹配个数」：合并后的总数也要按上限截断。
        const int maxMatches =
            qBound(1, m_params.value(QStringLiteral("maxMatches"), 1).toInt(), 1000);
        if (merged.size() > maxMatches)
            merged.resize(maxMatches);

        if (!merged.isEmpty()) {
            QStringList matchList, centers, centerXs, centerYs, widths, heights, angles;
            QStringList points, pointXs, pointYs, scales, scaleXs, scaleYs, scores, crosses;
            for (const MultiHit &h : merged) {
                matchList << QStringLiteral("%1,%2,%3,%4,%5,%6,%7")
                                 .arg(h.row, 0, 'f', 1)
                                 .arg(h.col, 0, 'f', 1)
                                 .arg(h.score, 0, 'f', 3)
                                 .arg(h.angle, 0, 'f', 2)
                                 .arg(h.scaleX, 0, 'f', 4)
                                 .arg(h.pointRow, 0, 'f', 1)
                                 .arg(h.pointCol, 0, 'f', 1);
                centers << QStringLiteral("%1,%2")
                               .arg(h.col, 0, 'f', 2).arg(h.row, 0, 'f', 2);
                centerXs << QString::number(h.col, 'f', 2);
                centerYs << QString::number(h.row, 'f', 2);
                widths << QString::number(h.width, 'f', 2);
                heights << QString::number(h.height, 'f', 2);
                angles << QString::number(h.angle, 'f', 2);
                points << QStringLiteral("%1,%2")
                              .arg(h.pointCol, 0, 'f', 2).arg(h.pointRow, 0, 'f', 2);
                pointXs << QString::number(h.pointCol, 'f', 2);
                pointYs << QString::number(h.pointRow, 'f', 2);
                scales << QString::number(h.scaleX, 'f', 4);
                scaleXs << QString::number(h.scaleX, 'f', 4);
                scaleYs << QString::number(h.scaleY, 'f', 4);
                scores << QString::number(h.score, 'f', 4);
                crosses << QString::number(h.cross, 'f', 2);
            }
            m_params[QStringLiteral("matchList")] = matchList.join(QLatin1Char(';'));
            m_params[QStringLiteral("matchBoxCenters")] = centers.join(QLatin1Char(';'));
            m_params[QStringLiteral("matchBoxCenterXs")] = centerXs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchBoxCenterYs")] = centerYs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchBoxWidths")] = widths.join(QLatin1Char(','));
            m_params[QStringLiteral("matchBoxHeights")] = heights.join(QLatin1Char(','));
            m_params[QStringLiteral("matchBoxAngles")] = angles.join(QLatin1Char(','));
            m_params[QStringLiteral("matchPoints")] = points.join(QLatin1Char(';'));
            m_params[QStringLiteral("matchPointXs")] = pointXs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchPointYs")] = pointYs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchScales")] = scales.join(QLatin1Char(','));
            m_params[QStringLiteral("matchScaleXs")] = scaleXs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchScaleYs")] = scaleYs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchCrossAngles")] = crosses.join(QLatin1Char(','));
            m_params[QStringLiteral("matchScores")] = scores.join(QLatin1Char(','));
            m_params[QStringLiteral("matchCount")] = merged.size();
            m_params[QStringLiteral("matchCountBeforeOverlap")] = beforeMultiOverlap;

            const MultiHit &mainHit = merged.first();
            m_params[QStringLiteral("matchRow")] = mainHit.row;
            m_params[QStringLiteral("matchCol")] = mainHit.col;
            m_params[QStringLiteral("matchScore")] = mainHit.score;
            m_params[QStringLiteral("matchAngle")] = mainHit.angle;
            m_params[QStringLiteral("matchScale")] = mainHit.scaleX;
            m_params[QStringLiteral("matchScaleX")] = mainHit.scaleX;
            m_params[QStringLiteral("matchScaleY")] = mainHit.scaleY;
            m_params[QStringLiteral("matchCrossAngle")] = mainHit.cross;
            m_params[QStringLiteral("matchPointRow")] = mainHit.pointRow;
            m_params[QStringLiteral("matchPointCol")] = mainHit.pointCol;
            m_params[QStringLiteral("matchTemplateIndex")] = mainHit.templateIndex;
            m_params[QStringLiteral("matchTemplateName")] = mainHit.templateName;
            m_params[QStringLiteral("matchStatus")] = 1;

            // 主结果对象按合并后的第一目标重建；掩膜等具名子结果从重跑对象上原样搬过来。
            const QSharedPointer<DataObject> baseObj = getOutputData(1);
            QVector<MeasureResult> baseResults;
            QVector<QPair<QString, QSharedPointer<DataObject>>> baseAttached;
            if (baseObj) {
                baseResults = baseObj->measureResults();
                baseAttached = baseObj->attachedResults();
            }
            double baseRow = 1e18, baseCol = 1e18;
            if (!baseResults.isEmpty() && baseResults.first().extraValues.size() >= 2) {
                baseRow = baseResults.first().extraValues[0];
                baseCol = baseResults.first().extraValues[1];
            }
            const bool contourMatchesMain =
                mainHit.templateIndex == bestIdx && qAbs(mainHit.row - baseRow) < 0.5
                && qAbs(mainHit.col - baseCol) < 0.5;
            MeasureResult mainRes;
            mainRes.type = QStringLiteral("template");
            mainRes.valueName = QStringLiteral("匹配得分");
            mainRes.valid = (mainHit.score >= multiMinScore);
            mainRes.value = mainHit.score;
            mainRes.point1 = QPointF(mainHit.col, mainHit.row);
            mainRes.extraValues = QVector<double>(
                {mainHit.row, mainHit.col, mainHit.score, mainHit.angle, mainHit.scaleX,
                 mainHit.width, mainHit.height, mainHit.pointRow, mainHit.pointCol,
                 mainHit.scaleX, mainHit.scaleY, mainHit.cross});
            auto mergedObj = QSharedPointer<DataObject>::create();
            mergedObj->setMeasureResult(mainRes);
            // 轮廓点集只代表「重跑模板 + 重跑主命中」；合并后主目标变了就作废。
            if (contourMatchesMain) {
                for (const MeasureResult &r : baseResults) {
                    if (r.type == QLatin1String("contour"))
                        mergedObj->attachMeasureResult(r);
                }
            }
            for (const auto &att : baseAttached)
                mergedObj->attachResult(att.first, att.second);
            setOutputData(1, mergedObj);

            // 结果判断基于合并后的 count/主结果重新跑一遍
            bool judgedOk = (mainHit.score >= multiMinScore);
            if (judgedOk) {
                QString reason;
                if (!resultJudgmentOk(&reason)) {
                    judgedOk = false;
                    if (!reason.isEmpty())
                        m_params[QStringLiteral("lastError")] = reason;
                } else {
                    m_params[QStringLiteral("lastError")] = QString();
                }
            }
            m_params[QStringLiteral("moduleStatus")] = judgedOk;
            setResultNg(!judgedOk);
            if (!contourMatchesMain) {
                m_params[QStringLiteral("matchContourCount")] = 0;
                m_params[QStringLiteral("matchContourPointXs")] = QString();
                m_params[QStringLiteral("matchContourPointYs")] = QString();
            }
            return;
        }
    }

    // 全部模板都没命中
    m_params[QStringLiteral("templateIndex")] = savedIndex;
    m_params[QStringLiteral("matchTemplateIndex")] = -1;
    m_params[QStringLiteral("matchTemplateName")] = QString();
    m_params[QStringLiteral("moduleStatus")] = false;   // 失败原因保留最后一次模板的 lastError
}

void OpencvTemplateMatchNode::runSingle(bool /*autoSwitch*/)
{
    QElapsedTimer runTimer;   // 官方模块结果「耗时（ms）」
    runTimer.start();
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
        // ROI 继承（VM「ROI 创建 = 继承」）：继承方式 = 按区域 / 按参数。
        //   按区域：上游模板/特征命中框直接当搜索区域（从输入对象 measureResults 取）；
        //   按参数：用 roiInheritX/Y/Width/Height/Angle（可在「参数订阅」里逐项订阅上游）；
        // 数据面取（不依赖场景图）：上游结果就在输入对象的 measureResults 里。
        int inheritCol = 0, inheritRow = 0, inheritW = 0, inheritH = 0;
        const int roiSource = m_params.value(QStringLiteral("roiSource"), 0).toInt();
        if (roiSource == 1) {
            const int inheritMode =
                qBound(0, m_params.value(QStringLiteral("roiInheritMode"), 0).toInt(), 1);
            if (inheritMode == 1) {
                const double cx = m_params.value(QStringLiteral("roiInheritX"), 0.0).toDouble();
                const double cy = m_params.value(QStringLiteral("roiInheritY"), 0.0).toDouble();
                const double bw =
                    m_params.value(QStringLiteral("roiInheritWidth"), 0.0).toDouble();
                const double bh =
                    m_params.value(QStringLiteral("roiInheritHeight"), 0.0).toDouble();
                const double angleDeg =
                    m_params.value(QStringLiteral("roiInheritAngle"), 0.0).toDouble();
                if (bw > 2.0 && bh > 2.0) {
                    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
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
                    inheritCol = int(x0);
                    inheritRow = int(y0);
                    inheritW = qMax(1, int(x1 - x0) + 1);
                    inheritH = qMax(1, int(y1 - y0) + 1);
                }
            } else {
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
        }

        // VM「位置修正」：基准点取模板 ROI 中心/角度，运行点取 Fixture（按信息）
        // 或 positionRunX/Y/Angle/ScaleX/Y（按点、按坐标）。只作用于「绘制」的检测区域：
        // 继承区域本就来自当前图，再叠加会双重修正。
        bool positionCorrect = false;
        double pcBaseX = 0.0, pcBaseY = 0.0, pcBaseAngle = 0.0;
        double pcRunX = 0.0, pcRunY = 0.0, pcRunAngle = 0.0;
        double pcScaleX = 1.0, pcScaleY = 1.0;
        const int pcMode = qBound(0, m_params.value(QStringLiteral("positionCorrectMode"), 0).toInt(), 3);
        if (pcMode != 0 && roiSource == 0) {
            pcBaseX = m_params.value(QStringLiteral("roiCenterCol"), 0.0).toDouble();
            pcBaseY = m_params.value(QStringLiteral("roiCenterRow"), 0.0).toDouble();
            pcBaseAngle = m_params.value(QStringLiteral("roiAngle"), 0.0).toDouble();
            if (pcMode == 1) {
                const QString fxName =
                    m_params.value(QStringLiteral("positionFixtureName")).toString().trimmed();
                FlowScene *fs = flowSceneRef();
                const FlowFixture fx =
                    (fs && !fxName.isEmpty()) ? fs->fixture(fxName) : FlowFixture();
                if (fxName.isEmpty() || !fx.hasPose) {
                    m_params[QStringLiteral("lastError")] = QStringLiteral(
                        "位置修正=按信息，但 Fixture「%1」不存在或无匹配位姿（先跑前序匹配写入 Fixture）")
                        .arg(fxName);
                    m_params[QStringLiteral("moduleStatus")] = false;
                    m_outputImage = m_inputImage;
                    setOutputData(1, QSharedPointer<DataObject>());
                    return;
                }
                pcRunX = fx.poseCol;
                pcRunY = fx.poseRow;
                pcRunAngle = fx.poseAngle;
                pcScaleX = pcScaleY = fx.poseScale;
            } else {
                pcRunX = m_params.value(QStringLiteral("positionRunX"), 0.0).toDouble();
                pcRunY = m_params.value(QStringLiteral("positionRunY"), 0.0).toDouble();
                pcRunAngle = m_params.value(QStringLiteral("positionRunAngle"), 0.0).toDouble();
                pcScaleX = m_params.value(QStringLiteral("positionRunScaleX"), 1.0).toDouble();
                pcScaleY = m_params.value(QStringLiteral("positionRunScaleY"), 1.0).toDouble();
            }
            pcScaleX = qBound(0.01, pcScaleX, 100.0);
            pcScaleY = qBound(0.01, pcScaleY, 100.0);
            positionCorrect = true;
        }
        // 把基准图坐标的点映射到运行图：先变尺、再旋转（角度差为转量）。
        auto pcMapPoint = [&](const QPointF &p) {
            const double rad =
                (pcRunAngle - pcBaseAngle) * 3.14159265358979323846 / 180.0;
            const double ca = std::cos(rad);
            const double sa = std::sin(rad);
            const double dx = (p.x() - pcBaseX) * pcScaleX;
            const double dy = (p.y() - pcBaseY) * pcScaleY;
            return QPointF(pcRunX + ca * dx - sa * dy, pcRunY + sa * dx + ca * dy);
        };

        cv::Rect searchRect(0, 0, gray.cols, gray.rows);
        bool explicitSearchRect = false;
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
                explicitSearchRect = true;
                searchRect = cv::Rect(qBound(0, rc, gray.cols - 1),
                                     qBound(0, rr, gray.rows - 1),
                                     qMin(rw, gray.cols),
                                     qMin(rh, gray.rows))
                                 & cv::Rect(0, 0, gray.cols, gray.rows);
                if (searchRect.width > 2 && searchRect.height > 2)
                    gray = gray(searchRect);
            }
        }

        // 位置修正（矩形检测区域）：把四个角变换到运行点，取轴对齐外接框。
        const QString polyTextPc =
            m_params.value(QStringLiteral("searchRegionPolygon")).toString().trimmed();
        if (positionCorrect && explicitSearchRect && polyTextPc.isEmpty()) {
            const double x0 = searchRect.x, y0 = searchRect.y;
            const double x1 = x0 + searchRect.width, y1 = y0 + searchRect.height;
            const QPointF corners[4] = {pcMapPoint(QPointF(x0, y0)),
                                        pcMapPoint(QPointF(x1, y0)),
                                        pcMapPoint(QPointF(x1, y1)),
                                        pcMapPoint(QPointF(x0, y1))};
            double bx0 = 1e18, bx1 = -1e18, by0 = 1e18, by1 = -1e18;
            for (const QPointF &c : corners) {
                bx0 = qMin(bx0, c.x());
                bx1 = qMax(bx1, c.x());
                by0 = qMin(by0, c.y());
                by1 = qMax(by1, c.y());
            }
            searchRect = cv::Rect(int(std::floor(bx0)), int(std::floor(by0)),
                                  qMax(1, int(std::ceil(bx1 - bx0))),
                                  qMax(1, int(std::ceil(by1 - by0))))
                         & cv::Rect(0, 0, grayFull.cols, grayFull.rows);
            if (searchRect.width > 2 && searchRect.height > 2)
                gray = grayFull(searchRect);
            else
                gray = cv::Mat();
        }
        // 位置修正 + 多边形：多边形坐标可能被搬到原矩形区域之外，
        // 因此先回到全图再裁。
        if (positionCorrect && !polyTextPc.isEmpty()) {
            searchRect = cv::Rect(0, 0, grayFull.cols, grayFull.rows);
            gray = grayFull;
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
                    if (ok1 && ok2) {
                        QPointF pp(x, y);
                        if (positionCorrect)
                            pp = pcMapPoint(pp);
                        searchPoly.push_back(cv::Point(int(pp.x()) - searchRect.x,
                                                       int(pp.y()) - searchRect.y));
                    }
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
        const int methodIdx = m_params.value(QStringLiteral("method"), 2).toInt();
        const double minScore = m_params.value(QStringLiteral("minScore"), 0.5).toDouble();
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
        if (active.scaleModeAuto) {
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

        // 阈值类型（VM 运行参数）：匹配阶段的边缘对比度阈值来自哪
        //   0=自动（按目标图像自动生成）  1=模板（用模板配置里的对比度阈值）  2=手动
        const int thresholdType = qBound(0, m_params.value(QStringLiteral("thresholdType"), 2).toInt(), 2);
        double searchEdgeThreshold = edgeThreshold;
        if (thresholdType == 2) {
            searchEdgeThreshold = qBound(1, m_params.value(QStringLiteral("matchContrastThreshold"), 5).toInt(), 255)
                                  * 4.0;
        } else if (thresholdType == 0) {
            searchEdgeThreshold =
                effectiveEdgeThresholdFor(gray, sobelKsize, active.contrastThreshold, true);
        }
        // 延拓阈值（VM）：目标被图像边缘截断时，允许缺失的特征比例（0~100）
        const double extendRatio =
            qBound(0, m_params.value(QStringLiteral("extendThreshold"), 0).toInt(), 100) / 100.0;
        // 超时控制（VM）：搜索超时毫秒数，0=关闭
        const int timeoutMs = qBound(0, m_params.value(QStringLiteral("timeoutMs"), 0).toInt(), 10000);
        QElapsedTimer searchTimer;
        searchTimer.start();

        QVector<double> angles;
        QVector<double> scales;
        collectSearchValues(m_params.value(QStringLiteral("angleMin"), -45.0).toDouble(),
                            m_params.value(QStringLiteral("angleMax"), 45.0).toDouble(),
                            m_params.value(QStringLiteral("angleStep"), 2.0).toDouble(),
                            90, angles);
        double sMin = m_params.value(QStringLiteral("scaleMin"), 1.0).toDouble();
        double sMax = m_params.value(QStringLiteral("scaleMax"), 1.0).toDouble();
        double sStep = m_params.value(QStringLiteral("scaleStep"), 0.05).toDouble();
        // VM 运行参数只有显式尺度范围（默认 1.00~1.00）；尺度模式属于模板配置，只管速度尺度。
        collectSearchValues(sMin, sMax, sStep, 16, scales);

        // 尺度范围类型（VM）：0=整体（scaleMin/Max），1=XY（X/Y 独立缩放）
        QVector<QPair<double, double>> scalePairs;
        if (m_params.value(QStringLiteral("scaleRangeType"), 0).toInt() == 1) {
            QVector<double> sx, sy;
            collectSearchValues(m_params.value(QStringLiteral("scaleMinX"), 1.0).toDouble(),
                                m_params.value(QStringLiteral("scaleMaxX"), 1.0).toDouble(),
                                sStep, 16, sx);
            collectSearchValues(m_params.value(QStringLiteral("scaleMinY"), 1.0).toDouble(),
                                m_params.value(QStringLiteral("scaleMaxY"), 1.0).toDouble(),
                                sStep, 16, sy);
            for (double x : sx)
                for (double y : sy)
                    scalePairs.append({x, y});
        } else {
            for (double s : scales)
                scalePairs.append({s, s});
        }

        // VM「尺度范围XY → 横切角度范围」：只在 XY 尺度模式下参与搜索；
        // 默认 0~0 表示只搜无横切目标，与旧行为逐字节一致。
        QVector<double> crossAngles;
        crossAngles.append(0.0);
        if (m_params.value(QStringLiteral("scaleRangeType"), 0).toInt() == 1) {
            QVector<double> cs;
            const double cStep =
                qMax(0.5, m_params.value(QStringLiteral("angleStep"), 2.0).toDouble());
            collectSearchValues(m_params.value(QStringLiteral("crossAngleMin"), 0.0).toDouble(),
                                m_params.value(QStringLiteral("crossAngleMax"), 0.0).toDouble(),
                                cStep, 16, cs);
            if (!cs.isEmpty())
                crossAngles = cs;
        }

        struct PosePair {
            double scaleX, scaleY, cross;
        };
        QVector<PosePair> posePairs;
        posePairs.reserve(scalePairs.size() * crossAngles.size());
        for (const auto &sp : scalePairs)
            for (double c : crossAngles)
                posePairs.append({sp.first, sp.second, c});

        const bool singleIdentity = (angles.size() == 1 && qAbs(angles.first()) < 1e-6
                                     && posePairs.size() == 1
                                     && qAbs(posePairs.first().scaleX - 1.0) < 1e-6
                                     && qAbs(posePairs.first().scaleY - 1.0) < 1e-6
                                     && qAbs(posePairs.first().cross) < 1e-6);

        // ── 粗到细搜索（r19：对齐 VisionMaster 的做法） ──
        // 实测：1500×1000 + 208px 模板 + 31 角度，全分辨率穷举 ≈16 秒。
        // 现在先在长边≈480 的粗图上搜全部姿态，再只对最优姿态做全分辨率局部精修。
        // 速度尺度 = 粗搜降采样倍率（1=不降采样）；VM：值越大越快越易漏检
        const double coarseFactor = (singleIdentity || speedScale <= 1) ? 1.0
                                                                        : double(speedScale);
        m_params[QStringLiteral("matchSpeedScale")] = speedScale;
        m_params[QStringLiteral("matchCoarseFactor")] = coarseFactor;
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
        // 掩膜内容也必须进变形缓存键（复核 2026-11-08）：掩膜每轮由当前图形/屏蔽重建，
        // 而 warpKeyBase 里的指纹只代表模板图；编辑图形或屏蔽后若键不变，会复用旧掩膜
        // 的变形结果。每轮算一次（不是逐姿态），成本可忽略。
        const quint64 shapeMaskFingerprint =
            shapeMask.empty() ? 0 : templateFingerprint(shapeMask);

        // VM「增强匹配」（仅高精度匹配）：被勾选的掩膜各自生成一张局部掩膜，评分时用该区域
        // 的局部分数给整体分数封顶（帮助页未给公式；语义 = 该区域相似度要求更严）。
        QVector<cv::Mat> enhancedMasks;
        if (methodIdx == 2 && !active.enhancedShapes.isEmpty()) {
            const QVector<RoiShape> shapeList = active.allShapes();
            QVector<RoiShape> all = shapeList;
            all += active.excludes;
            const QRectF bounds = shapesBounds(all);
            QVector<int> seenIdx;
            for (int idx : active.enhancedShapes) {
                if (idx < 0 || idx >= shapeList.size() || seenIdx.contains(idx))
                    continue;
                seenIdx.append(idx);
                cv::Mat m = shapesMask({shapeList.at(idx)}, bounds, active.excludes);
                if (!m.empty())
                    enhancedMasks.append(m);
            }
        }

        // ROI 图像类型（VM 的「区域类型=图像类型」）：第二个图像输入当掩膜，
        // 白=允许、黑=排除。按粗搜栅格缩放，候选中心必须落在白区。
        cv::Mat roiMask;
        cv::Mat roiMaskFull;   // 原图尺寸（输出掩膜用）
        if (m_params.value(QStringLiteral("roiType"), 0).toInt() == 1) {
            const QSharedPointer<DataObject> maskData = m_inputData.value(1);
            // B2：图像型 ROI 的掩膜来源是「输入订阅」，不是第二个输入点。
            // 没订阅时必须明确报错，不能静默退回图形 ROI（现场会以为 ROI 生效了）。
            if (!maskData || !maskData->getHImage().IsInitialized()) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("ROI区域类型=图像，但未订阅「掩膜图像」（基本参数 → 输入订阅）");
                m_params[QStringLiteral("moduleStatus")] = false;
                m_outputImage.Clear();
                return;
            }
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

        // VM「屏蔽区创建」：绘制（maskRegionPolygon）/ 继承（逻辑槽 2 订阅 Region/掩膜图）。
        // 语义：候选中心落在屏蔽区内的目标不检测；输出掩膜同步挖掉屏蔽区。
        cv::Mat maskRegionFull;   // 原图尺寸屏蔽区（输出掩膜扣掉）
        {
            cv::Mat maskFull;
            QStringList polyPoints, polyXs, polyYs;
            int polyCount = 0;
            const int maskSource =
                qBound(0, m_params.value(QStringLiteral("maskSource"), 0).toInt(), 1);
            if (maskSource == 1) {
                maskFull = maskRegionFromData(m_inputData.value(2), grayFull.size());
            } else {
                const QVector<RoiShape> polys = maskPolygonsFromText(
                    m_params.value(QStringLiteral("maskRegionPolygon")).toString());
                if (!polys.isEmpty()) {
                    maskFull =
                        shapesMask(polys, QRectF(0, 0, grayFull.cols, grayFull.rows));
                    // VM「模块结果 → 屏蔽区多边形 → 多边形点数/点集/点X/Y」：
                    // 顶点取绘制串的图像坐标（多条屏蔽区合计点数）。
                    for (const RoiShape &poly : polys) {
                        for (const QPointF &pt : poly.points) {
                            polyPoints << QStringLiteral("%1,%2")
                                              .arg(pt.x(), 0, 'f', 2)
                                              .arg(pt.y(), 0, 'f', 2);
                            polyXs << QString::number(pt.x(), 'f', 2);
                            polyYs << QString::number(pt.y(), 'f', 2);
                            ++polyCount;
                        }
                    }
                }
            }
            // 每条出口都写：无绘制屏蔽区时点数=0，避免上一轮的多边形留在结果表里。
            m_params[QStringLiteral("maskPolygonCount")] = polyCount;
            m_params[QStringLiteral("maskPolygonPoints")] =
                polyPoints.join(QLatin1Char(';'));
            m_params[QStringLiteral("maskPolygonXs")] = polyXs.join(QLatin1Char(','));
            m_params[QStringLiteral("maskPolygonYs")] = polyYs.join(QLatin1Char(','));
            if (!maskFull.empty()) {
                if (maskFull.size() != grayFull.size())
                    cv::resize(maskFull, maskFull, grayFull.size(), 0, 0, cv::INTER_NEAREST);
                cv::threshold(maskFull, maskFull, 127, 255, cv::THRESH_BINARY);
                maskRegionFull = maskFull;
                const cv::Rect rr =
                    searchRect & cv::Rect(0, 0, grayFull.cols, grayFull.rows);
                cv::Mat maskCoarse;
                if (rr.width > 2 && rr.height > 2) {
                    cv::resize(maskFull(rr), maskCoarse, graySearch.size(), 0, 0,
                               cv::INTER_NEAREST);
                    cv::threshold(maskCoarse, maskCoarse, 127, 255, cv::THRESH_BINARY);
                }
                if (!maskCoarse.empty()) {
                    cv::Mat keep;
                    cv::bitwise_not(maskCoarse, keep);
                    if (roiMask.empty()) {
                        roiMask = keep;
                    } else if (roiMask.size() == keep.size()) {
                        cv::bitwise_and(roiMask, keep, roiMask);
                    }
                }
                cv::Mat keepFull;
                cv::bitwise_not(maskFull, keepFull);
                if (roiMaskFull.empty()) {
                    roiMaskFull = keepFull;
                } else if (roiMaskFull.size() == keepFull.size()) {
                    cv::bitwise_and(roiMaskFull, keepFull, roiMaskFull);
                }
            }
        }

        // 缓存键：模板身份（尺寸 + 路径 + **内容指纹**）+ 角度 + 尺度。
        // 只放尺寸和路径不够（复核 P1-4）：同路径换图重训后尺寸往往不变，
        // 键不变 ⇒ 命中上一张模板的变形结果；路径为空时更糟——所有同尺寸的
        // 内存模板共用 "mem" 一个键，直接串味。指纹把这两种情况都区分开。
        const QString warpKeyBase =
            QStringLiteral("%1x%2:%3:%4")
                .arg(tmpl.cols)
                .arg(tmpl.rows)
                .arg(templatePath.isEmpty() ? QStringLiteral("mem") : templatePath)
                .arg(templateFingerprint(tmpl), 0, 16);

        double bestScore = -1e9;
        cv::Point bestLoc(0, 0);
        double bestAngle = 0.0;
        double bestScale = 1.0;       // = bestScaleX（VM matchScale）
        double bestScaleY = 1.0;
        double bestCross = 0.0;       // VM「横切值」：Y 轴向 X 轴倾斜角
        cv::Point2f bestCenterInWarp(tmpl.cols * 0.5f, tmpl.rows * 0.5f);
        cv::Point2f bestCoarseCenter;   // 粗搜栅格下的中心（多目标抑制用）
        cv::Point2f bestSubpix;         // 亚像素偏移（r19 收尾）
        cv::Point2f subpixTmp;
        const double tmplW = tmpl.cols;
        const double tmplH = tmpl.rows;

        // VM「是否获取轮廓」：提取模板轮廓特征点（模板局部坐标）。关闭时跳过提取，
        // 画布只显示匹配框（官方帮助：关闭则不显示特征点、可降低匹配耗时）。
        const bool getContour = m_params.value(QStringLiteral("getContour"), true).toBool();
        const bool outputContour = m_params.value(QStringLiteral("outputContour"), false).toBool();
        // VM「是否考虑噪点」：只作用于 GOC（method=2）的评分；默认关，旧行为逐字节不变。
        const bool considerNoise =
            m_params.value(QStringLiteral("considerNoise"), false).toBool();
        QVector<QPointF> contourLocal;
        if (getContour) {
            // featurePointsForPatch 返回图像坐标（按 ROI 落位）；这里反算回模板局部系，
            // 命中后再用 R·S·H 映射到每个目标的实际位姿。
            const RoiShape tmplRoi = geometryRoi();
            const QVector<QPointF> imgPts =
                featurePointsForPatch(tmpl, tmplRoi, edgeThreshold, sobelKsize, shapeMask,
                                      featureScale, minChain, active.erases);
            const bool axisAligned =
                (tmplRoi.type != RoiType::RotatedRect) || qAbs(tmplRoi.angleDeg) < 1e-6;
            const QRectF shapeBox = roiAxisAlignedBounds(tmplRoi);
            const double roiCx = axisAligned ? shapeBox.center().x() : tmplRoi.p1.x();
            const double roiCy = axisAligned ? shapeBox.center().y() : tmplRoi.p1.y();
            const int x0 = int(std::floor(roiCx - tmplW * 0.5 + 0.5));
            const int y0 = int(std::floor(roiCy - tmplH * 0.5 + 0.5));
            const double roiRad = tmplRoi.angleDeg * 3.14159265358979323846 / 180.0;
            const double roiCa = std::cos(roiRad);
            const double roiSa = std::sin(roiRad);
            contourLocal.reserve(imgPts.size());
            for (const QPointF &p : imgPts) {
                if (axisAligned) {
                    contourLocal.append(QPointF(p.x() - x0, p.y() - y0));
                } else {
                    const double dx = p.x() - roiCx;
                    const double dy = p.y() - roiCy;
                    const double lx = roiCa * dx + roiSa * dy;
                    const double ly = -roiSa * dx + roiCa * dy;
                    contourLocal.append(QPointF(lx + tmplW * 0.5 - 0.5,
                                                ly + tmplH * 0.5 - 0.5));
                }
            }
        }
        // 每轮先清空模块结果里的轮廓字段：本轮没命中/没开输出时不得留上一轮的点。
        m_params[QStringLiteral("matchContourCount")] = 0;
        m_params[QStringLiteral("matchContourPointXs")] = QString();
        m_params[QStringLiteral("matchContourPointYs")] = QString();

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
                           double scaleX, double scaleY, double crossAngle,
                           cv::Point *locOut, cv::Point2f *centerOut,
                           cv::Point2f *subpixOut = nullptr) -> double {
            const auto warpedPair =
                warpTemplateCached(base, warpKeyBase, angle, scaleX, scaleY, crossAngle);
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
                // 打分沿用幅值掩膜（轮廓细化只用于界面显示）：细化会把可见点压到
                // 1 像素线，跨帧/遮挡场景的可见点数容易跌破门槛，导致整帧判无效。
                cv::Mat wm;
                cv::threshold(wmag, wm, edgeThreshold, 1.0, cv::THRESH_BINARY);
                wm.convertTo(wm, CV_32F);
                if (!shapeMask.empty()) {
                    // 掩膜与模板同一套 warp，但**必须先缩到本次 base 的尺寸**：
                    // 粗搜会把模板降采样（speedScale），而掩膜是全分辨率模板坐标；
                    // 不跟着缩就会因尺寸不匹配被丢弃，粗搜阶段的排除区失效（2026-11-08 实测）。
                    cv::Mat maskForBase = shapeMask;
                    if (maskForBase.size() != base.size())
                        cv::resize(maskForBase, maskForBase, base.size(), 0, 0, cv::INTER_NEAREST);
                    const QString maskKey =
                        QStringLiteral("#shapeMask:%1:%2x%3")
                            .arg(shapeMaskFingerprint, 0, 16)
                            .arg(base.cols)
                            .arg(base.rows);
                    const auto warpedMask =
                        warpTemplateCached(maskForBase, warpKeyBase + maskKey, angle, scaleX, scaleY,
                                           crossAngle);
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
                // 2026-11-08 复核留记：曾怀疑 qMax(16.0, ...) 把下界钉死、使延拓阈值对小模板
                // 失效，试图改成 qMax(1.0, qMin(16.0, 0.4*n))。实测被
                // templateMinVisibleRatioControlsOcclusion 打回——该腿要求「38% 可见必须失败」，
                // 说明 16 是**刻意的噪声下界**（小模板要求更高可见率防误检），不是几何 bug。
                const double need = qMax(16.0, 0.4 * n);
                cv::Mat scoreVis = (r1 + r2) / (rv + 1e-6f);   // cos(Δθ) 的和除以可见点数
                // 极性忽略模式必须显式保存「无效格」掩膜（复核 2026-11-08）：GOC 的
                // 有效分数就是 cos(Δθ) 的均值，反极性目标天然是负值——而 polarity=0
                // 的语义正是把负分取绝对值（"两极性都试"）。事后用 scoreVis<0 反推
                // 无效格，会把反极性目标一起压成 -1，polarity=0 退化成与 polarity=1 等价。
                const bool ignorePolarity =
                    m_params.value(QStringLiteral("polarity"), 1).toInt() == 0;
                cv::Mat invalidMask;
                if (ignorePolarity)
                    invalidMask = (rv < need);
                scoreVis.setTo(-1.0, rv < need);           // 无效处给 -1（不会被选为最优）
                // 延拓阈值（VM）：目标被图像边缘截断时，缺失特征比例可放宽可见率门槛
                if (extendRatio > 0.0 && !wm.empty()) {
                    const double wW = double(wm.cols);
                    const double hW = double(wm.rows);
                    const double area = qMax(1.0, wW * hW);
                    const double W = double(curSearchStrong.cols);
                    const double H = double(curSearchStrong.rows);
                    for (int yy = 0; yy < scoreVis.rows; ++yy) {
                        for (int xx = 0; xx < scoreVis.cols; ++xx) {
                            if (scoreVis.at<float>(yy, xx) > -0.5f)
                                continue;
                            const double cx = xx + warpedPair.second.x;
                            const double cy = yy + warpedPair.second.y;
                            const double ix0 = qMax(0.0, cx - wW * 0.5);
                            const double ix1 = qMin(W, cx + wW * 0.5);
                            const double iy0 = qMax(0.0, cy - hW * 0.5);
                            const double iy1 = qMin(H, cy + hW * 0.5);
                            const double inter = qMax(0.0, ix1 - ix0) * qMax(0.0, iy1 - iy0);
                            const double outside = qBound(0.0, 1.0 - inter / area, 1.0);
                            if (outside <= 1e-6)
                                continue;
                            const double relaxedNeed =
                                qMax(16.0, 0.4 * n * (1.0 - extendRatio * outside));
                            const float v = rv.at<float>(yy, xx);
                            if (v >= relaxedNeed) {
                                scoreVis.at<float>(yy, xx) =
                                    (r1.at<float>(yy, xx) + r2.at<float>(yy, xx)) / (v + 1e-6f);
                                if (ignorePolarity)
                                    invalidMask.at<uchar>(yy, xx) = 0;   // 延拓后重新有效
                            }
                        }
                    }
                }
                if (ignorePolarity) {
                    // 极性翻转只作用于**有效**位置（复核 2026-11-08）：invalidMask 是
                    // 可见率（含延拓放宽后）不足的格子。用 score<0 反推会把反极性目标
                    // 的合法负分一起压掉，所以这里只认显式掩膜。
                    cv::Mat flipped;
                    cv::absdiff(scoreVis, cv::Scalar(0), flipped);
                    flipped.setTo(-1.0f, invalidMask);
                    scoreVis = flipped;
                }
                for (const cv::Mat &enhMaskSrc : enhancedMasks) {
                    // 掩膜与模板同一套 warp；粗搜会改 base 尺寸，先缩到 base 再变形。
                    cv::Mat maskForBase = enhMaskSrc;
                    if (maskForBase.size() != base.size())
                        cv::resize(maskForBase, maskForBase, base.size(), 0, 0,
                                   cv::INTER_NEAREST);
                    const QString enhKey =
                        warpKeyBase
                        + QStringLiteral("#enh:%1:%2x%3")
                              .arg(templateFingerprint(enhMaskSrc), 0, 16)
                              .arg(base.cols)
                              .arg(base.rows);
                    const auto warpedEnh =
                        warpTemplateCached(maskForBase, enhKey, angle, scaleX, scaleY, crossAngle);
                    if (warpedEnh.first.empty() || warpedEnh.first.size() != wm.size())
                        continue;
                    cv::Mat keep;
                    cv::threshold(warpedEnh.first, keep, 127.0, 1.0, cv::THRESH_BINARY);
                    keep.convertTo(keep, CV_32F);
                    cv::Mat localWm = wm.mul(keep);
                    const double localN = cv::sum(localWm)[0];
                    if (localN < 16.0)
                        continue;   // 该掩膜特征太少，增强无从谈起（不误伤整体匹配）
                    cv::Mat localCos = wc.mul(keep);
                    cv::Mat localSin = ws.mul(keep);
                    cv::Mat lr1, lr2, lrv;
                    cv::matchTemplate(curSearchCos, localCos, lr1, cv::TM_CCORR);
                    cv::matchTemplate(curSearchSin, localSin, lr2, cv::TM_CCORR);
                    cv::matchTemplate(curSearchStrong, localWm, lrv, cv::TM_CCORR);
                    const double localNeed = qMax(16.0, 0.4 * localN);
                    cv::Mat localScore = (lr1 + lr2) / (lrv + 1e-6f);
                    if (ignorePolarity) {
                        cv::Mat flippedLocal;
                        cv::absdiff(localScore, cv::Scalar(0), flippedLocal);
                        localScore = flippedLocal;
                    }
                    // 增强区域不可见/特征点没落在搜索边缘上 ⇒ 该区域相似度不达标，压到 0。
                    localScore.setTo(0.0f, lrv < localNeed);
                    cv::min(scoreVis, localScore, scoreVis);
                }
                if (considerNoise) {
                    // VM「是否考虑噪点」（帮助页只给语义、未给公式）：搜索图里未被模板特征点
                    // 解释的强边缘像素视为毛刺/噪点。噪点率 = 这些像素数 / 模板特征点数，
                    // 评分按 score/(1+噪点率) 衰减（单调、不改符号）；-1 哨兵格不参与。
                    cv::Mat dilated;
                    cv::dilate(wm, dilated,
                               cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));
                    cv::Mat noiseMask = cv::Mat::ones(dilated.size(), CV_32F) - dilated;
                    cv::Mat noiseCount;
                    cv::matchTemplate(curSearchStrong, noiseMask, noiseCount, cv::TM_CCORR);
                    cv::Mat denom;
                    cv::divide(noiseCount, cv::Scalar(n + 1e-6), denom);
                    denom = denom + 1.0;
                    cv::Mat penalized;
                    cv::divide(scoreVis, denom, penalized);
                    cv::Mat valid = (scoreVis > -0.5f);
                    penalized.copyTo(scoreVis, valid);
                }
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
                // 2026-11-08 复核留记：曾怀疑 boxFilter 是中心对齐、与 matchTemplate 的
                // 左上角对齐差 (w/2,h/2)，试图把 ROI 偏移到 var(w/2,h/2)。实测被
                // templateMatchOnRealImage（位置从 (1040,1430) 偏到 (905,186)）和
                // templateMatchMaskMovesBestHit（掩膜失效、多认一个目标）打回——
                // 原写法与 boxFilter 的中心对齐是配套正确的，不是半核错位。
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
            // 上面的 if (locOut) 只管住了亚像素块（没有大括号），这一行原本无条件执行；
            // 将来任何 locOut=nullptr 的调用都会空指针崩溃（2026-11-09 审核）。
            if (locOut)
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
            qBound(1, m_params.value(QStringLiteral("maxMatches"), 1).toInt(), 1000);
        QStringList matchList;
        // VM「排序类型 / 最大重叠率」：先收集命中，最后统一排序 + 重叠过滤再格式化
        struct MatchHit {
            double row, col, score, angle, scale, prow, pcol;
            double scaleY;   ///< XY 独立尺度模式下的 Y 向尺度（整体模式等于 scale）
            double cross;    ///< 横切角（度；整体尺度模式下恒为 0）
            /// 该命中自身的亚像素偏移。主结果字段要跟随排序后的首条命中，
            /// 不能只留第一实例的 bestSubpix（2026-11-09 口径对齐）。
            double subpixX = 0.0, subpixY = 0.0;
        };
        QVector<MatchHit> hits;
        int matchCount = 0;
        cv::Point mainLoc;
        double mainScore = -1e9, mainAngle = 0.0, mainScale = 1.0, mainCross = 0.0;
        double mainScaleY = 1.0;   // 主命中的 Y 尺度（XY 模式下与 mainScale 不同）
        cv::Point2f mainCenter;
        // 官方口径（帮助页「超时控制」）：到点停止搜索，不返回任何搜索结果。
        // 该标志跨实例保留：超时后不再找下一个目标，且已找到的部分结果整体作废。
        bool searchTimedOut = false;

        for (int inst = 0; inst < maxMatches; ++inst) {
            if (searchTimedOut)
                break;
            bestScore = -1e9;
            bestLoc = cv::Point();
            bestAngle = 0.0;
            bestScale = 1.0;
            bestScaleY = 1.0;
            bestCross = 0.0;
            bestCenterInWarp = cv::Point2f();
            bestCoarseCenter = cv::Point2f();
            bestSubpix = cv::Point2f();

            // 粗搜：全部角度 x 尺度（粗图上跑）
            rasterScale = 1.0 / qMax(1.0, coarseFactor);
            searchOrigin = cv::Point(0, 0);
        for (const auto &sp : posePairs) {
            if (searchTimedOut)
                break;
            for (double angle : angles) {
                // 超时控制（VM 官方帮助）：到点停止搜索，不返回任何搜索结果；
                // 本轮已找到的部分结果在实例循环结束后整体作废。
                if (timeoutMs > 0 && searchTimer.elapsed() >= timeoutMs) {
                    searchTimedOut = true;
                    break;
                }
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
            cv::threshold(smag, curSearchStrong, searchEdgeThreshold, 1.0, cv::THRESH_BINARY);
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
                const double sc = evalOne(coarseSearchImg, coarseTmplImg, angle, sp.scaleX,
                                          sp.scaleY, sp.cross, &loc, &centerInWarp, &subpixTmp);
                if (sc > bestScore) {
                    bestScore = sc;
                    bestLoc = loc;
                    bestAngle = angle;
                    bestScale = sp.scaleX;
                    bestScaleY = sp.scaleY;
                    bestCross = sp.cross;
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

        if (searchTimedOut)
            break;   // 官方口径：超时后不进入精修、不产出任何结果

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
        if (coarseFactor > 1.0 && bestScore > -1e8 && !searchTimedOut) {
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
                    cv::threshold(smag, curSearchStrong, searchEdgeThreshold, 1.0,
                                  cv::THRESH_BINARY);
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
                    if (timeoutMs > 0 && searchTimer.elapsed() >= timeoutMs) {
                        searchTimedOut = true;   // 精修阶段超时：同样整体作废
                        break;
                    }
                    const double angle = bestAngle + k * stepA;
                    if (angle < angles.first() - 1e-9 || angle > angles.last() + 1e-9)
                        continue;
                    cv::Point loc;
                    cv::Point2f centerInWarp;
                    const double sc = evalOne(refineWin, refineTmpl, angle, bestScale, bestScaleY,
                                              bestCross, &loc, &centerInWarp, &subpixTmp);
                    if (sc > bestScore) {
                        bestScore = sc;
                        bestLoc = cv::Point(x0 + loc.x, y0 + loc.y);
                        bestAngle = angle;
                        bestCenterInWarp = centerInWarp;
                    }
                }
            }
        }

            if (searchTimedOut)
                break;   // 精修阶段超时：不追加本轮命中
            if (bestScore < -1e8)
                break;
            if (inst > 0 && bestScore < minScore)
                break;                       // 追加实例必须过阈值（第一实例维持原口径）
            if (inst == 0) {
                mainLoc = bestLoc;
                mainScore = bestScore;
                mainAngle = bestAngle;
                mainScale = bestScale;
                mainScaleY = bestScaleY;
                mainCross = bestCross;
                mainCenter = bestCenterInWarp;
            }
            {
                // 列表坐标必须换算回原图（搜索区域裁剪过时，直接用 bestLoc 会整体偏一个 searchRect）。
                // 字段：row,col,score,angle,scale,pointRow,pointCol —— 前 3 项保持老顺序。
                const double centerX = bestLoc.x + bestCenterInWarp.x + searchRect.x;
                const double centerY = bestLoc.y + bestCenterInWarp.y + searchRect.y;
                // 匹配点 = 锚点（模板中心 + 配置的偏移）缩放旋转后的落点。
                // 统一走 mapAnchorToImage，与 warpTemplate 的旋转矩阵同源（复核 P1-2）。
                const QPointF anchorPoint = mapAnchorToImage(centerX, centerY, bestAngle,
                                                             active.anchorDx, active.anchorDy,
                                                             bestScale, bestCross, bestScaleY);
                const double pCol = anchorPoint.x();
                const double pRow = anchorPoint.y();
                hits.append({centerY, centerX, bestScore, bestAngle, bestScale, pRow, pCol,
                             bestScaleY, bestCross, bestSubpix.x, bestSubpix.y});
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

        if (searchTimedOut) {
            // 官方口径：超时「不返回任何搜索结果」。已找到的部分结果整体作废，
            // 不能把粗搜残留的 bestScore 当结果输出。
            hits.clear();
            matchList.clear();
            matchCount = 0;
            foundCenters.clear();
            bestScore = -1e9;
        }

        // 第一实例写回既有输出口径
        if (matchCount > 0) {
            bestLoc = mainLoc;
            bestScore = mainScore;
            bestAngle = mainAngle;
            bestScale = mainScale;
            bestScaleY = mainScaleY;
            bestCross = mainCross;
            bestCenterInWarp = mainCenter;
        }
        // matchCount 必须等重叠过滤 + 排序**之后**再写（2026-11-08 复核）：
        // 原位置在过滤前就写死，而 matchList / matchBox* 都是按过滤后的 hits 重建，
        // 于是「匹配数量」与「匹配列表条数」对不上（例如显示 5 个、列表只有 3 条）。
        m_params[QStringLiteral("matchCountBeforeOverlap")] = matchCount;
        // VM「最大重叠率」：重叠超阈值时舍弃得分较低的目标（先按分数降序处理，保高分）
        {
            const double maxOverlap =
                qBound(0, m_params.value(QStringLiteral("maxOverlap"), 50).toInt(), 100) / 100.0;
            if (maxOverlap < 1.0 && hits.size() > 1) {
                std::stable_sort(hits.begin(), hits.end(),
                                 [](const MatchHit &a, const MatchHit &b) { return a.score > b.score; });
                QVector<MatchHit> kept;
                for (const MatchHit &h : hits) {
                    // XY 尺度模式下高度必须乘 scaleY（2026-11-09 审核）：两轴都用 scale
                    // 会让重叠率分子/面积算错，可能误删或误留命中；输出框宽高已是正确口径。
                    const double hw = qMax(4.0, tmplW * h.scale);
                    const double hh = qMax(4.0, tmplH * h.scaleY);
                    bool drop = false;
                    for (const MatchHit &k : kept) {
                        const double kw = qMax(4.0, tmplW * k.scale);
                        const double kh = qMax(4.0, tmplH * k.scaleY);
                        const double interW = qMax(0.0, (hw + kw) * 0.5 - qAbs(h.col - k.col));
                        const double interH = qMax(0.0, (hh + kh) * 0.5 - qAbs(h.row - k.row));
                        const double inter = interW * interH;
                        const double minArea = qMin(hw * hh, kw * kh);
                        if (minArea > 1.0 && inter / minArea > maxOverlap) {
                            drop = true;
                            break;
                        }
                    }
                    if (!drop)
                        kept.append(h);
                }
                hits = kept;
            }
        }
        // VM「排序类型」：多目标结果的排序依据（默认按分数降序）。
        // 案例 4/5 的并列规则（整数化后相同再按另一轴）在 TemplateMatchSort 单点定义。
        {
            const int sortType =
                qBound(0, m_params.value(QStringLiteral("sortType"), 0).toInt(), 5);
            TemplateMatchSort::stableSortByType(hits, sortType);
        }
        // 到这里 hits 已是最终结果（重叠过滤 + 排序后），matchCount 与之对齐
        m_params[QStringLiteral("matchCount")] = hits.size();

        matchList.clear();
        for (const MatchHit &h : hits) {
            matchList << QStringLiteral("%1,%2,%3,%4,%5,%6,%7")
                             .arg(h.row, 0, 'f', 1)
                             .arg(h.col, 0, 'f', 1)
                             .arg(h.score, 0, 'f', 3)
                             .arg(h.angle, 0, 'f', 2)
                             .arg(h.scale, 0, 'f', 4)
                             .arg(h.prow, 0, 'f', 1)
                             .arg(h.pcol, 0, 'f', 1);
        }
        m_params[QStringLiteral("matchList")] = matchList.join(QLatin1Char(';'));

        // VM「模块结果」逐匹配目标字段：按最终 hits（重叠过滤 + 排序后）逐项输出。
        // 匹配框 = 模板矩形经尺度/角度变换后的外框：宽 = 模板宽×尺度X，高 = 模板高×尺度Y。
        {
            QStringList centers, centerXs, centerYs, widths, heights, angles;
            QStringList points, pointXs, pointYs, scales, scaleXs, scaleYs, scores, crosses;
            for (const MatchHit &h : hits) {
                centers << QStringLiteral("%1,%2")
                               .arg(h.col, 0, 'f', 2).arg(h.row, 0, 'f', 2);
                centerXs << QString::number(h.col, 'f', 2);
                centerYs << QString::number(h.row, 'f', 2);
                widths << QString::number(tmplW * h.scale, 'f', 2);
                heights << QString::number(tmplH * h.scaleY, 'f', 2);
                angles << QString::number(h.angle, 'f', 2);
                points << QStringLiteral("%1,%2")
                              .arg(h.pcol, 0, 'f', 2).arg(h.prow, 0, 'f', 2);
                pointXs << QString::number(h.pcol, 'f', 2);
                pointYs << QString::number(h.prow, 'f', 2);
                scales << QString::number(h.scale, 'f', 4);
                scaleXs << QString::number(h.scale, 'f', 4);
                scaleYs << QString::number(h.scaleY, 'f', 4);
                scores << QString::number(h.score, 'f', 4);
                crosses << QString::number(h.cross, 'f', 2);
            }
            m_params[QStringLiteral("matchBoxCenters")] = centers.join(QLatin1Char(';'));
            m_params[QStringLiteral("matchBoxCenterXs")] = centerXs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchBoxCenterYs")] = centerYs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchBoxWidths")] = widths.join(QLatin1Char(','));
            m_params[QStringLiteral("matchBoxHeights")] = heights.join(QLatin1Char(','));
            m_params[QStringLiteral("matchBoxAngles")] = angles.join(QLatin1Char(','));
            m_params[QStringLiteral("matchPoints")] = points.join(QLatin1Char(';'));
            m_params[QStringLiteral("matchPointXs")] = pointXs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchPointYs")] = pointYs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchScales")] = scales.join(QLatin1Char(','));
            m_params[QStringLiteral("matchScaleXs")] = scaleXs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchScaleYs")] = scaleYs.join(QLatin1Char(','));
            m_params[QStringLiteral("matchCrossAngles")] = crosses.join(QLatin1Char(','));
            m_params[QStringLiteral("matchScores")] = scores.join(QLatin1Char(','));
            // 匹配状态只看"是否找到达标目标"，与模块状态（结果判断）分开
            // 用**最终 hits** 判定，不用过滤前的局部 matchCount：重叠过滤把列表清空时
            // 不能仍报「找到目标」（2026-11-09 审核）。主标量字段仍取第一实例，口径见下。
            m_params[QStringLiteral("matchStatus")] =
                (!hits.isEmpty() && mainScore >= minScore) ? 1 : 0;

            // VM「模块结果 → 检测区域」：本轮实际搜索区域（无 ROI 时整图；多边形时取包围盒）
            m_params[QStringLiteral("searchCenterX")] =
                searchRect.x + searchRect.width * 0.5;
            m_params[QStringLiteral("searchCenterY")] =
                searchRect.y + searchRect.height * 0.5;
            m_params[QStringLiteral("searchWidth")] = searchRect.width;
            m_params[QStringLiteral("searchHeight")] = searchRect.height;
            m_params[QStringLiteral("searchAngle")] = 0.0;
        }

        if (bestScore < -1e8) {
            const QString why = searchTimedOut
                                    ? QStringLiteral("搜索超时（未返回任何搜索结果）")
                                    : QStringLiteral("无有效搜索位姿（模板相对图像过大）");
            m_params[QStringLiteral("trainStatus")] = why;
            m_params[QStringLiteral("lastError")] = why;
            m_params["moduleStatus"] = false;
            m_params[QStringLiteral("elapsedMs")] = static_cast<double>(runTimer.elapsed());
            m_outputImage = m_inputImage;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }

        // 官方口径（VM4.4高精度匹配对齐规范.md:68）：排序类型是「当前结果」的排序依据
        // ⇒ 主标量字段 / 结果判断 / MeasureResult 必须与 matchList 第一行是同一目标。
        // 原实现恒取「第一实例」（最高分），sortType != 0 时会与列表首行错位；多模板
        // 合并路径早用 merged.first() 重写主字段，这里补齐单模板路径（2026-11-09）。
        // hits.first().row/col 与下面 bestLoc 的表达式完全同源，故默认 sortType=0
        // 时行为零变化；亚像素也随命中一起存进 MatchHit，不再只留第一实例。
        const MatchHit *current = hits.isEmpty() ? nullptr : &hits.first();
        const double col = current ? current->col
                                   : bestLoc.x + searchRect.x + bestCenterInWarp.x;
        const double row = current ? current->row
                                   : bestLoc.y + searchRect.y + bestCenterInWarp.y;
        const double subX = current ? current->subpixX : bestSubpix.x;
        const double subY = current ? current->subpixY : bestSubpix.y;
        const double curScore = current ? current->score : bestScore;
        const double curAngle = current ? current->angle : bestAngle;
        const double curScale = current ? current->scale : bestScale;
        const double curScaleY = current ? current->scaleY : bestScaleY;
        const double curCross = current ? current->cross : bestCross;
        // 匹配框宽高：X 用 scaleX、Y 用 scaleY（VM 模块结果；XY 模式下两者独立）。
        const double outW = tmplW * curScale;
        const double outH = tmplH * curScaleY;

        m_params[QStringLiteral("matchRow")] = row + subY;
        m_params[QStringLiteral("matchCol")] = col + subX;
        m_params[QStringLiteral("matchScore")] = curScore;
        m_params[QStringLiteral("matchAngle")] = curAngle;
        m_params[QStringLiteral("matchScale")] = curScale;
        // VM 结果表口径：「匹配点」= 模板的匹配中心（锚点）落到场景里的位置。
        // 锚点默认在模板中心（anchorDx/Dy=0），可在模板配置窗里偏移并锁定。
        {
            // 与列表路径同一口径（复核 P1-2 残留）：主结果字段也必须走
            // mapAnchorToImage，不能再用共轭矩阵，否则非零锚点+非零角度时
            // 「匹配点 X/Y」与结果判断、叠加十字线一起镜像（差 2θ）。
            const QPointF anchorPoint =
                mapAnchorToImage(col + subX, row + subY, curAngle,
                                 active.anchorDx, active.anchorDy, curScale, curCross,
                                 curScaleY);
            m_params[QStringLiteral("matchPointCol")] = anchorPoint.x();
            m_params[QStringLiteral("matchPointRow")] = anchorPoint.y();
            m_params[QStringLiteral("matchScaleX")] = curScale;
            m_params[QStringLiteral("matchScaleY")] = curScaleY;
            m_params[QStringLiteral("matchCrossAngle")] = curCross;
        }
        // VM「是否输出轮廓」：主命中的匹配轮廓点集（图像坐标）。点源是模板局部坐标，
        // 经与打分同一套 R·S·H 映射落到实际位姿；只输出主目标（逐目标点集暂不展开）。
        QVector<QPointF> contourImage;
        if (getContour && !contourLocal.isEmpty()) {
            contourImage.reserve(contourLocal.size());
            for (const QPointF &p : contourLocal) {
                contourImage.append(
                    mapTemplatePoint(col + subX, row + subY, curAngle,
                                     p.x() - tmplW * 0.5, p.y() - tmplH * 0.5, curScale,
                                     curScaleY, curCross));
            }
            if (outputContour) {
                QStringList xs, ys;
                xs.reserve(contourImage.size());
                ys.reserve(contourImage.size());
                for (const QPointF &p : contourImage) {
                    xs << QString::number(p.x(), 'f', 2);
                    ys << QString::number(p.y(), 'f', 2);
                }
                m_params[QStringLiteral("matchContourCount")] = contourImage.size();
                m_params[QStringLiteral("matchContourPointXs")] = xs.join(QLatin1Char(','));
                m_params[QStringLiteral("matchContourPointYs")] = ys.join(QLatin1Char(','));
            }
        }
        bool judgedOk = (bestScore >= minScore);
        if (judgedOk) {
            // VM「结果显示 → 结果判断」：已启用的数量/角度/尺度/分数/坐标条件逐条判定。
            QString judgeReason;
            if (!resultJudgmentOk(&judgeReason)) {
                judgedOk = false;
                if (!judgeReason.isEmpty())
                    m_params[QStringLiteral("lastError")] = judgeReason;
            }
        } else {
            // 未匹配也要说明原因（2026-10-26 复核）：只判红，面板上看不到差多少
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("匹配分数 %1 低于阈值 %2").arg(bestScore).arg(minScore);
        }
        m_params["moduleStatus"] = judgedOk;
        // 判 NG 但匹配结果有效（分数/角度/尺度/匹配点已算出）：保留输出与结果字段，
        // 结果面板按红色展示逐目标数据；执行异常/无模板仍走普通失败（结果不保留）。
        setResultNg(!judgedOk);
        const QString fixtureName =
            m_params.value(QStringLiteral("writeFixtureName")).toString().trimmed();
        if (!fixtureName.isEmpty() && bestScore >= minScore) {
            if (FlowScene *fs = flowSceneRef())
                fs->setFixturePose(fixtureName, row, col, bestAngle, bestScale);
        }
        m_outputImage = m_inputImage;

        // 输出掩膜（两个独立开关，VM 口径）：
        //   · ROI 区域「输出掩膜」= 模板 ROI 的形状/图像掩膜
        //   · 检测区域「输出掩膜（把此区域作为算子掩膜输入）」= 搜索区域掩膜
        QSharedPointer<DataObject> roiMaskObj;
        if (m_params.value(QStringLiteral("outputMask"), false).toBool()) {
            cv::Mat maskOut;
            if (!roiMaskFull.empty()) {
                maskOut = roiMaskFull;
            } else {
                maskOut = cv::Mat::zeros(grayFull.size(), CV_8UC1);
                RoiShape roi = geometryRoi();
                if (roi.type == RoiType::RotatedRect && roi.width > 1 && roi.height > 1) {
                    const double rad = roi.angleDeg * CV_PI / 180.0;
                    const double ca = std::cos(rad);
                    const double sa = std::sin(rad);
                    const double hw = roi.width * 0.5;
                    const double hh = roi.height * 0.5;
                    std::vector<cv::Point> pts;
                    for (const auto &c : {std::pair<double, double>{-hw, -hh},
                                          std::pair<double, double>{hw, -hh},
                                          std::pair<double, double>{hw, hh},
                                          std::pair<double, double>{-hw, hh}}) {
                        pts.push_back(cv::Point(int(roi.p1.x() + c.first * ca - c.second * sa),
                                                int(roi.p1.y() + c.first * sa + c.second * ca)));
                    }
                    cv::fillPoly(maskOut, std::vector<std::vector<cv::Point>>{pts},
                                 cv::Scalar(255));
                } else if (roi.type == RoiType::Rect) {
                    cv::rectangle(maskOut, cv::Rect(cv::Point(int(roi.p1.x()), int(roi.p1.y())),
                                                    cv::Point(int(roi.p2.x()), int(roi.p2.y()))),
                                  cv::Scalar(255), cv::FILLED);
                }
            }
            roiMaskObj = QSharedPointer<DataObject>::create();
            roiMaskObj->setHImage(OpencvUtil::matToHimage(maskOut));
        }

        // VM「模块结果 → 输出掩膜」：检测区域掩膜 − 屏蔽区（官方：根据检测区域和屏蔽区域
        // 计算出的二值图）。宽度/高度/像素格式是模块结果固定字段，每轮都写；
        // 图像本身只在「输出掩膜（把此区域作为算子掩膜输入）」打开时挂具名子结果。
        cv::Mat matchMaskFull = cv::Mat::zeros(grayFull.size(), CV_8UC1);
        if (searchPoly.size() >= 3) {
            std::vector<cv::Point> pts = searchPoly;
            for (cv::Point &p : pts) {
                p.x += searchRect.x;
                p.y += searchRect.y;
            }
            cv::fillPoly(matchMaskFull, std::vector<std::vector<cv::Point>>{pts},
                         cv::Scalar(255));
        } else {
            cv::rectangle(matchMaskFull, searchRect, cv::Scalar(255), cv::FILLED);
        }
        if (!maskRegionFull.empty() && maskRegionFull.size() == matchMaskFull.size()) {
            cv::Mat keep;
            cv::bitwise_not(maskRegionFull, keep);
            cv::bitwise_and(matchMaskFull, keep, matchMaskFull);
        }
        m_params[QStringLiteral("outMaskWidth")] = matchMaskFull.cols;
        m_params[QStringLiteral("outMaskHeight")] = matchMaskFull.rows;
        // 17301505 = VM/HALCON 的 Mono8 像素格式编号（帮助页「一般为17301505，表示Mono8格式」）
        m_params[QStringLiteral("outMaskPixelFormat")] = 17301505;

        QSharedPointer<DataObject> searchMaskObj;
        if (m_params.value(QStringLiteral("outputSearchMask"), false).toBool()) {
            searchMaskObj = QSharedPointer<DataObject>::create();
            searchMaskObj->setHImage(OpencvUtil::matToHimage(matchMaskFull));
        }

        MeasureResult res;
        res.type = QStringLiteral("template");
        res.valueName = QStringLiteral("匹配得分");
        // 与主标量字段同源：MeasureResult 也描述「当前结果」（排序后首条命中）
        res.valid = (curScore >= minScore);
        res.value = curScore;
        res.point1 = QPointF(col, row);
        res.extraValues = QVector<double>({row, col, curScore, curAngle, curScale, outW, outH,
                                           m_params.value(QStringLiteral("matchPointRow")).toDouble(),
                                           m_params.value(QStringLiteral("matchPointCol")).toDouble(),
                                           curScale, curScaleY, curCross});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        // VM「是否获取轮廓」：轮廓点集作为独立结果交给叠加层（默认不显示的特征点层）。
        // 放在独立 MeasureResult 里而不是模块结果字段——「是否输出轮廓」只控制后者。
        if (getContour && !contourImage.isEmpty()) {
            MeasureResult contourRes;
            contourRes.type = QStringLiteral("contour");
            contourRes.valueName = QStringLiteral("匹配轮廓点");
            contourRes.valid = (bestScore >= minScore);
            contourRes.value = contourImage.size();
            contourRes.extraValues.reserve(contourImage.size() * 2);
            for (const QPointF &p : contourImage) {
                contourRes.extraValues << p.x() << p.y();
            }
            resObj->attachMeasureResult(contourRes);
        }
        if (roiMaskObj)
            resObj->attachResult(QStringLiteral("掩膜"), roiMaskObj);
        if (searchMaskObj)
            resObj->attachResult(QStringLiteral("检测区域掩膜"), searchMaskObj);
        setOutputData(1, resObj);
        m_params[QStringLiteral("elapsedMs")] = static_cast<double>(runTimer.elapsed());
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

QVector<NodeResultField> OpencvTemplateMatchNode::resultDisplayFields() const
{
    // 官方「模块结果」字段表（GUID-11F05391「模块结果」节，高精度匹配）：
    // 耗时 / 模块状态 / 匹配状态 / 匹配个数 / 匹配模板编号 / 匹配模板名称 /
    // 匹配框(box：中心点/X/Y/宽度/高度/角度) / 匹配点(X/Y) / 尺度(X/Y) / 分数 /
    // 检测区域(box：中心点X/Y/宽度/高度/角度) / 匹配轮廓信息(pointset，官方注明不显示) /
    // 匹配轮廓点个数 / 屏蔽区多边形(polygo：点数/点集/点X/Y) /
    // 输出掩膜(image：宽度/高度/像素格式)。
    // box/polygo 是官方结果树的父节点，数值都在子项里：本表用同一组子字段表达，
    // 不另造「匹配框」聚合值；图像类结果（输出掩膜图像、轮廓 pointset）不画数值行，
    // 图像本身走输出对象上的具名子结果。配置参数一律不进结果表。
    const QString group = QStringLiteral("匹配%1");
    const QString maskGroup = QStringLiteral("屏蔽区多边形");

    QVector<NodeResultField> fields;
    fields << resultValue(QStringLiteral("elapsedMs"), QStringLiteral("耗时"),
                          QStringLiteral("ms"), 0)
           << resultStatus(QStringLiteral("moduleStatus"), QStringLiteral("模块状态"))
           << resultValue(QStringLiteral("matchStatus"), QStringLiteral("匹配状态"),
                          QString(), 0)
           << resultValue(QStringLiteral("matchCount"), QStringLiteral("匹配个数"),
                          QString(), 0)
           << resultValue(QStringLiteral("matchTemplateIndex"), QStringLiteral("匹配模板编号"),
                          QString(), 0)
           << resultValue(QStringLiteral("matchTemplateName"), QStringLiteral("匹配模板名称"))
           << resultPoint(QStringLiteral("matchBoxCenters"), group, QStringLiteral("匹配框中心"), 2)
           << resultList(QStringLiteral("matchBoxCenterXs"), group, QStringLiteral("匹配框中心X"),
                         QStringLiteral("px"), 2)
           << resultList(QStringLiteral("matchBoxCenterYs"), group, QStringLiteral("匹配框中心Y"),
                         QStringLiteral("px"), 2)
           << resultList(QStringLiteral("matchBoxWidths"), group, QStringLiteral("匹配框宽度"),
                         QStringLiteral("px"), 2)
           << resultList(QStringLiteral("matchBoxHeights"), group, QStringLiteral("匹配框高度"),
                         QStringLiteral("px"), 2)
           << resultList(QStringLiteral("matchBoxAngles"), group, QStringLiteral("角度"),
                         QStringLiteral("°"), 2)
           << resultPoint(QStringLiteral("matchPoints"), group, QStringLiteral("匹配点"), 2)
           << resultList(QStringLiteral("matchPointXs"), group, QStringLiteral("匹配点X"),
                         QStringLiteral("px"), 2)
           << resultList(QStringLiteral("matchPointYs"), group, QStringLiteral("匹配点Y"),
                         QStringLiteral("px"), 2)
           << resultList(QStringLiteral("matchScales"), group, QStringLiteral("尺度"),
                         QString(), 4)
           << resultList(QStringLiteral("matchScaleXs"), group, QStringLiteral("尺度X"),
                         QString(), 4)
           << resultList(QStringLiteral("matchScaleYs"), group, QStringLiteral("尺度Y"),
                         QString(), 4)
           << resultList(QStringLiteral("matchCrossAngles"), group, QStringLiteral("横切值"),
                         QStringLiteral("°"), 2)
           << resultList(QStringLiteral("matchScores"), group, QStringLiteral("分数"),
                         QString(), 4)
           << resultValue(QStringLiteral("searchCenterX"), VmSharedText::roiCenterXLabel(),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("searchCenterY"), VmSharedText::roiCenterYLabel(),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("searchWidth"), QStringLiteral("检测区域宽度"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("searchHeight"), QStringLiteral("检测区域高度"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("searchAngle"), QStringLiteral("检测区域角度"),
                          QStringLiteral("°"), 3)
           << resultValue(QStringLiteral("matchContourCount"), QStringLiteral("匹配轮廓点个数"),
                          QString(), 0)
           << resultList(QStringLiteral("matchContourPointXs"), group,
                         QStringLiteral("轮廓点X"), QStringLiteral("px"), 2)
           << resultList(QStringLiteral("matchContourPointYs"), group,
                         QStringLiteral("轮廓点Y"), QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("maskPolygonCount"), QStringLiteral("多边形点数"),
                          QString(), 0)
           << resultPoint(QStringLiteral("maskPolygonPoints"), maskGroup,
                          QStringLiteral("多边形点集"), 2)
           << resultList(QStringLiteral("maskPolygonXs"), maskGroup,
                         QStringLiteral("多边形点X"), QStringLiteral("px"), 2)
           << resultList(QStringLiteral("maskPolygonYs"), maskGroup,
                         QStringLiteral("多边形点Y"), QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("outMaskWidth"), QStringLiteral("输出掩膜宽度"),
                          QStringLiteral("px"), 0)
           << resultValue(QStringLiteral("outMaskHeight"), QStringLiteral("输出掩膜高度"),
                          QStringLiteral("px"), 0)
           << resultValue(QStringLiteral("outMaskPixelFormat"),
                          QStringLiteral("输出掩膜像素格式"), QString(), 0);
    return fields;
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
    // 唯一几何表示：中心 + 宽高 + 角度（不再保留 roiCol/roiRow 旧左上角写法）
    s.p1 = QPointF(m_params.value(QStringLiteral("roiCenterCol"), 0.0).toDouble(),
                   m_params.value(QStringLiteral("roiCenterRow"), 0.0).toDouble());
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
        setParam(QStringLiteral("roiCenterCol"), shape.p1.x());
        setParam(QStringLiteral("roiCenterRow"), shape.p1.y());
        setParam(QStringLiteral("roiWidth"), qMax(1, int(shape.width + 0.5)));
        setParam(QStringLiteral("roiHeight"), qMax(1, int(shape.height + 0.5)));
        setParam(QStringLiteral("roiAngle"), shape.angleDeg);
        // 注意：这里不再自动把 trainFromImage 置 true。
        // VM 的流程是"先框区域 → 再点训练"；框完就自动训练会让用户以为训练按钮是摆设。
        return;
    }
    if (shape.type == RoiType::Rect) {
        const QRectF r = roiAxisAlignedBounds(shape);
        if (r.width() <= 1 || r.height() <= 1)
            return;
        setParam(QStringLiteral("roiCenterCol"), r.center().x());
        setParam(QStringLiteral("roiCenterRow"), r.center().y());
        setParam(QStringLiteral("roiWidth"), qMax(1, int(r.width() + 0.5)));
        setParam(QStringLiteral("roiHeight"), qMax(1, int(r.height() + 0.5)));
        setParam(QStringLiteral("roiAngle"), 0.0);
        // 训练由「训练模板」按钮显式触发（见 ModuleEditorDialog::onTrainTemplate）。
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
        // 单模板模式：训练模板直接落盘到 templatePath、还没有模板列表时，运行时合成一条。
        // 称呼已不再是"老方案兼容"：这是「训练模板」按钮的既定工作模式。
        const QString path =
            m_params.value(QStringLiteral("templatePath"), QString()).toString().trimmed();
        if (!path.isEmpty()) {
            TemplateEntry e;
            e.name = QFileInfo(path).fileName();
            e.imagePath = path;
            e.shape = geometryRoi();
            e.contrastThreshold =
                qBound(1, m_params.value(QStringLiteral("contrastThreshold"), 5).toInt(), 255);
            e.autoThreshold =
                m_params.value(QStringLiteral("autoContrastThreshold"), false).toBool();
            e.featureScale =
                qBound(1, m_params.value(QStringLiteral("featureScale"), 1).toInt(), 20);
            e.speedScale = qBound(1, m_params.value(QStringLiteral("speedScale"), 3).toInt(), 20);
            e.scaleModeAuto =
                m_params.value(QStringLiteral("scaleMode"), 1).toInt() == 1;
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
        e.contrastThreshold =
            qBound(1, m_params.value(QStringLiteral("contrastThreshold"), 5).toInt(), 255);
        e.autoThreshold = m_params.value(QStringLiteral("autoContrastThreshold"), false).toBool();
        e.featureScale = qBound(1, m_params.value(QStringLiteral("featureScale"), 1).toInt(), 20);
        e.speedScale = qBound(1, m_params.value(QStringLiteral("speedScale"), 3).toInt(), 20);
        e.scaleModeAuto = m_params.value(QStringLiteral("scaleMode"), 1).toInt() == 1;
        e.minChainLength =
            qBound(1, m_params.value(QStringLiteral("minChainLength"), 1).toInt(), 200);
        e.chainModeAuto = m_params.value(QStringLiteral("chainMode"), false).toBool();
        return e;
    }
    return list.at(activeTemplateIndex());
}

quint64 OpencvTemplateMatchNode::templateFingerprint(const cv::Mat &tmpl)
{
    // FNV-1a 全像素扫描。用途：变形缓存的键里必须含**内容**，不能只有「尺寸 + 路径」
    // —— 同一路径换图重训、或两个同尺寸的内存模板，会共用同一个键并拿到上一张模板的
    // 旋转/缩放结果（复核 P1-4）。模板通常几百像素边长，每帧一次全扫在微秒级，
    // 相对匹配本身可忽略。
    quint64 h = 1469598103934665603ull;   // FNV-1a offset basis
    if (tmpl.empty())
        return h;
    const size_t rowBytes = size_t(tmpl.cols) * tmpl.elemSize();
    for (int y = 0; y < tmpl.rows; ++y) {
        const uchar *row = tmpl.ptr<uchar>(y);
        for (size_t i = 0; i < rowBytes; ++i) {
            h ^= quint64(row[i]);
            h *= 1099511628211ull;        // FNV prime
        }
    }
    h ^= quint64(tmpl.type());
    return h;
}

QPointF OpencvTemplateMatchNode::mapAnchorToImage(double centerX, double centerY,
                                                 double angleDeg, double anchorDx,
                                                 double anchorDy, double scale,
                                                 double crossAngleDeg, double scaleY)
{
    // 锚点即"相对模板中心的偏移"，与轮廓点共用同一映射（M = R·S·H）。
    // scaleY < 0 是"单尺度"哨兵（兼容旧调用与几何腿）；XY 尺度模式下调用方
    // 传 bestScaleY，否则匹配点 Y 方向偏移会错用 X 尺度。
    const double sy = scaleY < 0.0 ? scale : scaleY;
    return mapTemplatePoint(centerX, centerY, angleDeg, anchorDx, anchorDy, scale, sy,
                            crossAngleDeg);
}

QPointF OpencvTemplateMatchNode::mapTemplatePoint(double centerX, double centerY,
                                                  double angleDeg, double dx, double dy,
                                                  double scaleX, double scaleY,
                                                  double crossAngleDeg)
{
    // 与 warpTemplate 同源的线性部分：cv::getRotationMatrix2D(center, angle, 1.0)
    // 返回的 2×2 是 [cos sin; -sin cos]。模板局部点必须经**同一个**矩阵映射，
    // 否则角度非 0 时轮廓点相对中心镜像（差 2θ）。
    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
    const double ca = std::cos(rad);
    const double sa = std::sin(rad);
    // M = R·S·H：横切先把局部偏移 (dx,dy) 变成 (dx + tanθ·dy, dy)，再缩放、再旋转。
    const double t = std::tan(crossAngleDeg * 3.14159265358979323846 / 180.0);
    const double ax = (dx + t * dy) * scaleX;
    const double ay = dy * scaleY;
    return QPointF(centerX + ca * ax + sa * ay,    // x = col
                   centerY - sa * ax + ca * ay);   // y = row
}

double OpencvTemplateMatchNode::effectiveEdgeThresholdFor(const cv::Mat &tmpl, int aperture,
                                                          double manualGrayDiff, bool autoMode)
{
    // aperture 来自参数窗「特征尺度 1~20」下拉，直接当 Sobel 的 ksize 会让 OpenCV
    // 抛异常（ksize 只接受 1/3/5/7 这类奇数，2/4/6 直接报错，而自动阈值默认开启，
    // 打开参数窗即可触发）。这里是所有调用方的汇合点，统一钳位成合法奇数，
    // 与另一处 qBound(3, aperture, 7) | 1 的口径保持一致。
    aperture = qBound(3, aperture, 7) | 1;

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
                                 featureScale, minChain, active.erases);
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
    m_params[QStringLiteral("autoContrastThreshold")] = s.autoThreshold;
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
                                                               int featureScale, int minChain,
                                                               const QVector<RoiShape> &erases)
{
    if (tmpl.empty())
        return {};
    cv::Mat gx, gy, mag;
    cv::Sobel(tmpl, gx, CV_32F, 1, 0, qBound(3, aperture, 7) | 1);
    cv::Sobel(tmpl, gy, CV_32F, 0, 1, qBound(3, aperture, 7) | 1);
    cv::magnitude(gx, gy, mag);
    // 与打分同一套提纯：最小链长 + 特征尺度稀疏化（否则"看到的点"和"打分的点"又分叉）
    cv::Mat strong = thinEdgeMask(gx, gy, mag, threshold);
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
            QPointF imgPt;
            if (axisAligned) {
                imgPt = QPointF(x0 + u, y0 + v);
            } else {
                const double lx = u - w * 0.5 + 0.5;
                const double ly = v - h * 0.5 + 0.5;
                imgPt = QPointF(cx + ca * lx - sa * ly, cy + sa * lx + ca * ly);
            }
            // 擦除轮廓点（VM）：落在擦除圆内的特征点不参与匹配
            bool erased = false;
            for (const RoiShape &e : erases) {
                if (e.type != RoiType::Circle)
                    continue;
                const double r = std::hypot(e.p2.x() - e.p1.x(), e.p2.y() - e.p1.y());
                if (r > 1.0 && QLineF(e.p1, imgPt).length() <= r + 0.5) {
                    erased = true;
                    break;
                }
            }
            if (!erased)
                pts.append(imgPt);
        }
    }
    return pts;
}
