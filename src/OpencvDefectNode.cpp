#include "OpencvDefectNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QWidget>
#include <QFileInfo>
#include <QDir>
#include <algorithm>
#include <cmath>

namespace {

/// 旋转估计（P1-1 补完）：**多角度试探 + 平移相关响应择优**，返回"当前图相对黄金模板"的旋转角（度），
/// 正负沿用 cv::getRotationMatrix2D 的约定（正值 = 逆时针），符号由用例钉住。
///
/// 为什么不用"对数极坐标 + 相位相关"：cv::warpPolar/warpLogPolar 的角度轴约定极易搞错，
/// 而且**错了不报错、只是静默返回≈0 的估计值**（实测把 2° 旋转估成 0.07°，表面看"没报错"）。
/// 改用本方案后只用已经钉住符号约定的 phaseCorrelate，且天然带置信度（响应值可喂给同一套闸门）。
///
/// 性能：在长边 ≤512 的缩略图上按 0.5° 步长搜索（旋转估计对分辨率不敏感，对耗时极敏感——
/// 2448×2048 原图做 FFT 相关是几十~上百毫秒量级，缩略后整轮搜索只要几十毫秒）。
/// 代价：不做尺度归一化（本节点不处理缩放）。
double estimateRotationSearch(const cv::Mat &goldF, const cv::Mat &curF, double rangeDeg)
{
    const int longSide = std::max(goldF.cols, goldF.rows);
    const double s = (longSide > 512) ? (512.0 / longSide) : 1.0;
    cv::Mat g, c;
    cv::resize(goldF, g, cv::Size(), s, s, cv::INTER_AREA);
    cv::resize(curF, c, cv::Size(), s, s, cv::INTER_AREA);
    if (g.empty() || c.empty() || g.size() != c.size())
        return 0.0;
    const cv::Point2d center(g.cols / 2.0, g.rows / 2.0);

    double bestAngle = 0.0, bestResp = -1.0;
    const double step = 0.5;
    for (double a = -rangeDeg; a <= rangeDeg + 1e-9; a += step) {
        const cv::Mat R = cv::getRotationMatrix2D(center, -a, 1.0);   // 试：按 -a 把当前图转回
        cv::Mat t;
        cv::warpAffine(c, t, R, c.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        double resp = 0.0;
        cv::phaseCorrelate(g, t, cv::noArray(), &resp);
        if (resp > bestResp) {
            bestResp = resp;
            bestAngle = a;
        }
    }
    return bestAngle;
}

/// 边缘抑制（P1-1 补完）：**黄金模板**的强边缘 ±guard 像素内不判缺陷。
/// 为什么必要：即使做了对齐，亚像素残差也会让硬边缘两侧差出细线，而"一条细线"的连通域
/// 面积不大，minArea 往往拦不住；现场表现就是"工件轮廓一圈假缺陷"。
/// 用黄金模板取边缘（而不是当前图）——当前图上的真缺陷不应制造免检区。
void applyEdgeGuard(const cv::Mat &goldenGray, cv::Mat &bin, int guard, int sobelThresh)
{
    if (guard <= 0 || bin.empty() || goldenGray.size() != bin.size())
        return;
    // 注意：cv::magnitude 只吃 CV_32F/CV_64F——Sobel 若输出 CV_16S 会直接断言失败
    //（而异常会被节点 run() 的 catch 吞成"moduleStatus=false"，表现成"边缘抑制没效果"，很难查）
    cv::Mat gx, gy, mag;
    cv::Sobel(goldenGray, gx, CV_32F, 1, 0, 3);
    cv::Sobel(goldenGray, gy, CV_32F, 0, 1, 3);
    cv::magnitude(gx, gy, mag);
    cv::Mat edge;
    cv::threshold(mag, edge, std::max(1, sobelThresh), 255, cv::THRESH_BINARY);
    edge.convertTo(edge, CV_8U);
    const int k = 2 * guard + 1;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
    cv::dilate(edge, edge, kernel);
    bin.setTo(0, edge);
}

/// 8 位灰度图转 32F（phaseCorrelate 要求浮点）
cv::Mat toFloatGray(const cv::Mat &m)
{
    cv::Mat f;
    m.convertTo(f, CV_32F);
    return f;
}

} // namespace

OpencvDefectNode::OpencvDefectNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("缺陷检测"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvDefectNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("缺陷"), PortDataType::Measure);
    registerParams({
        makeFilePathParam(QStringLiteral("goldenPath"), QString(),
                          QStringLiteral("黄金模板图像（空=从图上 ROI 教学）")),
        makeBoolParam(QStringLiteral("trainGolden"), true,
                      QStringLiteral("从当前图 ROI 更新黄金模板")),
        makeIntParam(QStringLiteral("roiCol"), 0, 0, 100000, QStringLiteral("ROI 列"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiRow"), 0, 0, 100000, QStringLiteral("ROI 行"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiWidth"), 0, 0, 100000, QStringLiteral("ROI 宽（0=整图）"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiHeight"), 0, 0, 100000, QStringLiteral("ROI 高"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("diffThreshold"), 25, 1, 255, QStringLiteral("差影阈值")),
        makeIntParam(QStringLiteral("morphSize"), 3, 1, 31, QStringLiteral("形态学核"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("minArea"), 20.0, 0.0, 1e9, QStringLiteral("最小缺陷面积"), QStringLiteral("px²")),
        makeDoubleParam(QStringLiteral("maxArea"), 1e9, 0.0, 1e12, QStringLiteral("最大缺陷面积"), QStringLiteral("px²")),
        makeDoubleParam(QStringLiteral("ngArea"), 50.0, 0.0, 1e12, QStringLiteral("NG 面积阈值"), QStringLiteral("px²")),
        // ── P1-1 补完：对齐归一化 + 边缘抑制 ──────────────────────────────────────────
        // 默认开对齐：工件在新图里偏 1 像素，差影就会把整圈边缘点亮成"缺陷"，
        // 默认关着等于把最常见的现场问题留给用户自己发现。
        makeEnumParam(QStringLiteral("alignMode"), 1,
                      {QStringLiteral("不对齐"), QStringLiteral("平移对齐"),
                       QStringLiteral("平移+旋转对齐")},
                      QStringLiteral("对齐归一化（比对前把当前图对回黄金模板）")),
        makeIntParam(QStringLiteral("alignMaxShift"), 50, 0, 2000,
                     QStringLiteral("平移对齐最大允许偏移（超出视为对齐失败）"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("alignMinResponse"), 0.05, 0.0, 1.0,
                        QStringLiteral("对齐置信度下限（低于则不做变换，防低纹理乱移）")),
        makeIntParam(QStringLiteral("alignAngleRange"), 5, 0, 45,
                     QStringLiteral("旋转搜索范围 ±（仅“平移+旋转”模式，步长 0.5°）"),
                     QStringLiteral("°")),
        makeIntParam(QStringLiteral("edgeGuard"), 2, 0, 32,
                     QStringLiteral("边缘抑制宽度（黄金模板强边缘两侧不判缺陷，0=关闭）"),
                     QStringLiteral("px")),
        makeIntParam(QStringLiteral("edgeThresh"), 60, 1, 255,
                     QStringLiteral("边缘抑制的 Sobel 幅值阈值")),
    });
    m_params[QStringLiteral("defectCount")] = 0;
    m_params[QStringLiteral("defectArea")] = 0.0;
    m_params[QStringLiteral("trainStatus")] = QString();
    m_params[QStringLiteral("alignApplied")] = false;
    m_params[QStringLiteral("alignDx")] = 0.0;
    m_params[QStringLiteral("alignDy")] = 0.0;
    m_params[QStringLiteral("alignAngle")] = 0.0;
    m_params[QStringLiteral("alignResponse")] = 0.0;
}

void OpencvDefectNode::run(bool)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat gray = OpencvUtil::himageToMat(input);
        if (gray.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (gray.channels() == 3)
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);

        const int roiW = m_params.value(QStringLiteral("roiWidth"), 0).toInt();
        const int roiH = m_params.value(QStringLiteral("roiHeight"), 0).toInt();
        cv::Rect roi(0, 0, gray.cols, gray.rows);
        if (roiW > 1 && roiH > 1) {
            const int x = qBound(0, m_params.value(QStringLiteral("roiCol")).toInt(), gray.cols - 1);
            const int y = qBound(0, m_params.value(QStringLiteral("roiRow")).toInt(), gray.rows - 1);
            roi = cv::Rect(x, y,
                           qBound(1, roiW, gray.cols - x),
                           qBound(1, roiH, gray.rows - y));
        }
        cv::Mat inspect = gray(roi).clone();

        const QString goldenPath =
            m_params.value(QStringLiteral("goldenPath")).toString().trimmed();
        const bool train = m_params.value(QStringLiteral("trainGolden"), true).toBool();
        cv::Mat golden;
        if (train) {
            golden = inspect.clone();
            if (!goldenPath.isEmpty()) {
                QFileInfo fi(goldenPath);
                if (!fi.dir().exists())
                    QDir().mkpath(fi.absolutePath());
                if (cv::imwrite(goldenPath.toStdString(), golden))
                    m_params[QStringLiteral("trainStatus")] =
                        QStringLiteral("已保存黄金图: %1").arg(goldenPath);
                else
                    m_params[QStringLiteral("trainStatus")] = QStringLiteral("黄金图保存失败");
            } else {
                m_params[QStringLiteral("trainStatus")] = QStringLiteral("已从 ROI 教学（未落盘）");
            }
        } else if (!goldenPath.isEmpty()) {
            golden = cv::imread(goldenPath.toStdString(), cv::IMREAD_GRAYSCALE);
            m_params[QStringLiteral("trainStatus")] = golden.empty()
                ? QStringLiteral("黄金图加载失败") : QStringLiteral("已加载黄金图");
        }
        if (golden.empty()) {
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }
        if (golden.size() != inspect.size())
            cv::resize(golden, golden, inspect.size(), 0, 0, cv::INTER_LINEAR);

        // ── 对齐归一化（P1-1 补完）──────────────────────────────────────────────────
        // 为什么必须做：黄金差影对位置极敏感——工件在新图里偏 1 像素，整圈轮廓就会被判成缺陷，
        // 这是"标准件比对"在现场最常翻车的地方。做法：phaseCorrelate 求亚像素平移（可选先由
        // 对数极坐标估旋转），再把"当前图"仿射回黄金模板位置，之后照常差影。
        // 两道闸门防止"低纹理图乱移"：置信度 alignMinResponse、偏移上限 alignMaxShift。
        const int alignMode = m_params.value(QStringLiteral("alignMode"), 1).toInt();
        double alignDx = 0.0, alignDy = 0.0, alignAngle = 0.0, alignResp = 0.0;
        bool alignApplied = false;
        if (alignMode > 0 && !golden.empty() && golden.size() == inspect.size()) {
            const cv::Mat goldF = toFloatGray(golden);
            const cv::Mat inspF = toFloatGray(inspect);
            const cv::Point2d center(inspect.cols / 2.0, inspect.rows / 2.0);

            // ① 旋转（仅"平移+旋转"模式）：绕图像中心估，旋转会带动内容，故平移必须在其后重估
            double angleEst = 0.0;
            if (alignMode >= 2) {
                const double rangeDeg =
                    m_params.value(QStringLiteral("alignAngleRange"), 5).toInt();
                angleEst = estimateRotationSearch(goldF, inspF, rangeDeg);
            }

            // ② 先按估计角把当前图转回，再求残余平移（亚像素）
            cv::Mat deRotated = inspF;
            if (std::abs(angleEst) > 1e-6) {
                const cv::Mat R = cv::getRotationMatrix2D(center, -angleEst, 1.0);
                cv::warpAffine(inspF, deRotated, R, inspF.size(), cv::INTER_LINEAR,
                               cv::BORDER_REPLICATE);
            }
            double resp = 0.0;
            const cv::Point2d shift = cv::phaseCorrelate(goldF, deRotated, cv::noArray(), &resp);

            // 注意 phaseCorrelate 是**循环相关**：位移超过图像边长一半会折回（例如 80 px ≡ -48 px），
            // 因此 alignMaxShift 只能拦住 (maxShift, 边长/2] 区间的估计值；把上限设成"工件可能的最大位移"
            // 即可（折回区间通常已在物理上不可能）。另外折回后的对齐“碰巧”正确也无害。
            const int maxShift = m_params.value(QStringLiteral("alignMaxShift"), 50).toInt();
            const double minResp =
                m_params.value(QStringLiteral("alignMinResponse"), 0.05).toDouble();
            alignResp = resp;
            if (resp >= minResp && std::abs(shift.x) <= maxShift && std::abs(shift.y) <= maxShift) {
                // ③ 旋转 + 平移合成一次仿射，避免二次插值损失
                cv::Mat M = cv::getRotationMatrix2D(center, -angleEst, 1.0);
                M.at<double>(0, 2) -= shift.x;
                M.at<double>(1, 2) -= shift.y;
                cv::Mat alignedF;
                cv::warpAffine(inspF, alignedF, M, inspF.size(), cv::INTER_LINEAR,
                               cv::BORDER_REPLICATE);
                alignedF.convertTo(inspect, inspect.type());
                alignDx = shift.x;
                alignDy = shift.y;
                alignAngle = angleEst;
                alignApplied = true;
            }
            // 与黄金模板对比时会用到灰度，故对齐结果回写 inspect 后继续走原流程
        }
        m_params[QStringLiteral("alignApplied")] = alignApplied;
        m_params[QStringLiteral("alignDx")] = alignDx;
        m_params[QStringLiteral("alignDy")] = alignDy;
        m_params[QStringLiteral("alignAngle")] = alignAngle;
        m_params[QStringLiteral("alignResponse")] = alignResp;

        cv::Mat diff;
        cv::absdiff(inspect, golden, diff);
        cv::Mat bin;
        cv::threshold(diff, bin, m_params.value(QStringLiteral("diffThreshold"), 25).toInt(),
                      255, cv::THRESH_BINARY);
        int k = m_params.value(QStringLiteral("morphSize"), 3).toInt();
        if (k % 2 == 0) ++k;
        if (k >= 3) {
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
            cv::morphologyEx(bin, bin, cv::MORPH_OPEN, kernel);
        }
        // 边缘抑制：黄金模板强边缘 ±guard 内不判缺陷（对齐后仍有亚像素残差，硬边缘必然差出细线）
        applyEdgeGuard(golden, bin, m_params.value(QStringLiteral("edgeGuard"), 2).toInt(),
                       m_params.value(QStringLiteral("edgeThresh"), 60).toInt());

        cv::Mat fullBin = cv::Mat::zeros(gray.size(), CV_8UC1);
        bin.copyTo(fullBin(roi));
        OpencvUtil::applyGrayMask(fullBin, editMask());
        bin = fullBin(roi).clone();

        cv::Mat labels, stats, centroids;
        const int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8, CV_32S);
        const double minA = m_params.value(QStringLiteral("minArea"), 20.0).toDouble();
        const double maxA = m_params.value(QStringLiteral("maxArea"), 1e9).toDouble();
        QVector<double> boxes;
        double totalArea = 0.0;
        int count = 0;
        cv::Mat color;
        cv::cvtColor(gray, color, cv::COLOR_GRAY2BGR);
        for (int i = 1; i < n; ++i) {
            const double area = stats.at<int>(i, cv::CC_STAT_AREA);
            if (area < minA || area > maxA)
                continue;
            const int x = stats.at<int>(i, cv::CC_STAT_LEFT) + roi.x;
            const int y = stats.at<int>(i, cv::CC_STAT_TOP) + roi.y;
            const int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
            const int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
            boxes << x << y << w << h;
            totalArea += area;
            ++count;
            cv::rectangle(color, cv::Rect(x, y, w, h), cv::Scalar(0, 0, 255), 2);
        }

        const bool ng = totalArea >= m_params.value(QStringLiteral("ngArea"), 50.0).toDouble();
        m_params[QStringLiteral("defectCount")] = count;
        m_params[QStringLiteral("defectArea")] = totalArea;
        m_params["moduleStatus"] = !ng;
        m_outputImage = OpencvUtil::matToHimage(color);

        MeasureResult res;
        res.type = QStringLiteral("defect");
        res.valueName = QStringLiteral("缺陷面积");
        res.valid = true;
        res.value = totalArea;
        res.extraValues = boxes;
        auto obj = QSharedPointer<DataObject>::create();
        obj->setMeasureResult(res);
        setOutputData(1, obj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvDefectNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvDefectNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvDefectNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

RoiShape OpencvDefectNode::geometryRoi() const
{
    RoiShape s;
    s.type = RoiType::Rect;
    const double col = m_params.value(QStringLiteral("roiCol")).toDouble();
    const double row = m_params.value(QStringLiteral("roiRow")).toDouble();
    const double w = m_params.value(QStringLiteral("roiWidth")).toDouble();
    const double h = m_params.value(QStringLiteral("roiHeight")).toDouble();
    if (w <= 1 || h <= 1)
        return RoiShape();
    s.p1 = QPointF(col, row);
    s.p2 = QPointF(col + w, row + h);
    return s;
}

void OpencvDefectNode::applyGeometryRoi(const RoiShape &shape)
{
    const QRectF r = roiAxisAlignedBounds(shape);
    if (r.width() < 1 || r.height() < 1)
        return;
    setParam(QStringLiteral("roiCol"), int(r.x() + 0.5));
    setParam(QStringLiteral("roiRow"), int(r.y() + 0.5));
    setParam(QStringLiteral("roiWidth"), qMax(1, int(r.width() + 0.5)));
    setParam(QStringLiteral("roiHeight"), qMax(1, int(r.height() + 0.5)));
    setParam(QStringLiteral("trainGolden"), true);
}
