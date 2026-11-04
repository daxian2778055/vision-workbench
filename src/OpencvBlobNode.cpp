#include "OpencvBlobNode.h"
#include "NodeResultFields.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
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

/// Blob 圆度 = 4πA/P²（取该标签的最大外轮廓；周长按 8 邻域链码）。
/// 只在该过滤条件真的启用时才调用（见 run 里的 needCircularity），避免默认路径多一遍轮廓扫描。
double blobCircularity(const cv::Mat &labels, int label, const int *stat)
{
    const int x = stat[cv::CC_STAT_LEFT];
    const int y = stat[cv::CC_STAT_TOP];
    const int w = stat[cv::CC_STAT_WIDTH];
    const int h = stat[cv::CC_STAT_HEIGHT];
    if (w <= 0 || h <= 0)
        return 0.0;

    cv::Mat mask;
    cv::compare(labels(cv::Rect(x, y, w, h)), cv::Scalar(label), mask, cv::CMP_EQ);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

    double bestArea = 0.0;
    double bestPerim = 0.0;
    for (const auto &c : contours) {
        const double a = cv::contourArea(c);
        const double p = cv::arcLength(c, true);
        if (a > bestArea) {
            bestArea = a;
            bestPerim = p;
        }
    }
    if (bestPerim <= 0.0)
        return 0.0;
    const double circ = 4.0 * CV_PI * bestArea / (bestPerim * bestPerim);
    return std::min(1.0, std::max(0.0, circ));
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
};

}   // namespace

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
        // ---- 阈值（对标 VM Blob：阈值类型 / 极性 / 低高阈值）----
        grouped(makeEnumParam(QStringLiteral("thresholdType"), 0,
                              {QStringLiteral("固定阈值"), QStringLiteral("Otsu 自动阈值")},
                              QStringLiteral("阈值类型")),
                QStringLiteral("阈值")),
        grouped(makeEnumParam(QStringLiteral("polarity"), 0,
                              {QStringLiteral("亮目标（灰度区间内为前景）"),
                               QStringLiteral("暗目标（灰度区间外为前景）")},
                              QStringLiteral("目标极性")),
                QStringLiteral("阈值")),
        grouped(makeIntParam(QStringLiteral("lowThreshold"), 1, 0, 255,
                             QStringLiteral("低阈值")),
                QStringLiteral("阈值")),
        grouped(makeIntParam(QStringLiteral("highThreshold"), 255, 0, 255,
                             QStringLiteral("高阈值")),
                QStringLiteral("阈值")),
        // ---- 连通域与形状筛选 ----
        grouped(makeIntParam(QStringLiteral("minArea"), 0, 0, 1000000,
                             QStringLiteral("最小面积过滤"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("maxArea"), 100000000, 0, 100000000,
                             QStringLiteral("最大面积过滤"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("minWidth"), 0, 0, 100000,
                             QStringLiteral("最小外接宽"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("maxWidth"), 100000, 0, 100000,
                             QStringLiteral("最大外接宽"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("minHeight"), 0, 0, 100000,
                             QStringLiteral("最小外接高"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("maxHeight"), 100000, 0, 100000,
                             QStringLiteral("最大外接高"), QStringLiteral("px")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("minCircularity"), 0.0, 0.0, 1.0,
                                QStringLiteral("最小圆度")),
                QStringLiteral("筛选")),
        grouped(makeDoubleParam(QStringLiteral("maxCircularity"), 1.0, 0.0, 1.0,
                                QStringLiteral("最大圆度")),
                QStringLiteral("筛选")),
        grouped(makeEnumParam(QStringLiteral("connectivity"), 0,
                              {QStringLiteral("8 邻域"), QStringLiteral("4 邻域")},
                              QStringLiteral("连通性")),
                QStringLiteral("筛选")),
        grouped(makeIntParam(QStringLiteral("holeMinArea"), 0, 0, 1000000,
                             QStringLiteral("孔洞填充面积（小于该值的孔洞填平，0=不填）"),
                             QStringLiteral("px")),
                QStringLiteral("筛选")),
        // ---- 结果数量与顺序 ----
        grouped(makeIntParam(QStringLiteral("findNum"), 0, 0, 10000,
                             QStringLiteral("最多输出个数（0=全部）")),
                QStringLiteral("输出")),
        grouped(makeEnumParam(QStringLiteral("sortFeature"), 0,
                              {QStringLiteral("面积"), QStringLiteral("行坐标"),
                               QStringLiteral("列坐标")},
                              QStringLiteral("排序特征")),
                QStringLiteral("输出")),
        grouped(makeEnumParam(QStringLiteral("sortMode"), 0,
                              {QStringLiteral("降序"), QStringLiteral("升序")},
                              QStringLiteral("排序方式")),
                QStringLiteral("输出")),
        grouped(makeBoolParam(QStringLiteral("showBlobBox"), true,
                              QStringLiteral("显示 Blob 检测框")),
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

        const int thresholdType = m_params.value(QStringLiteral("thresholdType"), 0).toInt();
        const int polarity = m_params.value(QStringLiteral("polarity"), 0).toInt();
        int lowThreshold = m_params.value(QStringLiteral("lowThreshold"), 1).toInt();
        int highThreshold = m_params.value(QStringLiteral("highThreshold"), 255).toInt();
        const int minArea = m_params.value(QStringLiteral("minArea"), 0).toInt();
        const int maxArea = m_params.value(QStringLiteral("maxArea"), 100000000).toInt();
        const int minWidth = m_params.value(QStringLiteral("minWidth"), 0).toInt();
        const int maxWidth = m_params.value(QStringLiteral("maxWidth"), 100000).toInt();
        const int minHeight = m_params.value(QStringLiteral("minHeight"), 0).toInt();
        const int maxHeight = m_params.value(QStringLiteral("maxHeight"), 100000).toInt();
        const double minCircularity = m_params.value(QStringLiteral("minCircularity"), 0.0).toDouble();
        const double maxCircularity = m_params.value(QStringLiteral("maxCircularity"), 1.0).toDouble();
        const int connectivity = m_params.value(QStringLiteral("connectivity"), 0).toInt();
        const int holeMinArea = m_params.value(QStringLiteral("holeMinArea"), 0).toInt();
        const int findNum = m_params.value(QStringLiteral("findNum"), 0).toInt();
        const int sortFeature = m_params.value(QStringLiteral("sortFeature"), 0).toInt();
        const int sortMode = m_params.value(QStringLiteral("sortMode"), 0).toInt();

        // 上下限写反时按大小自动纠正（现场手填常见，直接报错只会让人再看一遍说明书）
        if (lowThreshold > highThreshold)
            std::swap(lowThreshold, highThreshold);

        cv::Mat bin;
        if (thresholdType == 1) {
            // Otsu：在整图上求全局阈值，再按极性决定前景是亮侧还是暗侧
            const double otsu = cv::threshold(mat, bin, 0, 255,
                                              cv::THRESH_BINARY | cv::THRESH_OTSU);
            m_params[QStringLiteral("otsuThreshold")] = otsu;
            if (polarity == 1)
                cv::bitwise_not(bin, bin);
        } else if (polarity == 0) {
            cv::inRange(mat, cv::Scalar(lowThreshold), cv::Scalar(highThreshold), bin);
        } else {
            cv::Mat inner;
            cv::inRange(mat, cv::Scalar(lowThreshold), cv::Scalar(highThreshold), inner);
            cv::bitwise_not(inner, bin);
        }
        OpencvUtil::applyGrayMask(bin, editMask());
        fillSmallHoles(bin, holeMinArea);

        cv::Mat labels, stats, centroids;
        const int n = cv::connectedComponentsWithStats(
            bin, labels, stats, centroids, connectivity == 0 ? 8 : 4);

        const bool needCircularity = minCircularity > 0.0 || maxCircularity < 1.0;
        std::vector<Candidate> candidates;
        candidates.reserve(static_cast<size_t>(std::max(0, n - 1)));
        for (int i = 1; i < n; ++i) {  // 0 = 背景
            const int *st = stats.ptr<int>(i);
            const int area = st[cv::CC_STAT_AREA];
            const int w = st[cv::CC_STAT_WIDTH];
            const int h = st[cv::CC_STAT_HEIGHT];
            if (area < minArea || area > maxArea)
                continue;
            if (w < minWidth || w > maxWidth || h < minHeight || h > maxHeight)
                continue;
            if (needCircularity) {
                const double circ = blobCircularity(labels, i, st);
                if (circ < minCircularity || circ > maxCircularity)
                    continue;
            }
            Candidate c;
            c.label = i;
            c.area = area;
            c.x = st[cv::CC_STAT_LEFT];
            c.y = st[cv::CC_STAT_TOP];
            c.w = w;
            c.h = h;
            c.cx = centroids.at<double>(i, 0);
            c.cy = centroids.at<double>(i, 1);
            candidates.push_back(c);
        }

        // 排序：面积 / 行 / 列，降序或升序；用 stable_sort + 标签兜底，保证同值顺序可复现
        std::stable_sort(candidates.begin(), candidates.end(),
                         [sortFeature, sortMode](const Candidate &a, const Candidate &b) {
                             double fa = 0.0, fb = 0.0;
                             if (sortFeature == 1) {
                                 fa = a.cy;
                                 fb = b.cy;
                             } else if (sortFeature == 2) {
                                 fa = a.cx;
                                 fb = b.cx;
                             } else {
                                 fa = a.area;
                                 fb = b.area;
                             }
                             if (fa == fb)
                                 return false;
                             return sortMode == 0 ? fa > fb : fa < fb;
                         });
        if (findNum > 0 && static_cast<int>(candidates.size()) > findNum)
            candidates.resize(static_cast<size_t>(findNum));

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
        m_params["moduleStatus"] = true;

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

        m_outputImage = m_inputImage;
        auto visObj = QSharedPointer<DataObject>::create();
        visObj->setHImage(OpencvUtil::matToHimage(vis));
        setOutputData(1, visObj);
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
    return createAutoParamPanel();
}

void OpencvBlobNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
