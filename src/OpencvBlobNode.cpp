#include "OpencvBlobNode.h"
#include "NodeResultFields.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QRectF>
#include <QComboBox>
#include <QFormLayout>
#include <QSet>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <QWidget>

namespace {

/// 给参数补分组标题（自动面板按组渲染）
ParamSpec grouped(ParamSpec spec, const QString &group)
{
    spec.group = group;
    return spec;
}

/// 填平面积小于 holeMinArea 的**内部孔洞**（与图像边界相连的背景不是孔洞）。
/// 先对反相图做一次连通域，再按标签回填前景——全图两遍扫描，不按孔洞逐个 ROI。
void fillSmallHoles(cv::Mat &bin, int holeMinArea)
{
    if (holeMinArea <= 0 || bin.empty())
        return;
    cv::Mat inv;
    cv::bitwise_not(bin, inv);
    cv::Mat holeLabels, holeStats, holeCentroids;
    const int hn = cv::connectedComponentsWithStats(inv, holeLabels, holeStats, holeCentroids, 4);
    if (hn <= 1)
        return;

    // 与边界相连的背景（含 label 0）不是孔洞，不能填
    std::vector<uchar> touchesBorder(static_cast<size_t>(hn), 0);
    for (int x = 0; x < holeLabels.cols; ++x) {
        touchesBorder[static_cast<size_t>(holeLabels.at<int>(0, x))] = 1;
        touchesBorder[static_cast<size_t>(holeLabels.at<int>(holeLabels.rows - 1, x))] = 1;
    }
    for (int y = 0; y < holeLabels.rows; ++y) {
        touchesBorder[static_cast<size_t>(holeLabels.at<int>(y, 0))] = 1;
        touchesBorder[static_cast<size_t>(holeLabels.at<int>(y, holeLabels.cols - 1))] = 1;
    }
    std::vector<uchar> fill(static_cast<size_t>(hn), 0);
    for (int i = 1; i < hn; ++i) {
        if (touchesBorder[static_cast<size_t>(i)])
            continue;
        if (holeStats.at<int>(i, cv::CC_STAT_AREA) < holeMinArea)
            fill[static_cast<size_t>(i)] = 1;
    }

    for (int y = 0; y < bin.rows; ++y) {
        const int *hp = holeLabels.ptr<int>(y);
        uchar *bp = bin.ptr<uchar>(y);
        for (int x = 0; x < bin.cols; ++x) {
            if (bp[x] == 0 && fill[static_cast<size_t>(hp[x])])
                bp[x] = 255;
        }
    }
}

/// Blob 形状度量（一次轮廓扫描算齐 VM 的周长/长轴/短轴/矩形度/质心偏移/
/// box 角度/二阶中心矩主轴角度/圆形度）。
/// 官方口径来源：本机 VM 4.4 帮助「Blob分析」GUID-F5B9D37D-...html
///   - 圆形度 = 连通域面积 / (π·d²)，d = 质心到轮廓点最大距离，圆为 1
///   - 矩形度 = 连通域面积 / 最小外接矩形面积，矩形为 1
///   - 角度：顺时针为正、逆时针为负
///   - 排序特征「二阶中心距主轴角度」与「box角度」是两项独立特征
// 度量类型 BlobShapeMetrics 定义在头文件（测试要直接断言数值），匿名命名空间里只放实现。
BlobShapeMetrics vmMeasureShape(const cv::Mat &labels, int label, const int *stat,
                                double cx, double cy)
{
    BlobShapeMetrics m;
    const int x = stat[cv::CC_STAT_LEFT];
    const int y = stat[cv::CC_STAT_TOP];
    const int w = stat[cv::CC_STAT_WIDTH];
    const int h = stat[cv::CC_STAT_HEIGHT];
    const int area = stat[cv::CC_STAT_AREA];
    if (w <= 0 || h <= 0 || area <= 0)
        return m;

    cv::Mat mask;
    cv::compare(labels(cv::Rect(x, y, w, h)), cv::Scalar(label), mask, cv::CMP_EQ);
    std::vector<std::vector<cv::Point>> contours;
    // 关键：轮廓必须转到**全图坐标**（centroids 的 cx/cy 是全图坐标）。
    // 少了 offset 时 box.center 与轮廓点都是子图相对坐标，圆形度和质心偏移
    // 会随 Blob 在图中的位置变化——同一个形状平移后度量不同。
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE,
                     cv::Point(x, y));

    const std::vector<cv::Point> *best = nullptr;
    double bestArea = -1.0;
    for (const auto &c : contours) {
        const double a = cv::contourArea(c);
        if (a > bestArea) {
            bestArea = a;
            best = &c;
        }
    }
    if (!best || best->size() < 3)
        return m;

    m.perimeter = cv::arcLength(*best, true);
    const cv::RotatedRect box = cv::minAreaRect(*best);
    m.longAxis = std::max(box.size.width, box.size.height);
    m.shortAxis = std::min(box.size.width, box.size.height);
    m.boxWidth = box.size.width;
    m.boxHeight = box.size.height;
    // 矩形度：S1 = 连通域像素面积，S2 = 最小外接矩形面积。
    // minAreaRect 的 size 是轮廓点跨度（几何长度），比像素计数少 1；直接相除会让
    // 小方块算出 >1 的矩形度，被 std::min 掩盖成 1，使筛选形同虚设（3×3 块：
    // area=9、box=2×2=4 → 2.25）。按像素口径 +1 修正后 9/(3×3)=1。
    const double boxAreaPx = (box.size.width + 1.0) * (box.size.height + 1.0);
    m.rectangularity = (boxAreaPx > 0.0) ? std::min(1.0, double(area) / boxAreaPx) : 0.0;
    m.centerBias = std::hypot(cx - box.center.x, cy - box.center.y);

    // VM：box 角度 = 最小外接矩形**长轴**与 x 轴夹角。OpenCV minAreaRect 对
    // 轴对齐的水平矩形会返回 (w<h, angle=-90)，直接归一化得到 -90 而不是 0；
    // size 宽高互换时补 90° 才是长轴方向。
    double angle = box.angle;
    if (box.size.width < box.size.height)
        angle += 90.0;
    while (angle <= -90.0) angle += 180.0;   // 图像坐标 y 向下 → 正值即顺时针（VM 口径）
    while (angle > 90.0) angle -= 180.0;
    m.boxAngle = angle;

    // 二阶中心矩主轴角度（VM「二阶中心距主轴角度」，与 boxAngle 独立）：
    // θ = 0.5·atan2(2μ11, μ20-μ02)，由全像素分布决定，连续、无长短边互换的 90° 跳变。
    // 圆/正方形等各向同性形状 μ 退化时 atan2(0,0)=0，稳定输出 0°。
    // binaryImage=true：mask 的 0/255 当作 0/1；中心矩平移不变，子图上算即可。
    const cv::Moments mu = cv::moments(mask, true);
    if (mu.m00 > 0.0) {
        // 必须用中心矩 mu20/mu02/mu11；m20/m02 是原始矩（含平移项），混用会让
        // 角度既不是真实倾角、又随 Blob 位置漂移。
        double axis = 0.5 * std::atan2(2.0 * mu.mu11, mu.mu20 - mu.mu02) * 180.0 / CV_PI;
        while (axis <= -90.0) axis += 180.0;
        while (axis > 90.0) axis -= 180.0;
        m.axisAngle = axis;
    }

