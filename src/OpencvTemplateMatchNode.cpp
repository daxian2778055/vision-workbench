#include "OpencvTemplateMatchNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include "FlowScene.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QWidget>
#include <QFileInfo>
#include <QDir>
#include <QVector>
#include <cmath>
#include <utility>

namespace {

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

OpencvTemplateMatchNode::OpencvTemplateMatchNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("模板匹配"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvTemplateMatchNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("匹配结果"), PortDataType::Measure);
    registerParams({
        makeFilePathParam(QStringLiteral("templatePath"), QString(),
                          QStringLiteral("模板图像文件 (png/bmp/jpg)")),
        makeBoolParam(QStringLiteral("trainFromImage"), true,
                      QStringLiteral("从图像 ROI 训练并保存模板")),
        makeIntParam(QStringLiteral("method"), 1, 0, 1,
                     QStringLiteral("匹配方法：0=归一化平方差，1=归一化相关系数")),
        makeDoubleParam(QStringLiteral("minScore"), 0.6, 0.0, 1.0,
                        QStringLiteral("最低匹配度")),
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
    });
    m_params[QStringLiteral("matchRow")] = 0.0;
    m_params[QStringLiteral("matchCol")] = 0.0;
    m_params[QStringLiteral("matchScore")] = 0.0;
    m_params[QStringLiteral("matchAngle")] = 0.0;
    m_params[QStringLiteral("matchScale")] = 1.0;
    m_params[QStringLiteral("trainStatus")] = QString();
}

