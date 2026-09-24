#include "AnomalyDetectNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"

#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <algorithm>
#include <cmath>

AnomalyDetectNode::AnomalyDetectNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("异常检测"));
    m_type = SHAPE_ANALYSIS;
}

void AnomalyDetectNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("异常"), PortDataType::Measure);
    registerParams({
        makeFilePathParam(QStringLiteral("baselinePath"), QString(),
                          QStringLiteral("基线文件（空=只在内存里，重启即失）")),
        // 默认**只推理**：教学会改写基线，把默认值设成教学就是"跑一轮产线就把模型刷了"，
        // 而且没有任何提示。要教学必须显式选。
        makeEnumParam(QStringLiteral("trainMode"), 0,
                      {QStringLiteral("只推理（用已加载基线）"),
                       QStringLiteral("覆盖式教学（清空基线，以当前图为第 1 个样本）"),
                       QStringLiteral("累加教学（在当前基线上追加当前图）")},
                      QStringLiteral("教学模式")),
        makeIntParam(QStringLiteral("roiCol"), 0, 0, 100000, QStringLiteral("ROI 列"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiRow"), 0, 0, 100000, QStringLiteral("ROI 行"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiWidth"), 0, 0, 100000, QStringLiteral("ROI 宽（0=整图）"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiHeight"), 0, 0, 100000, QStringLiteral("ROI 高（0=整图）"), QStringLiteral("px")),
        // σ 下限（灰度单位）：平坦区 σ≈0，不钉下限则 |x-μ|/σ 在整片平坦区爆到几十~几百，
        // 表现成"满图噪声热图"，且这种假阳调 k 阈值救不回来（分数本身无界）。
        makeDoubleParam(QStringLiteral("sigmaFloor"), 2.0, 0.1, 255.0,
                        QStringLiteral("σ 下限（防平坦区除零爆分，灰度单位）"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("scoreSmooth"), 3, 0, 31,
                     QStringLiteral("score 图均值滤波核（0=关，抑制椒盐）"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("kSigma"), 5.0, 0.5, 100.0,
                        QStringLiteral("判定倍数（偏离几个 σ 算异常）"), QStringLiteral("σ")),
        makeIntParam(QStringLiteral("morphSize"), 3, 1, 31, QStringLiteral("形态学核"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("minArea"), 20.0, 0.0, 1e9, QStringLiteral("最小异常面积"), QStringLiteral("px²")),
        makeDoubleParam(QStringLiteral("maxArea"), 1e9, 0.0, 1e12, QStringLiteral("最大异常面积"), QStringLiteral("px²")),
        makeDoubleParam(QStringLiteral("ngArea"), 50.0, 0.0, 1e12, QStringLiteral("NG 面积阈值"), QStringLiteral("px²")),
        makeEnumParam(QStringLiteral("overlayMode"), 1,
                      {QStringLiteral("纯热图"), QStringLiteral("热图叠加原图"), QStringLiteral("原图+异常框")},
                      QStringLiteral("输出图像形式")),
        // 两个后端不是"新旧"关系：A 逐像素、B 看整体结构，各自的盲区不同，故留开关而不是自动选
        makeEnumParam(QStringLiteral("backend"), 0,
                      {QStringLiteral("统计基线（逐像素 μ/σ）"), QStringLiteral("PCA 残差（整体流形）")},
                      QStringLiteral("建模后端（小缺陷用 A；OK 样本本身有较大灰度漂移、要检结构异常用 B）")),
        makeIntParam(QStringLiteral("pcaComponents"), 8, 2, 64,
                     QStringLiteral("PCA 主成分数（保留的 OK 变化方向数）")),
        makeIntParam(QStringLiteral("pcaAnalysisSide"), 256, 32, 1024,
                     QStringLiteral("PCA 分析尺寸长边上限（越大越慢越占内存，且检得更细）"), QStringLiteral("px")),
        // ── 对齐归一化（与"缺陷检测"同名同默认值；实现见 OpencvUtil::alignToReference）──
        // 统计基线比黄金差影更怕位移吗？不：两者同源的失效方式都是"整圈轮廓被判异常"，
        // 而这里的 σ 只吸收了**灰度**波动，吸收不了几十像素的位置差。
        makeEnumParam(QStringLiteral("alignMode"), 1,
                      {QStringLiteral("不对齐"), QStringLiteral("平移对齐"),
                       QStringLiteral("平移+旋转对齐")},
                      QStringLiteral("对齐归一化（打分前把当前图对回基线参考图）")),
        makeIntParam(QStringLiteral("alignMaxShift"), 50, 0, 2000,
                     QStringLiteral("平移对齐最大允许偏移（超出视为对齐失败）"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("alignMinResponse"), 0.05, 0.0, 1.0,
                        QStringLiteral("对齐置信度下限（低于则不做变换，防低纹理图乱移）")),
        makeIntParam(QStringLiteral("alignAngleRange"), 5, 0, 45,
                     QStringLiteral("旋转搜索范围 ±（仅“平移+旋转”模式，步长 0.5°）"),
                     QStringLiteral("°")),
    });
    m_params[QStringLiteral("anomalyCount")] = 0;
    m_params[QStringLiteral("anomalyArea")] = 0.0;
    m_params[QStringLiteral("scoreMax")] = 0.0;
    m_params[QStringLiteral("scoreMean")] = 0.0;
    m_params[QStringLiteral("sampleCount")] = 0;
    m_params[QStringLiteral("trainStatus")] = QString();
    m_params[QStringLiteral("baselineStatus")] = QString();
    m_params[QStringLiteral("alignApplied")] = false;
    m_params[QStringLiteral("alignDx")] = 0.0;
    m_params[QStringLiteral("alignDy")] = 0.0;
    m_params[QStringLiteral("alignAngle")] = 0.0;
    m_params[QStringLiteral("alignResponse")] = 0.0;
}

void AnomalyDetectNode::run(bool)
{
    try {
        // 先复位本轮结果：下面多条错误分支会提前 return，不复位就会把"上一轮的检出"
        // 当成"本轮的检出"读出去（界面显示残留数字，测试更是会假绿）。
        m_params[QStringLiteral("anomalyCount")] = 0;
        m_params[QStringLiteral("anomalyArea")] = 0.0;
        m_params[QStringLiteral("scoreMax")] = 0.0;
        m_params[QStringLiteral("scoreMean")] = 0.0;
        m_params[QStringLiteral("trainStatus")] = QString();
        m_params[QStringLiteral("baselineStatus")] = QString();

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

        const QString path = m_params.value(QStringLiteral("baselinePath")).toString().trimmed();
        const int trainMode = m_params.value(QStringLiteral("trainMode"), 0).toInt();
        const int backend = m_params.value(QStringLiteral("backend"), 0).toInt();
        QString status;

        // 换文件 = 换模型：内存里留着的那份必须按当前后端重新装载（推理）或直接清空（教学）。
        // 不这么做就是"拿 A 产品的模型判 B 产品"——现场表现成热图全红却查不出原因。
        const bool stale = !path.isEmpty() && m_loadedFrom != path;
        if (stale) {
            m_baseline.clear();
            m_pca = AnomalyPcaModel();
            m_pcaSamples.clear();
            if (trainMode == 0) {
                QString err;
                const bool loaded =
                    (backend == 0) ? m_baseline.load(path, &err) : m_pca.load(path, &err);
                if (!loaded) {
                    m_params["moduleStatus"] = false;
                    m_params[QStringLiteral("baselineStatus")] = err;
                    m_outputImage = m_inputImage;
                    setOutputData(1, QSharedPointer<DataObject>());
                    return;
                }
            }
            // 走到这里内存模型已与 path 对齐（推理=刚装载成功；教学=从本轮起为该路径重建），
            // 两种情况都必须当下绑定。教学尤其不能等"第一次成功落盘"再绑定：后端 B 是批量拟合，
            // 第 1 个样本拟合不出模型也就不会落盘，下一轮就会被本判定当成换文件再清空，
            // 样本永远攒不满。
            m_loadedFrom = path;
        }
        if (trainMode == 1) {   // 覆盖式教学：旧模型（含另一个后端的）一律不用
            m_baseline.clear();
            m_pca = AnomalyPcaModel();
            m_pcaSamples.clear();
        }

        cv::Mat score;
        QString modelError;
        const bool scored =
            (backend == 0)
                ? teachAndScorePixel(inspect, path, trainMode, score, status, modelError)
                : teachAndScorePca(inspect, path, trainMode,
                                   m_params.value(QStringLiteral("pcaComponents"), 8).toInt(),
                                   m_params.value(QStringLiteral("pcaAnalysisSide"), 256).toInt(),
                                   score, status, modelError);
        m_params[QStringLiteral("trainStatus")] = status;
        if (!scored || score.empty()) {
            m_params["moduleStatus"] = false;
            if (!modelError.isEmpty())
                m_params[QStringLiteral("baselineStatus")] = modelError;
            m_outputImage = m_inputImage;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }
        m_params[QStringLiteral("sampleCount")] =
            backend == 0 ? m_baseline.sampleCount() : m_pca.sampleCount();

        const int smooth = m_params.value(QStringLiteral("scoreSmooth"), 3).toInt();
        if (smooth >= 2) {
            int k = smooth;
            if (k % 2 == 0)
                ++k;
            cv::blur(score, score, cv::Size(k, k));
        }

        double sMax = 0.0;
        cv::minMaxLoc(score, nullptr, &sMax);
        m_params[QStringLiteral("scoreMax")] = sMax;
        m_params[QStringLiteral("scoreMean")] = cv::mean(score)[0];

        const double kSigma = std::max(0.01, m_params.value(QStringLiteral("kSigma"), 5.0).toDouble());
        cv::Mat bin;
        cv::threshold(score, bin, kSigma, 255.0, cv::THRESH_BINARY);
        bin.convertTo(bin, CV_8U);

        int mk = m_params.value(QStringLiteral("morphSize"), 3).toInt();
        if (mk % 2 == 0)
            ++mk;
        if (mk >= 3) {
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(mk, mk));
            cv::morphologyEx(bin, bin, cv::MORPH_OPEN, kernel);
        }

        // 屏蔽区在整幅坐标系上生效（与 OpencvDefect 同一做法）：先铺回全图、套掩膜、再裁回 ROI
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
        for (int i = 1; i < n; ++i) {
            const double area = stats.at<int>(i, cv::CC_STAT_AREA);
            if (area < minA || area > maxA)
                continue;
            boxes << stats.at<int>(i, cv::CC_STAT_LEFT) + roi.x
                  << stats.at<int>(i, cv::CC_STAT_TOP) + roi.y
                  << stats.at<int>(i, cv::CC_STAT_WIDTH)
                  << stats.at<int>(i, cv::CC_STAT_HEIGHT);
            totalArea += area;
            ++count;
        }
        m_params[QStringLiteral("anomalyCount")] = count;
        m_params[QStringLiteral("anomalyArea")] = totalArea;

        const int overlayMode = m_params.value(QStringLiteral("overlayMode"), 1).toInt();
        const cv::Mat heat = heatFromScore(score, kSigma);
        cv::Mat jet;
        cv::applyColorMap(heat, jet, cv::COLORMAP_JET);
        cv::Mat out;
        if (overlayMode == 0) {
            out = jet;
        } else {
            cv::cvtColor(gray, out, cv::COLOR_GRAY2BGR);
            if (overlayMode == 1) {
                cv::Mat fullHeat = cv::Mat::zeros(gray.size(), CV_8UC1);
                heat.copyTo(fullHeat(roi));
                cv::Mat fullJet;
                cv::applyColorMap(fullHeat, fullJet, cv::COLORMAP_JET);
                cv::addWeighted(fullJet, 0.5, out, 0.5, 0.0, out);
            }
        }
        if (overlayMode != 0) {
            for (int i = 0; i + 3 < boxes.size(); i += 4)
                cv::rectangle(out,
                              cv::Rect(int(boxes[i]), int(boxes[i + 1]), int(boxes[i + 2]), int(boxes[i + 3])),
                              cv::Scalar(0, 0, 255), 2);
        }

        const bool ng = totalArea >= m_params.value(QStringLiteral("ngArea"), 50.0).toDouble();
        m_params["moduleStatus"] = !ng;
        m_outputImage = OpencvUtil::matToHimage(out);

        MeasureResult res;
        res.type = QStringLiteral("anomaly");
        res.valueName = QStringLiteral("异常面积");
        res.valid = true;
        res.value = totalArea;
        res.extraValues = boxes;
        auto obj = QSharedPointer<DataObject>::create();
        obj->setMeasureResult(res);
        setOutputData(1, obj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "AnomalyDetectNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

void AnomalyDetectNode::alignWorking(const cv::Mat &ref, const cv::Mat &cur, cv::Mat &aligned)
{
    // σ 吸收的是**灰度**波动，吸收不了位置差：不对齐时工件轮廓整圈都会被算成异常，
    // 失效方式与黄金差影完全同源，故复用同一套估计与双闸门（见 OpencvUtil::alignToReference）。
    const OpencvUtil::AlignInfo a = OpencvUtil::alignToReference(
        ref, cur, m_params.value(QStringLiteral("alignMode"), 1).toInt(),
        m_params.value(QStringLiteral("alignMaxShift"), 50).toInt(),
        m_params.value(QStringLiteral("alignMinResponse"), 0.05).toDouble(),
        m_params.value(QStringLiteral("alignAngleRange"), 5).toInt(), aligned);
    m_params[QStringLiteral("alignApplied")] = a.applied;
    m_params[QStringLiteral("alignDx")] = a.dx;
    m_params[QStringLiteral("alignDy")] = a.dy;
    m_params[QStringLiteral("alignAngle")] = a.angle;
    m_params[QStringLiteral("alignResponse")] = a.response;
}

bool AnomalyDetectNode::teachAndScorePixel(const cv::Mat &inspect, const QString &path,
                                           int trainMode, cv::Mat &score, QString &status,
                                           QString &error)
{
    cv::Mat working;
    alignWorking(m_baseline.isEmpty() ? cv::Mat() : m_baseline.reference(), inspect, working);

    if (trainMode != 0) {
        if (!m_baseline.addSample(working)) {
            error = QStringLiteral("基线尺寸与当前 ROI 不符（%1×%2 vs %3×%4），请改用覆盖式教学重教学")
                        .arg(m_baseline.size().width)
                        .arg(m_baseline.size().height)
                        .arg(working.cols)
                        .arg(working.rows);
            return false;
        }
        status = QStringLiteral("已累积样本 %1 个（%2×%3）")
                     .arg(m_baseline.sampleCount())
                     .arg(working.cols)
                     .arg(working.rows);
        if (!path.isEmpty()) {
            QString saveErr;
            if (!m_baseline.save(path, &saveErr))
                status += QStringLiteral("；保存失败：%1").arg(saveErr);
            else
                m_loadedFrom = path;
        }
    } else if (m_baseline.isEmpty()) {
        error = QStringLiteral("基线为空：先教学（或指定基线文件）");
        return false;
    }

    score = m_baseline.scoreMap(working, m_params.value(QStringLiteral("sigmaFloor"), 2.0).toDouble());
    if (score.empty()) {
        error = QStringLiteral("当前 ROI 尺寸与基线不符，无法打分");
        return false;
    }
    return true;
}

bool AnomalyDetectNode::teachAndScorePca(const cv::Mat &inspect, const QString &path, int trainMode,
                                         int components, int analysisSide, cv::Mat &score,
                                         QString &status, QString &error)
{
    // 分析尺寸：D（=像素数）决定协方差/基向量的内存与拟合耗时，按长边等比缩到上限。
    // 代价是检不出 3 像素级的小点——那是后端 A 的活。
    const int longSide = std::max(1, std::max(inspect.cols, inspect.rows));
    const double s =
        (analysisSide > 0 && longSide > analysisSide) ? double(analysisSide) / longSide : 1.0;
    const cv::Size target(std::max(1, int(inspect.cols * s)), std::max(1, int(inspect.rows * s)));
    cv::Mat small;
    cv::resize(inspect, small, target, 0, 0, cv::INTER_AREA);

    cv::Mat working;
    alignWorking(m_pcaSamples.empty() ? cv::Mat() : m_pcaSamples.front(), small, working);

    if (trainMode != 0) {
        // 环形缓冲：PCA 要批量拟合，攒下的样本就是训练集；到上限丢最旧（现场是"持续补教学"）
        constexpr size_t kMaxSamples = 128;
        if (m_pcaSamples.size() >= kMaxSamples)
            m_pcaSamples.erase(m_pcaSamples.begin());
        m_pcaSamples.push_back(working.clone());

        QString trainErr;
        if (!m_pca.train(m_pcaSamples, components, &trainErr)) {
            error = trainErr;
            return false;
        }
        status = QStringLiteral("已拟合 PCA 模型（样本 %1，主成分 %2，分析尺寸 %3×%4）")
                     .arg(m_pca.sampleCount())
                     .arg(m_pca.components())
                     .arg(m_pca.analysisSize().width)
                     .arg(m_pca.analysisSize().height);
        if (!path.isEmpty()) {
            QString saveErr;
            if (!m_pca.save(path, &saveErr))
                status += QStringLiteral("；保存失败：%1").arg(saveErr);
            else
                m_loadedFrom = path;
        }
    } else if (m_pca.isEmpty()) {
        error = QStringLiteral("PCA 模型为空：先教学（或指定模型文件，注意与统计基线文件不通用）");
        return false;
    }

    score = m_pca.scoreMap(working, m_params.value(QStringLiteral("sigmaFloor"), 2.0).toDouble());
    if (score.empty()) {
        error = QStringLiteral("当前 ROI 与分析尺寸不符，无法打分");
        return false;
    }
    if (score.size() != inspect.size())
        cv::resize(score, score, inspect.size(), 0, 0, cv::INTER_LINEAR);
    return true;
}

cv::Mat AnomalyDetectNode::heatFromScore(const cv::Mat &score, double kSigma)
{
    cv::Mat heatF;
    cv::multiply(score, cv::Scalar(127.5 / std::max(0.01, kSigma)), heatF);
    cv::Mat heat;
    heatF.convertTo(heat, CV_8U);   // 越界自动饱和到 255
    return heat;
}

QWidget *AnomalyDetectNode::createParamPanel()
{
    return createAutoParamPanel();
}

void AnomalyDetectNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

RoiShape AnomalyDetectNode::geometryRoi() const
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

void AnomalyDetectNode::applyGeometryRoi(const RoiShape &shape)
{
    const QRectF r = roiAxisAlignedBounds(shape);
    if (r.width() < 1 || r.height() < 1)
        return;
    setParam(QStringLiteral("roiCol"), int(r.x() + 0.5));
    setParam(QStringLiteral("roiRow"), int(r.y() + 0.5));
    setParam(QStringLiteral("roiWidth"), qMax(1, int(r.width() + 0.5)));
    setParam(QStringLiteral("roiHeight"), qMax(1, int(r.height() + 0.5)));
    // 手画 ROI 的意图就是"圈出来教一遍"；用累加而非覆盖，避免把之前攒的样本一笔画清掉
    setParam(QStringLiteral("trainMode"), 2);
}