    double maxDist = 0.0;
    for (const cv::Point &p : *best)
        maxDist = std::max(maxDist, std::hypot(double(p.x) - cx, double(p.y) - cy));
    if (maxDist > 0.0)
        m.circularity = std::min(1.0, double(area) / (CV_PI * maxDist * maxDist));
    return m;
}

/// 通过筛选的连通域（后续排序 / 截断 / 输出都基于这张表）
struct Candidate {
    int label = 0;
    int area = 0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    double cx = 0.0;
    double cy = 0.0;
    double perimeter = 0.0;
    double circularity = 0.0;
    double rectangularity = 0.0;
    double boxAngle = 0.0;
    double boxWidth = 0.0;
    double boxHeight = 0.0;
    double axisAngle = 0.0;
    double axisRatio = 0.0;
};

/// VM「最大内接矩形」：连通域内最大面积**轴对齐**内接矩形。
/// 逐行累积连续前景高度，再对每行用单调栈解 largest-rectangle-in-histogram，O(w·h)。
/// 返回全图坐标 (x,y,w,h)；无前景时返回空矩形。
cv::Rect largestInscribedRect(const cv::Mat &labels, int label, const cv::Rect &bbox)
{
    cv::Rect best(0, 0, 0, 0);
    if (bbox.width <= 0 || bbox.height <= 0)
        return best;
    std::vector<int> heights(bbox.width, 0);
    int bestArea = 0;
    for (int yy = 0; yy < bbox.height; ++yy) {
        const int *lp = labels.ptr<int>(bbox.y + yy);
        for (int xx = 0; xx < bbox.width; ++xx)
            heights[xx] = (lp[bbox.x + xx] == label) ? heights[xx] + 1 : 0;
        std::vector<int> h(heights);
        h.push_back(0);                    // 哨兵，逼出栈里最后一段
        std::vector<int> st;
        for (int xx = 0; xx <= bbox.width; ++xx) {
            while (!st.empty() && h[st.back()] >= h[xx]) {
                const int top = st.back();
                st.pop_back();
                const int left = st.empty() ? 0 : st.back() + 1;
                const int ww = xx - left;
                const int hh = h[top];
                if (hh > 0 && ww * hh > bestArea) {
                    bestArea = ww * hh;
                    best = cv::Rect(bbox.x + left, bbox.y + yy - hh + 1, ww, hh);
                }
            }
            st.push_back(xx);
        }
    }
    return best;
}

}   // namespace

BlobShapeMetrics OpencvBlobNode::measureShape(const cv::Mat &labels, int label,
                                              const int *stat, double cx, double cy)
{
    return vmMeasureShape(labels, label, stat, cx, cy);
}