cv::Mat OpencvTemplateMatchNode::extractTemplatePatch(const cv::Mat &gray, const RoiShape &roi)
{
    if (gray.empty())
        return {};
    if (roi.type == RoiType::RotatedRect && roi.width > 1 && roi.height > 1) {
        const int w = qMax(1, int(roi.width + 0.5));
        const int h = qMax(1, int(roi.height + 0.5));
        const cv::Point2f c(float(roi.p1.x()), float(roi.p1.y()));
        cv::Mat M = cv::getRotationMatrix2D(c, roi.angleDeg, 1.0);
        M.at<double>(0, 2) += w * 0.5 - c.x;
        M.at<double>(1, 2) += h * 0.5 - c.y;
        cv::Mat patch;
        cv::warpAffine(gray, patch, M, cv::Size(w, h), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        return patch;
    }
    const QRectF r = (roi.type == RoiType::Rect) ? roiAxisAlignedBounds(roi)
                                                 : ((roi.width > 1 && roi.height > 1)
                                                        ? roiAxisAlignedBounds(roi) : QRectF());
    if (r.width() > 1 && r.height() > 1) {
        const int x = qBound(0, int(r.x()), gray.cols - 1);
        const int y = qBound(0, int(r.y()), gray.rows - 1);
        const int w = qBound(1, int(r.width() + 0.5), gray.cols - x);
        const int h = qBound(1, int(r.height() + 0.5), gray.rows - y);
        return gray(cv::Rect(x, y, w, h)).clone();
    }
    return {};
}

void OpencvTemplateMatchNode::run(bool /*autoSwitch*/)
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
        if (gray.channels() == 3) {
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
        }

        const QString templatePath =
            m_params.value(QStringLiteral("templatePath"), QString()).toString().trimmed();
        const bool train = m_params.value(QStringLiteral("trainFromImage"), true).toBool();
        const int methodIdx = m_params.value(QStringLiteral("method"), 1).toInt();
        const double minScore = m_params.value(QStringLiteral("minScore"), 0.6).toDouble();
        const int method = (methodIdx == 0) ? cv::TM_SQDIFF_NORMED : cv::TM_CCOEFF_NORMED;

        cv::Mat tmpl;
        if (train) {
            const RoiShape roi = geometryRoi();
            tmpl = extractTemplatePatch(gray, roi);
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
                    } else {
                        m_params[QStringLiteral("trainStatus")] =
                            QStringLiteral("模板保存失败: %1").arg(templatePath);
                    }
                } catch (const std::exception &e) {
                    m_params[QStringLiteral("trainStatus")] =
                        QStringLiteral("模板保存异常: %1").arg(QString::fromLocal8Bit(e.what()));
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
            cv::Mat loaded = cv::imread(templatePath.toStdString(), cv::IMREAD_GRAYSCALE);
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

        QVector<double> angles;
        QVector<double> scales;
        collectSearchValues(m_params.value(QStringLiteral("angleMin"), -30.0).toDouble(),
                            m_params.value(QStringLiteral("angleMax"), 30.0).toDouble(),
                            m_params.value(QStringLiteral("angleStep"), 2.0).toDouble(),
                            90, angles);
        collectSearchValues(m_params.value(QStringLiteral("scaleMin"), 1.0).toDouble(),
                            m_params.value(QStringLiteral("scaleMax"), 1.0).toDouble(),
                            m_params.value(QStringLiteral("scaleStep"), 0.05).toDouble(),
                            16, scales);

        const bool singleIdentity = (angles.size() == 1 && qAbs(angles.first()) < 1e-6
                                     && scales.size() == 1 && qAbs(scales.first() - 1.0) < 1e-6);

        double bestScore = -1e9;
        cv::Point bestLoc(0, 0);
        double bestAngle = 0.0;
        double bestScale = 1.0;
        cv::Point2f bestCenterInWarp(tmpl.cols * 0.5f, tmpl.rows * 0.5f);
        const double tmplW = tmpl.cols;
        const double tmplH = tmpl.rows;

        for (double scale : scales) {
            for (double angle : angles) {
                cv::Point2f centerInWarp;
                cv::Mat warped = warpTemplate(tmpl, angle, scale, &centerInWarp);
                if (warped.empty() || warped.cols >= gray.cols || warped.rows >= gray.rows)
                    continue;

                cv::Mat result;
                cv::matchTemplate(gray, warped, result, method);

                if (singleIdentity) {
                    cv::Mat grayF;
                    gray.convertTo(grayF, CV_32F);
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

                double minVal = 0.0, maxVal = 0.0;
                cv::Point minLoc, maxLoc;
                cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

                double score = 0.0;
                cv::Point loc;
                if (methodIdx == 0) {
                    score = 1.0 - minVal;
                    loc = minLoc;
                } else {
                    score = maxVal;
                    loc = maxLoc;
                }
                if (score > bestScore) {
                    bestScore = score;
                    bestLoc = loc;
                    bestAngle = angle;
                    bestScale = scale;
                    bestCenterInWarp = centerInWarp;
                }
            }
        }

        if (bestScore < -1e8) {
            m_params[QStringLiteral("trainStatus")] = QStringLiteral("无有效搜索位姿（模板相对图像过大）");
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }

        const double col = bestLoc.x + bestCenterInWarp.x;
        const double row = bestLoc.y + bestCenterInWarp.y;
        const double outW = tmplW * bestScale;
        const double outH = tmplH * bestScale;

        m_params[QStringLiteral("matchRow")] = row;
        m_params[QStringLiteral("matchCol")] = col;
        m_params[QStringLiteral("matchScore")] = bestScore;
        m_params[QStringLiteral("matchAngle")] = bestAngle;
        m_params[QStringLiteral("matchScale")] = bestScale;
        m_params["moduleStatus"] = (bestScore >= minScore);
        const QString fixtureName =
            m_params.value(QStringLiteral("writeFixtureName")).toString().trimmed();
        if (!fixtureName.isEmpty() && bestScore >= minScore) {
            if (FlowScene *fs = flowSceneRef())
                fs->setFixturePose(fixtureName, row, col, bestAngle, bestScale);
        }
        m_outputImage = m_inputImage;

        MeasureResult res;
        res.type = QStringLiteral("template");
        res.valueName = QStringLiteral("匹配得分");
        res.valid = (bestScore >= minScore);
        res.value = bestScore;
        res.point1 = QPointF(col, row);
        res.extraValues = QVector<double>({row, col, bestScore, bestAngle, bestScale, outW, outH});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvTemplateMatchNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
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