OpencvBlobNode::OpencvBlobNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("Blob分析"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvBlobNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("标记图"), PortDataType::Image);
    addOutputPort(QStringLiteral("Blob数量"), PortDataType::Number);
    addOutputPort(QStringLiteral("面积"), PortDataType::Number);   // 🥇 暴露最大Blob面积端口（替代被禁的 HALCON AreaNode）
    // 每个保留 Blob 一条 "rect" 测量结果：画布上叠加检测框（结果表由参数字段承载）
    addOutputPort(QStringLiteral("Blob列表"), PortDataType::Measure);
    registerParams({
        // ---- 阈值（对标 VM Blob：阈值方式 / 极性 / 阈值参数）----
        grouped(makeEnumParam(QStringLiteral("thresholdType"), 1,
                              {QStringLiteral("不进行二值化"), QStringLiteral("单阈值"),
                               QStringLiteral("双阈值"), QStringLiteral("自动阈值"),
                               QStringLiteral("软阈值（固定）"), QStringLiteral("软阈值（相对）")},
                              QStringLiteral("阈值方式")),
                QStringLiteral("阈值")),
        grouped(makeEnumParam(QStringLiteral("polarity"), 0,
                              {QStringLiteral("亮于背景"), QStringLiteral("暗于背景")},
                              QStringLiteral("极性")),
                QStringLiteral("阈值")),
        grouped(makeIntParam(QStringLiteral("lowThreshold"), 100, 0, 255,
                             QStringLiteral("低阈值")),
                QStringLiteral("阈值")),
        grouped(makeIntParam(QStringLiteral("highThreshold"), 150, 0, 255,
                             QStringLiteral("高阈值")),
                QStringLiteral("阈值")),
        grouped(makeIntParam(QStringLiteral("softLowThreshold"), 100, 0, 255,
                             QStringLiteral("软阈值低阈值（固定）")),
                QStringLiteral("阈值")),
        grouped(makeIntParam(QStringLiteral("softHighThreshold"), 150, 0, 255,
                             QStringLiteral("软阈值高阈值（固定）")),
                QStringLiteral("阈值")),
        grouped(makeIntParam(QStringLiteral("softness"), 254, 0, 255,
                             QStringLiteral("软阈值柔和度")),
                QStringLiteral("阈值")),
        grouped(makeIntParam(QStringLiteral("softLeftRatio"), 5, 0, 100,
                             QStringLiteral("低尾部比例")),
                QStringLiteral("阈值")),
        grouped(makeIntParam(QStringLiteral("softRightRatio"), 5, 0, 100,
                             QStringLiteral("高尾部比例")),
                QStringLiteral("阈值")),
        grouped(makeIntParam(QStringLiteral("softLowRatio"), 40, 0, 100,
                             QStringLiteral("低阈值比例")),
                QStringLiteral("阈值")),
        grouped(makeIntParam(QStringLiteral("softHighRatio"), 60, 0, 100,
                             QStringLiteral("高阈值比例")),
                QStringLiteral("阈值")),
        // ---- 连通域与形状筛选（对标 VM：每项 enable + min/max）----
        grouped(makeBoolParam(QStringLiteral("selectByArea"), true, QStringLiteral("面积使能")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("minArea"), 10, 0, 1000000000,
                             QStringLiteral("最小面积"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("maxArea"), 999999999, 0, 1000000000,
                             QStringLiteral("最大面积"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeBoolParam(QStringLiteral("selectByRectWidth"), false, QStringLiteral("外接宽使能")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("minRectWidth"), 100, 0, 100000,
                             QStringLiteral("最小外接宽"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("maxRectWidth"), 200, 0, 100000,
                             QStringLiteral("最大外接宽"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeBoolParam(QStringLiteral("selectByRectHeight"), false, QStringLiteral("外接高使能")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("minRectHeight"), 100, 0, 100000,
                             QStringLiteral("最小外接高"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("maxRectHeight"), 200, 0, 100000,
                             QStringLiteral("最大外接高"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeBoolParam(QStringLiteral("selectByPerimeter"), false, QStringLiteral("周长使能")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("minPerimeter"), 10.0, 0.0, 1e9,
                                QStringLiteral("最小周长")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("maxPerimeter"), 999999999.0, 0.0, 1e9,
                                QStringLiteral("最大周长")),
                QStringLiteral("筛选")),
        grouped(makeBoolParam(QStringLiteral("selectByShortAxis"), false, QStringLiteral("短轴使能")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("minShortAxis"), 1.0, 0.0, 1e9,
                                QStringLiteral("最小短轴")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("maxShortAxis"), 999999999.0, 0.0, 1e9,
                                QStringLiteral("最大短轴")),
                QStringLiteral("筛选")),
        grouped(makeBoolParam(QStringLiteral("selectByLongAxis"), false, QStringLiteral("长轴使能")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("minLongAxis"), 10.0, 0.0, 1e9,
                                QStringLiteral("最小长轴")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("maxLongAxis"), 999999999.0, 0.0, 1e9,
                                QStringLiteral("最大长轴")),
                QStringLiteral("筛选")),
        grouped(makeBoolParam(QStringLiteral("selectByCircularity"), false, QStringLiteral("圆形度使能")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("minCircularity"), 0.1, 0.0, 1.0,
                                QStringLiteral("最小圆形度")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("maxCircularity"), 1.0, 0.0, 1.0,
                                QStringLiteral("最大圆形度")),
                QStringLiteral("筛选")),
        grouped(makeBoolParam(QStringLiteral("selectByRectangularity"), false, QStringLiteral("矩形度使能")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("minRectangularity"), 0.1, 0.0, 1.0,
                                QStringLiteral("最小矩形度")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("maxRectangularity"), 1.0, 0.0, 1.0,
                                QStringLiteral("最大矩形度")),
                QStringLiteral("筛选")),
        grouped(makeBoolParam(QStringLiteral("selectByCentraBias"), false, QStringLiteral("质心偏移使能")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("minCenterBias"), 0.0, 0.0, 1e9,
                                QStringLiteral("最小质心偏移"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("maxCenterBias"), 9999999.99, 0.0, 1e9,
                                QStringLiteral("最大质心偏移"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeBoolParam(QStringLiteral("axisRatioEnable"), false, QStringLiteral("轴比使能")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("minAxisRatio"), 0.01, 0.0, 1.0,
                                QStringLiteral("最小轴比")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("maxAxisRatio"), 1.0, 0.0, 1.0,
                                QStringLiteral("最大轴比")),
                QStringLiteral("筛选")),
        grouped(makeBoolParam(QStringLiteral("selectByBoxAngle"), false, QStringLiteral("box 角度使能")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("minBoxAngle"), -90.0, -180.0, 180.0,
                                QStringLiteral("最小 box 角度"), QStringLiteral("°")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("maxBoxAngle"), 90.0, -180.0, 180.0,
                                QStringLiteral("最大 box 角度"), QStringLiteral("°")),
                QStringLiteral("筛选")),
        grouped(makeEnumParam(QStringLiteral("connectivity"), 0,
                              {QStringLiteral("8 邻域"), QStringLiteral("4 邻域")},
                              QStringLiteral("连通性")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("holeMinArea"), 0, 0, 1000000,
                             QStringLiteral("填充面积阈值"),
                             QStringLiteral("px")),
                QStringLiteral("筛选")),
        // ---- 结果数量与顺序 ----
        grouped(makeIntParam(QStringLiteral("findNum"), 100, 1, 20000,
                             QStringLiteral("查找个数")),
                QStringLiteral("输出")),
        grouped(makeEnumParam(QStringLiteral("sortFeature"), 0,
                              {QStringLiteral("面积"), QStringLiteral("周长"),
                               QStringLiteral("圆形度"), QStringLiteral("矩形度"),
                               QStringLiteral("质心X"), QStringLiteral("质心Y"),
                               QStringLiteral("box角度"), QStringLiteral("box宽度"),
                               QStringLiteral("box高度"), QStringLiteral("矩形左上X"),
                               QStringLiteral("矩形左上Y"), QStringLiteral("主轴角度"),
                               QStringLiteral("轴比")},
                              QStringLiteral("排序特征")),
                QStringLiteral("输出")),
        grouped(makeEnumParam(QStringLiteral("sortMode"), 1,
                              {QStringLiteral("升序"), QStringLiteral("降序"),
                               QStringLiteral("不排序")},
                              QStringLiteral("排序方式")),
                QStringLiteral("输出")),
        grouped(makeBoolParam(QStringLiteral("okWhenNumIsZero"), false,
                              QStringLiteral("无目标时判定 OK")),
                QStringLiteral("结果判断")),
        grouped(makeBoolParam(QStringLiteral("blobNumLimitEnable"), false,
                              QStringLiteral("Blob 数量检查")),
                QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("blobNumLimitLow"), 1, 0, 99999,
                             QStringLiteral("Blob 数量下限")),
                QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("blobNumLimitHigh"), 99999, 0, 99999,
                             QStringLiteral("Blob 数量上限")),
                QStringLiteral("结果判断")),
        grouped(makeBoolParam(QStringLiteral("blobAreaLimitEnable"), false,
                              QStringLiteral("Blob 面积检查")),
                QStringLiteral("结果判断")),
        grouped(makeDoubleParam(QStringLiteral("blobAreaLimitLow"), 1.0, 0.0, 1e9,
                                QStringLiteral("Blob 面积下限")),
                QStringLiteral("结果判断")),
        grouped(makeDoubleParam(QStringLiteral("blobAreaLimitHigh"), 999999999.0, 0.0, 1e9,
                                QStringLiteral("Blob 面积上限")),
                QStringLiteral("结果判断")),
        grouped(makeBoolParam(QStringLiteral("blobTotalAreaLimitEnable"), false,
                              QStringLiteral("Blob 总面积检查")),
                QStringLiteral("结果判断")),
        grouped(makeDoubleParam(QStringLiteral("blobTotalAreaLimitLow"), 1.0, 0.0, 1e9,
                                QStringLiteral("总面积下限")),
                QStringLiteral("结果判断")),
        grouped(makeDoubleParam(QStringLiteral("blobTotalAreaLimitHigh"), 999999999.0, 0.0, 1e9,
                                QStringLiteral("总面积上限")),
                QStringLiteral("结果判断")),
        grouped(makeBoolParam(QStringLiteral("showBlobBox"), true,
                              QStringLiteral("显示 Blob 检测框")),
                QStringLiteral("输出")),
        // ---- VM 4.4「Blob分析」剩余四项（官方口径 GUID-F5B9D37D-...）----
        // 像素超界：Blob 中与图像/ROI/掩膜边缘重合的像素 > 最大超界像素时删除该 Blob
        grouped(makeBoolParam(QStringLiteral("boundaryPixelEnable"), false,
                              QStringLiteral("像素超界使能")),
                QStringLiteral("输出")),
        grouped(makeIntParam(QStringLiteral("maxBoundaryPixels"), 0, 0, 999999,
                             QStringLiteral("最大超界像素")),
                QStringLiteral("输出")),
        // 最大内接矩形输出数量：计算前 N 个 Blob 的最大内接矩形
        grouped(makeIntParam(QStringLiteral("maxInscribedRectCount"), 0, 0, 100,
                             QStringLiteral("最大内接矩形输出数量")),
                QStringLiteral("输出")),
        // 轮廓输出使能：开启后在标记图上画出 Blob 轮廓
        grouped(makeBoolParam(QStringLiteral("contourOutputEnable"), false,
                              QStringLiteral("轮廓输出使能")),
                QStringLiteral("输出")),
        // Blob 图像输出：关闭后不产出分析后的标记图
        grouped(makeBoolParam(QStringLiteral("blobImageOutput"), true,
                              QStringLiteral("Blob 图像输出")),
                QStringLiteral("输出")),
    });

    setParamDirect(QStringLiteral("blobCount"), 0);
    setParamDirect(QStringLiteral("totalBlobArea"), 0.0);
    setParamDirect(QStringLiteral("minBlobArea"), 0.0);
    setParamDirect(QStringLiteral("maxBlobArea"), 0.0);
    setParamDirect(QStringLiteral("meanBlobArea"), 0.0);
    setParamDirect(QStringLiteral("maxBlobCentroidRow"), 0.0);
    setParamDirect(QStringLiteral("maxBlobCentroidCol"), 0.0);
    setParamDirect(QStringLiteral("maxBlobWidth"), 0.0);
    setParamDirect(QStringLiteral("maxBlobHeight"), 0.0);
    setParamDirect(QStringLiteral("otsuThreshold"), 0.0);
    setParamDirect(QStringLiteral("inscribedRects"), QString());
}

void OpencvBlobNode::run(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat mat = OpencvUtil::himageToMat(input);
        if (mat.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (mat.channels() == 3) {
            cv::cvtColor(mat, mat, cv::COLOR_BGR2GRAY);
        }
        QString depthWhy;
        if (!OpencvUtil::requireGray8(mat, &depthWhy))
            throw std::runtime_error(depthWhy.toStdString());

        const int thresholdType = m_params.value(QStringLiteral("thresholdType"), 1).toInt();
        const int polarity = m_params.value(QStringLiteral("polarity"), 0).toInt();
        int lowThreshold = m_params.value(QStringLiteral("lowThreshold"), 100).toInt();
        int highThreshold = m_params.value(QStringLiteral("highThreshold"), 150).toInt();
        const int softLow = m_params.value(QStringLiteral("softLowThreshold"), 100).toInt();
        const int softHigh = m_params.value(QStringLiteral("softHighThreshold"), 150).toInt();
        const int softness = m_params.value(QStringLiteral("softness"), 254).toInt();
        const int softLeftRatio = m_params.value(QStringLiteral("softLeftRatio"), 5).toInt();
        const int softRightRatio = m_params.value(QStringLiteral("softRightRatio"), 5).toInt();
        const int softLowRatio = m_params.value(QStringLiteral("softLowRatio"), 40).toInt();
        const int softHighRatio = m_params.value(QStringLiteral("softHighRatio"), 60).toInt();
        const bool selectArea = m_params.value(QStringLiteral("selectByArea"), true).toBool();
        const int minArea = m_params.value(QStringLiteral("minArea"), 10).toInt();
        const int maxArea = m_params.value(QStringLiteral("maxArea"), 999999999).toInt();
        const bool selectRectWidth = m_params.value(QStringLiteral("selectByRectWidth"), false).toBool();
        const int minRectWidth = m_params.value(QStringLiteral("minRectWidth"), 100).toInt();
        const int maxRectWidth = m_params.value(QStringLiteral("maxRectWidth"), 200).toInt();
        const bool selectRectHeight = m_params.value(QStringLiteral("selectByRectHeight"), false).toBool();
        const int minRectHeight = m_params.value(QStringLiteral("minRectHeight"), 100).toInt();
        const int maxRectHeight = m_params.value(QStringLiteral("maxRectHeight"), 200).toInt();
        const bool selectPerimeter = m_params.value(QStringLiteral("selectByPerimeter"), false).toBool();
        const double minPerimeter = m_params.value(QStringLiteral("minPerimeter"), 10.0).toDouble();
        const double maxPerimeter = m_params.value(QStringLiteral("maxPerimeter"), 999999999.0).toDouble();
        const bool selectShortAxis = m_params.value(QStringLiteral("selectByShortAxis"), false).toBool();
        const double minShortAxis = m_params.value(QStringLiteral("minShortAxis"), 1.0).toDouble();
        const double maxShortAxis = m_params.value(QStringLiteral("maxShortAxis"), 999999999.0).toDouble();
        const bool selectLongAxis = m_params.value(QStringLiteral("selectByLongAxis"), false).toBool();
        const double minLongAxis = m_params.value(QStringLiteral("minLongAxis"), 10.0).toDouble();
        const double maxLongAxis = m_params.value(QStringLiteral("maxLongAxis"), 999999999.0).toDouble();
        const bool selectCircularity = m_params.value(QStringLiteral("selectByCircularity"), false).toBool();
        const double minCircularity = m_params.value(QStringLiteral("minCircularity"), 0.1).toDouble();
        const double maxCircularity = m_params.value(QStringLiteral("maxCircularity"), 1.0).toDouble();
        const bool selectRectangularity = m_params.value(QStringLiteral("selectByRectangularity"), false).toBool();
        const double minRectangularity = m_params.value(QStringLiteral("minRectangularity"), 0.1).toDouble();
        const double maxRectangularity = m_params.value(QStringLiteral("maxRectangularity"), 1.0).toDouble();
        const bool selectCenterBias = m_params.value(QStringLiteral("selectByCentraBias"), false).toBool();
        const double minCenterBias = m_params.value(QStringLiteral("minCenterBias"), 0.0).toDouble();
        const double maxCenterBias = m_params.value(QStringLiteral("maxCenterBias"), 9999999.99).toDouble();
        const bool selectAxisRatio = m_params.value(QStringLiteral("axisRatioEnable"), false).toBool();
        const double minAxisRatio = m_params.value(QStringLiteral("minAxisRatio"), 0.01).toDouble();
        const double maxAxisRatio = m_params.value(QStringLiteral("maxAxisRatio"), 1.0).toDouble();
        const bool selectBoxAngle = m_params.value(QStringLiteral("selectByBoxAngle"), false).toBool();
        const double minBoxAngle = m_params.value(QStringLiteral("minBoxAngle"), -90.0).toDouble();
        const double maxBoxAngle = m_params.value(QStringLiteral("maxBoxAngle"), 90.0).toDouble();
        const int connectivity = m_params.value(QStringLiteral("connectivity"), 0).toInt();
        const int holeMinArea = m_params.value(QStringLiteral("holeMinArea"), 0).toInt();
        const int findNum = m_params.value(QStringLiteral("findNum"), 100).toInt();
        const int sortFeature = m_params.value(QStringLiteral("sortFeature"), 0).toInt();
        const int sortMode = m_params.value(QStringLiteral("sortMode"), 1).toInt();
        const bool okWhenNumIsZero = m_params.value(QStringLiteral("okWhenNumIsZero"), false).toBool();
        const bool blobNumLimitEnable = m_params.value(QStringLiteral("blobNumLimitEnable"), false).toBool();
        const int blobNumLimitLow = m_params.value(QStringLiteral("blobNumLimitLow"), 1).toInt();
        const int blobNumLimitHigh = m_params.value(QStringLiteral("blobNumLimitHigh"), 99999).toInt();
        const bool blobAreaLimitEnable = m_params.value(QStringLiteral("blobAreaLimitEnable"), false).toBool();
        const double blobAreaLimitLow = m_params.value(QStringLiteral("blobAreaLimitLow"), 1.0).toDouble();
        const double blobAreaLimitHigh = m_params.value(QStringLiteral("blobAreaLimitHigh"), 999999999.0).toDouble();
        const bool blobTotalAreaLimitEnable =
            m_params.value(QStringLiteral("blobTotalAreaLimitEnable"), false).toBool();
        const double blobTotalAreaLimitLow =
            m_params.value(QStringLiteral("blobTotalAreaLimitLow"), 1.0).toDouble();
        const double blobTotalAreaLimitHigh =
            m_params.value(QStringLiteral("blobTotalAreaLimitHigh"), 999999999.0).toDouble();
        // VM 剩余四项（默认值 = 老行为：不超界过滤 / 不算内接矩形 / 不画轮廓 / 照常出图）
        const bool boundaryPixelEnable =
            m_params.value(QStringLiteral("boundaryPixelEnable"), false).toBool();
        const int maxBoundaryPixels =
            m_params.value(QStringLiteral("maxBoundaryPixels"), 0).toInt();
        const int maxInscribedRectCount =
            m_params.value(QStringLiteral("maxInscribedRectCount"), 0).toInt();
        const bool contourOutputEnable =
            m_params.value(QStringLiteral("contourOutputEnable"), false).toBool();
        const bool blobImageOutput =
            m_params.value(QStringLiteral("blobImageOutput"), true).toBool();

        // ROI（矩形）+ 掩膜：只分析允许区域内的像素。
        cv::Rect roiRect;
        const RoiShape roi = geometryRoi();
        if (roi.type == RoiType::Rect) {
            const QRectF box = QRectF(roi.p1, roi.p2).normalized();
            if (box.width() > 1.0 && box.height() > 1.0)
                roiRect = cv::Rect(int(box.x()), int(box.y()), int(box.width()), int(box.height()));
        }
        const cv::Mat allowed = OpencvUtil::allowedMask(mat.size(), roiRect, editMask());

        // 双阈值上下限不做交换：VM 的「低>高=区间外为前景」是有效语义。
        // 软阈值权重 → 二值图（Blob 连通域仍需要二值输入；权重按 0.5 判定）。
        auto softBinary = [&](int lo, int hi) -> cv::Mat {
            cv::Mat bin = cv::Mat::zeros(mat.size(), CV_8U);
            if (hi <= lo) {
                // 退化区间：按低阈值做硬阈值
                for (int y = 0; y < mat.rows; ++y) {
                    const uchar *srcRow = mat.ptr<uchar>(y);
                    uchar *outRow = bin.ptr<uchar>(y);
                    for (int x = 0; x < mat.cols; ++x) {
                        const bool fg = (polarity == 0) ? (srcRow[x] >= lo) : (srcRow[x] < lo);
                        outRow[x] = fg ? 255 : 0;
                    }
                }
                return bin;
            }
            for (int y = 0; y < mat.rows; ++y) {
                const uchar *srcRow = mat.ptr<uchar>(y);
                uchar *outRow = bin.ptr<uchar>(y);
                for (int x = 0; x < mat.cols; ++x) {
                    double w = double(srcRow[x] - lo) / double(hi - lo);
                    w = std::min(1.0, std::max(0.0, w));
                    if (polarity == 1)
                        w = 1.0 - w;              // 暗于背景：低灰度是前景
                    if (softness > 0)
                        w = std::round(w * softness) / softness;
                    outRow[x] = (w >= 0.5) ? 255 : 0;
                }
            }
            return bin;
        };

        cv::Mat bin;
        switch (thresholdType) {
        case 0:   // 不进行二值化：输入本身按非零为前景
            cv::threshold(mat, bin, 0, 255, cv::THRESH_BINARY);
            break;
        case 1:   // 单阈值
            if (polarity == 0)
                cv::compare(mat, cv::Scalar(lowThreshold), bin, cv::CMP_GE);
            else
                cv::compare(mat, cv::Scalar(lowThreshold), bin, cv::CMP_LT);
            break;
        case 2:   // 双阈值：低<高 → 区间内；低>高 → 区间外
            if (lowThreshold <= highThreshold) {
                cv::inRange(mat, cv::Scalar(lowThreshold), cv::Scalar(highThreshold), bin);
            } else {
                cv::inRange(mat, cv::Scalar(highThreshold), cv::Scalar(lowThreshold), bin);
                cv::bitwise_not(bin, bin);
            }
            break;
        case 3: { // 自动阈值（OTSU）+ 极性
            const double otsu = cv::threshold(mat, bin, 0, 255,
                                              cv::THRESH_BINARY | cv::THRESH_OTSU);
            m_params[QStringLiteral("otsuThreshold")] = otsu;
            if (polarity == 1)
                cv::bitwise_not(bin, bin);
            break;
        }
        case 4:   // 软阈值（固定）
            bin = softBinary(softLow, softHigh);
            break;
        case 5: { // 软阈值（相对）：先按尾部比例裁掉极端灰度，再按比例取低/高阈值
            int hist[256] = {0};
            for (int y = 0; y < mat.rows; ++y) {
                const uchar *srcRow = mat.ptr<uchar>(y);
                const uchar *allowRow = allowed.ptr<uchar>(y);
                for (int x = 0; x < mat.cols; ++x)
                    if (allowRow[x])
                        ++hist[srcRow[x]];
            }
            int total = 0;
            for (int v = 0; v < 256; ++v)
                total += hist[v];
            if (total <= 0) {                 // ROI 内无有效像素：整图背景，不退化成全前景
                bin = cv::Mat::zeros(mat.size(), CV_8UC1);
                break;
            }
            const int leftTail = total * softLeftRatio / 100;
            const int rightTail = total * softRightRatio / 100;
            int cum = 0;
            int a = 0, b = 255;
            // 严格「>」：tail=0（小 ROI 或尾部比例 0）时不能让 cum>=0 在 v=0 立即成立，
            // 否则 a 恒为 0，软阈值区间被拉到整段 0..255，现场看到整图变前景。
            for (int v = 0; v < 256; ++v) {
                cum += hist[v];
                if (cum > leftTail) {
                    a = v;
                    break;
                }
            }
            // 与左侧严格对称：从高端累计，累计量刚超过 rightTail 时取当前灰度为上界。
            // 旧写法 keep = total - rightTail 且判 cum >= keep，在 rightTail=0 时会把 b
            // 压到最低有像素的灰度，使 a==b、区间退化成硬阈值 → 整图变前景。
            cum = 0;
            for (int v = 255; v >= 0; --v) {
                cum += hist[v];
                if (cum > rightTail) {
                    b = v;
                    break;
                }
            }
            if (b < a) b = a;                  // 单灰度图：区间退化为一点，交 softBinary 兜底
            const int span = b - a;
            const int rlo = a + span * softLowRatio / 100;
            const int rhi = a + span * softHighRatio / 100;
            bin = softBinary(std::min(rlo, rhi), std::max(rlo, rhi));
            break;
        }
        default:
            cv::threshold(mat, bin, lowThreshold - 1, 255, cv::THRESH_BINARY);
            break;
        }
        cv::bitwise_and(bin, allowed, bin);
        fillSmallHoles(bin, holeMinArea);

        cv::Mat labels, stats, centroids;
        const int n = cv::connectedComponentsWithStats(
            bin, labels, stats, centroids, connectivity == 0 ? 8 : 4);

        const bool sortNeedsShape = (sortFeature == 1 || sortFeature == 2 || sortFeature == 3
                                     || sortFeature >= 6);
        const bool needShape = selectPerimeter || selectShortAxis || selectLongAxis
            || selectCircularity || selectRectangularity || selectCenterBias
            || selectAxisRatio || selectBoxAngle || sortNeedsShape;
        std::vector<Candidate> candidates;
        candidates.reserve(static_cast<size_t>(std::max(0, n - 1)));
        for (int i = 1; i < n; ++i) {  // 0 = 背景
            const int *st = stats.ptr<int>(i);
            const int area = st[cv::CC_STAT_AREA];
            const int w = st[cv::CC_STAT_WIDTH];
            const int h = st[cv::CC_STAT_HEIGHT];
            const double cx = centroids.at<double>(i, 0);
            const double cy = centroids.at<double>(i, 1);

            if (selectArea && (area < minArea || area > maxArea))
                continue;
            if (selectRectWidth && (w < minRectWidth || w > maxRectWidth))
                continue;
            if (selectRectHeight && (h < minRectHeight || h > maxRectHeight))
                continue;

            // VM「像素超界使能」：Blob 中与**图像边缘 / ROI 边缘 / 掩膜边缘**重合的像素数
            // 超过最大超界像素时删除该 Blob。只扫该连通域自己的包围盒，总代价 O(前景面积)。
            if (boundaryPixelEnable) {
                const int bx = st[cv::CC_STAT_LEFT];
                const int by = st[cv::CC_STAT_TOP];
                int touching = 0;
                for (int yy = by; yy < by + h && touching <= maxBoundaryPixels; ++yy) {
                    const int *lp = labels.ptr<int>(yy);
                    const uchar *ap = allowed.ptr<uchar>(yy);
                    const uchar *apUp = yy > 0 ? allowed.ptr<uchar>(yy - 1) : ap;
                    const uchar *apDn = yy + 1 < mat.rows ? allowed.ptr<uchar>(yy + 1) : ap;
                    for (int xx = bx; xx < bx + w; ++xx) {
                        if (lp[xx] != i)
                            continue;
                        bool edge = (xx == 0 || yy == 0 || xx == mat.cols - 1
                                     || yy == mat.rows - 1);
                        if (!edge && (xx == roiRect.x || yy == roiRect.y
                                      || xx == roiRect.x + roiRect.width - 1
                                      || yy == roiRect.y + roiRect.height - 1))
                            edge = true;
                        if (!edge)
                            edge = (xx > 0 && ap[xx - 1] == 0)
                                || (xx + 1 < mat.cols && ap[xx + 1] == 0)
                                || (yy > 0 && apUp[xx] == 0)
                                || (yy + 1 < mat.rows && apDn[xx] == 0);
                        if (edge)
                            ++touching;
                    }
                }
                if (touching > maxBoundaryPixels)
                    continue;
            }

            BlobShapeMetrics sm;
            if (needShape) {
                sm = measureShape(labels, i, st, cx, cy);
                if (selectPerimeter && (sm.perimeter < minPerimeter || sm.perimeter > maxPerimeter))
                    continue;
                if (selectShortAxis && (sm.shortAxis < minShortAxis || sm.shortAxis > maxShortAxis))
                    continue;
                if (selectLongAxis && (sm.longAxis < minLongAxis || sm.longAxis > maxLongAxis))
                    continue;
                if (selectCircularity && (sm.circularity < minCircularity || sm.circularity > maxCircularity))
                    continue;
                if (selectRectangularity
                    && (sm.rectangularity < minRectangularity || sm.rectangularity > maxRectangularity))
                    continue;
                if (selectCenterBias && (sm.centerBias < minCenterBias || sm.centerBias > maxCenterBias))
                    continue;
                const double axisRatio = (sm.longAxis > 0.0) ? sm.shortAxis / sm.longAxis : 0.0;
                if (selectAxisRatio && (axisRatio < minAxisRatio || axisRatio > maxAxisRatio))
                    continue;
                if (selectBoxAngle && (sm.boxAngle < minBoxAngle || sm.boxAngle > maxBoxAngle))
                    continue;
            }

            Candidate c;
            c.label = i;
            c.area = area;
            c.x = st[cv::CC_STAT_LEFT];
            c.y = st[cv::CC_STAT_TOP];
            c.w = w;
            c.h = h;
            c.cx = cx;
            c.cy = cy;
            if (needShape) {
                c.perimeter = sm.perimeter;
                c.circularity = sm.circularity;
                c.rectangularity = sm.rectangularity;
                c.boxAngle = sm.boxAngle;
                c.boxWidth = sm.boxWidth;
                c.boxHeight = sm.boxHeight;
                c.axisAngle = sm.axisAngle;   // VM 二阶中心矩主轴角度（不再复用 boxAngle）
                c.axisRatio = (sm.longAxis > 0.0) ? sm.shortAxis / sm.longAxis : 0.0;
            }
            candidates.push_back(c);
        }

        // 排序：13 种特征 + 升序/降序/不排序；stable_sort + 标签兜底保证同值顺序可复现。
        if (sortMode != 2) {
            std::stable_sort(candidates.begin(), candidates.end(),
                             [sortFeature, sortMode](const Candidate &a, const Candidate &b) {
                                 double fa = 0.0, fb = 0.0;
                                 switch (sortFeature) {
                                 case 1: fa = a.perimeter; fb = b.perimeter; break;
                                 case 2: fa = a.circularity; fb = b.circularity; break;
                                 case 3: fa = a.rectangularity; fb = b.rectangularity; break;
                                 case 4: fa = a.cx; fb = b.cx; break;
                                 case 5: fa = a.cy; fb = b.cy; break;
                                 case 6: fa = a.boxAngle; fb = b.boxAngle; break;
                                 case 7: fa = a.boxWidth; fb = b.boxWidth; break;
                                 case 8: fa = a.boxHeight; fb = b.boxHeight; break;
                                 case 9: fa = a.x; fb = b.x; break;
                                 case 10: fa = a.y; fb = b.y; break;
                                 case 11: fa = a.axisAngle; fb = b.axisAngle; break;
                                 case 12: fa = a.axisRatio; fb = b.axisRatio; break;
                                 default: fa = a.area; fb = b.area; break;
                                 }
                                 if (fa == fb)
                                     return false;
                                 return sortMode == 0 ? fa < fb : fa > fb;
                             });
        }
        if (findNum > 0 && static_cast<int>(candidates.size()) > findNum)
            candidates.resize(static_cast<size_t>(findNum));

        // VM「最大内接矩形输出数量」：只算**最终保留集**的前 N 个，避免 findNum 截断后
        // 输出与结果表自相矛盾。格式 cx,cy,w,h,angle;...（轴对齐内接矩形角度为 0）。
        QString inscribedText;
        if (maxInscribedRectCount > 0) {
            const int count = qMin(maxInscribedRectCount, static_cast<int>(candidates.size()));
            QStringList parts;
            for (int k = 0; k < count; ++k) {
                const Candidate &c = candidates[static_cast<size_t>(k)];
                const cv::Rect r =
                    largestInscribedRect(labels, c.label, cv::Rect(c.x, c.y, c.w, c.h));
                if (r.width <= 0 || r.height <= 0)
                    continue;
                parts << QStringLiteral("%1,%2,%3,%4,0")
                             .arg(r.x + r.width * 0.5, 0, 'f', 1)
                             .arg(r.y + r.height * 0.5, 0, 'f', 1)
                             .arg(r.width)
                             .arg(r.height);
            }
            inscribedText = parts.join(QLatin1Char(';'));
        }
        m_params[QStringLiteral("inscribedRects")] = inscribedText;

        // 结果字段（全部按**最终保留集**统计，findNum 截断后不会自相矛盾）
        double totalArea = 0.0;
        double minBlobArea = 0.0;
        double maxBlobArea = 0.0;
        double maxRow = 0.0, maxCol = 0.0, maxW = 0.0, maxH = 0.0;
        for (const Candidate &c : candidates) {
            totalArea += c.area;
            if (maxBlobArea <= 0.0 || c.area > maxBlobArea) {
                maxBlobArea = c.area;
                maxRow = c.cy;
                maxCol = c.cx;
                maxW = c.w;
                maxH = c.h;
            }
            if (minBlobArea <= 0.0 || c.area < minBlobArea)
                minBlobArea = c.area;
        }
        const int validCount = static_cast<int>(candidates.size());
        const double meanArea = validCount > 0 ? totalArea / validCount : 0.0;

        m_params[QStringLiteral("blobCount")] = validCount;
        m_params[QStringLiteral("totalBlobArea")] = totalArea;
        m_params[QStringLiteral("minBlobArea")] = minBlobArea;
        m_params[QStringLiteral("maxBlobArea")] = maxBlobArea;
        m_params[QStringLiteral("meanBlobArea")] = meanArea;
        m_params[QStringLiteral("maxBlobCentroidRow")] = maxRow;
        m_params[QStringLiteral("maxBlobCentroidCol")] = maxCol;
        m_params[QStringLiteral("maxBlobWidth")] = maxW;
        m_params[QStringLiteral("maxBlobHeight")] = maxH;

        // 结果判断（VM：无目标 OK / Blob 数量 / 单个 Blob 面积 / 总面积）
        bool judgeOk = true;
        QString judgeReason;
        if (validCount == 0) {
            judgeOk = okWhenNumIsZero;   // VM：无目标时是否 OK 由该开关决定
            if (!judgeOk)
                judgeReason = QStringLiteral("未检测到 Blob（结果判断：无目标不允许）");
        } else {
            if (blobNumLimitEnable
                && (validCount < blobNumLimitLow || validCount > blobNumLimitHigh)) {
                judgeOk = false;
                judgeReason = QStringLiteral("Blob 数量 %1 超出 [%2, %3]")
                                  .arg(validCount).arg(blobNumLimitLow).arg(blobNumLimitHigh);
            }
            if (judgeOk && blobAreaLimitEnable) {
                for (const Candidate &c : candidates) {
                    if (c.area < blobAreaLimitLow || c.area > blobAreaLimitHigh) {
                        judgeOk = false;
                        judgeReason = QStringLiteral("Blob 面积 %1 超出 [%2, %3]")
                                          .arg(c.area).arg(blobAreaLimitLow).arg(blobAreaLimitHigh);
                        break;
                    }
                }
            }
            if (judgeOk && blobTotalAreaLimitEnable
                && (totalArea < blobTotalAreaLimitLow || totalArea > blobTotalAreaLimitHigh)) {
                judgeOk = false;
                judgeReason = QStringLiteral("Blob 总面积 %1 超出 [%2, %3]")
                                  .arg(totalArea).arg(blobTotalAreaLimitLow).arg(blobTotalAreaLimitHigh);
            }
        }
        m_params[QStringLiteral("judgeReason")] = judgeReason;
        // 结果表 / 失败轮只采集 lastError 或 *Note 结尾的键（NodeResultFields.h）。
        // 判红原因必须同时写进 lastError，否则现场只见红灯、没有原因说明；
        // 判 OK 时写空串可清掉上一轮残留。
        m_params[QStringLiteral("lastError")] = judgeReason;
        m_params["moduleStatus"] = judgeOk;

        // 标记图：**单遍扫描 + 标签查色**（2026-10-26 复核 P3：原实现对每个 blob 都扫一遍
        // 全图 = O(blob 数 × 像素数)，5MP 图 × 100 个 blob 就是 5 亿次比较，现场会明显卡顿）。
        std::vector<uchar> colorOf(static_cast<size_t>(n), 0);
        for (size_t rank = 0; rank < candidates.size(); ++rank)
            colorOf[static_cast<size_t>(candidates[rank].label)] =
                static_cast<uchar>(20 + (rank % 12) * 20);
        cv::Mat vis = cv::Mat::zeros(bin.size(), CV_8UC1);
        for (int y = 0; y < labels.rows; ++y) {
            const int *lp = labels.ptr<int>(y);
            uchar *vp = vis.ptr<uchar>(y);
            for (int x = 0; x < labels.cols; ++x) {
                const int lb = lp[x];
                if (lb > 0 && lb < n)
                    vp[x] = colorOf[static_cast<size_t>(lb)];
            }
        }

        // VM「轮廓输出使能」：开启后在标记图上画出**保留** Blob 的轮廓
        if (contourOutputEnable && !candidates.empty()) {
            cv::Mat binCopy = bin.clone();       // findContours 会改写输入
            std::vector<std::vector<cv::Point>> cnts;
            cv::findContours(binCopy, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            std::vector<std::vector<cv::Point>> keep;
            for (const auto &c : cnts) {
                if (c.empty())
                    continue;
                const int lb = labels.at<int>(c.front().y, c.front().x);
                if (lb > 0 && lb < n && colorOf[static_cast<size_t>(lb)] != 0)
                    keep.push_back(c);
            }
            cv::drawContours(vis, keep, -1, cv::Scalar(255), 1);
        }

        m_outputImage = m_inputImage;
        if (blobImageOutput) {
            auto visObj = QSharedPointer<DataObject>::create();
            visObj->setHImage(OpencvUtil::matToHimage(vis));
            setOutputData(1, visObj);
        } else {
            // VM「Blob 图像输出」关闭：不产出分析后的标记图
            setOutputData(1, QSharedPointer<DataObject>());
        }
        auto numObj = QSharedPointer<DataObject>::create();
        numObj->setValue(double(validCount));
        setOutputData(2, numObj);

        // 🥇 面积端口：输出最大 Blob 的面积（像素）
        auto areaObj = QSharedPointer<DataObject>::create();
        areaObj->setValue(maxBlobArea);
        setOutputData(3, areaObj);

        // Blob 列表：每个保留 Blob 一条 "rect" 测量结果（叠加层按 extraValues=[x,y,w,h] 画框）
        if (!candidates.empty()) {
            auto listObj = QSharedPointer<DataObject>::create();
            bool first = true;
            for (const Candidate &c : candidates) {
                MeasureResult r;
                r.type = QStringLiteral("rect");
                r.valueName = QStringLiteral("面积");
                r.value = static_cast<double>(c.area);
                r.valid = true;
                r.point1 = QPointF(c.cx, c.cy);
                r.extraValues = QVector<double>{static_cast<double>(c.x),
                                                static_cast<double>(c.y),
                                                static_cast<double>(c.w),
                                                static_cast<double>(c.h)};
                if (first) {
                    listObj->setMeasureResult(r);
                    first = false;
                } else {
                    listObj->attachMeasureResult(r);
                }
            }
            setOutputData(4, listObj);
        } else {
            setOutputData(4, QSharedPointer<DataObject>());
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvBlobNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());   // 原因到面板（2026-10-26）
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvBlobNode::createParamPanel()
{
    QWidget *panel = createAutoParamPanel();
    if (auto *typeCombo = panel->findChild<QComboBox *>(QStringLiteral("thresholdType"))) {
        connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, panel](int) { applyParamVisibility(panel); });
    }
    applyParamVisibility(panel);
    return panel;
}

void OpencvBlobNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
    applyParamVisibility(panel);
}

void OpencvBlobNode::applyParamVisibility(QWidget *panel)
{
    if (!panel)
        return;
    QComboBox *typeCombo = panel->findChild<QComboBox *>(QStringLiteral("thresholdType"));
    const int thresholdType = typeCombo ? typeCombo->currentIndex()
                                        : m_params.value(QStringLiteral("thresholdType"), 1).toInt();

    QSet<QString> visible;
    switch (thresholdType) {
    case 1:
        visible = {QStringLiteral("lowThreshold")};
        break;
    case 2:
        visible = {QStringLiteral("lowThreshold"), QStringLiteral("highThreshold")};
        break;
    case 4:
        visible = {QStringLiteral("softLowThreshold"), QStringLiteral("softHighThreshold"),
                   QStringLiteral("softness")};
        break;
    case 5:
        visible = {QStringLiteral("softLeftRatio"), QStringLiteral("softRightRatio"),
                   QStringLiteral("softLowRatio"), QStringLiteral("softHighRatio"),
                   QStringLiteral("softness")};
        break;
    default:
        break;   // 不进行二值化 / 自动阈值没有阈值参数
    }

    static const QSet<QString> dependent {
        QStringLiteral("lowThreshold"), QStringLiteral("highThreshold"),
        QStringLiteral("softLowThreshold"), QStringLiteral("softHighThreshold"),
        QStringLiteral("softness"), QStringLiteral("softLeftRatio"),
        QStringLiteral("softRightRatio"), QStringLiteral("softLowRatio"),
        QStringLiteral("softHighRatio")
    };

    QFormLayout *form = nullptr;
    for (QFormLayout *f : panel->findChildren<QFormLayout *>()) {
        if (typeCombo && f->indexOf(typeCombo) >= 0) {
            form = f;
            break;
        }
    }
    if (!form)
        return;
    for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem *fieldItem = form->itemAt(row, QFormLayout::FieldRole);
        QWidget *field = fieldItem ? fieldItem->widget() : nullptr;
        const QString name = field ? field->objectName() : QString();
        if (!dependent.contains(name))
            continue;
        form->setRowVisible(row, visible.contains(name));
    }
}

RoiShape OpencvBlobNode::geometryRoi() const
{
    const int row = m_params.value(QStringLiteral("roiRectRow"), 0).toInt();
    const int col = m_params.value(QStringLiteral("roiRectCol"), 0).toInt();
    const int width = m_params.value(QStringLiteral("roiRectWidth"), 0).toInt();
    const int height = m_params.value(QStringLiteral("roiRectHeight"), 0).toInt();
    RoiShape shape;
    if (width <= 1 || height <= 1)
        return shape;
    shape.type = RoiType::Rect;
    shape.p1 = QPointF(col, row);
    shape.p2 = QPointF(col + width, row + height);
    return shape;
}

void OpencvBlobNode::applyGeometryRoi(const RoiShape &shape)
{
    if (shape.type == RoiType::Rect) {
        const QRectF box = QRectF(shape.p1, shape.p2).normalized();
        setParam(QStringLiteral("roiRectRow"), int(box.y()));
        setParam(QStringLiteral("roiRectCol"), int(box.x()));
        setParam(QStringLiteral("roiRectWidth"), qMax(1, int(box.width())));
        setParam(QStringLiteral("roiRectHeight"), qMax(1, int(box.height())));
    } else if (shape.type == RoiType::None) {
        setParam(QStringLiteral("roiRectRow"), 0);
        setParam(QStringLiteral("roiRectCol"), 0);
        setParam(QStringLiteral("roiRectWidth"), 0);
        setParam(QStringLiteral("roiRectHeight"), 0);
    }
}
