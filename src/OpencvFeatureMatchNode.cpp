#include "OpencvFeatureMatchNode.h"
#include "OpencvTemplateMatchNode.h"   // 复用 extractTemplatePatch（同一套 ROI→模板块抠取）
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include "FlowScene.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/calib3d.hpp>
#include <QWidget>
#include <QFileInfo>
#include <QDir>
#include <climits>
#include <cmath>
#include <vector>

namespace {

const char *kTag = "OpencvFeatureMatchNode";

cv::Ptr<cv::Feature2D> makeDetector(int idx, int maxFeatures)
{
    const int n = (maxFeatures > 0) ? maxFeatures : INT_MAX;
    if (idx == 1)
        return cv::SIFT::create(n);
    return cv::ORB::create(n);
}

/// FLANN 的树索引不支持二进制描述子（ORB），且样本太少时 KDTree 会直接抛异常；
/// 这两种情形不是"配置错了"，而是"这条路在这个输入上不通"——由调用方写进 matchNote，
/// 不能悄悄换一条路还宣称用的是用户选的那条。
bool flannApplicable(const cv::Mat &a, const cv::Mat &b, QString *reason)
{
    if (a.type() == CV_8U || b.type() == CV_8U) {
        if (reason)
            *reason = QStringLiteral("FLANN 不适用于二进制描述子（ORB）");
        return false;
    }
    if (a.rows < 32 || b.rows < 32) {
        if (reason)
            *reason = QStringLiteral("FLANN 需要两侧各 ≥32 条描述子（实际 %1/%2）").arg(a.rows).arg(b.rows);
        return false;
    }
    return true;
}

void matchWithKnn(const cv::Mat &objDesc, const cv::Mat &sceneDesc, bool useFlann,
                  std::vector<std::vector<cv::DMatch>> *knn, QString *note)
{
    if (useFlann) {
        QString why;
        if (flannApplicable(objDesc, sceneDesc, &why)) {
            try {
                cv::FlannBasedMatcher m(cv::makePtr<cv::flann::KDTreeIndexParams>(5),
                                        cv::makePtr<cv::flann::SearchParams>(50));
                m.knnMatch(objDesc, sceneDesc, *knn, 2);
                return;
            } catch (const cv::Exception &e) {
                why = QStringLiteral("FLANN 抛出异常: %1").arg(QString::fromUtf8(e.err.c_str()));
            }
        }
        if (note)
            *note = why + QStringLiteral(" ⇒ 本跑次改用 BF");
    }
    cv::BFMatcher bf(objDesc.type() == CV_8U ? cv::NORM_HAMMING : cv::NORM_L2);
    bf.knnMatch(objDesc, sceneDesc, *knn, 2);
}

} // namespace

OpencvFeatureMatchNode::OpencvFeatureMatchNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("特征匹配"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvFeatureMatchNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("匹配结果"), PortDataType::Measure);
    registerParams({
        makeFilePathParam(QStringLiteral("templatePath"), QString(),
                          QStringLiteral("模板图像文件 (png/bmp/jpg)")),
        makeBoolParam(QStringLiteral("trainFromImage"), true,
                      QStringLiteral("从图像 ROI 教学特征模板")),
        makeEnumParam(QStringLiteral("detector"), 0,
                      {QStringLiteral("ORB（二进制/快）"), QStringLiteral("SIFT（尺度不变）")},
                      QStringLiteral("特征检测器")),
        makeEnumParam(QStringLiteral("matcher"), 0,
                      {QStringLiteral("暴力匹配 BF"), QStringLiteral("FLANN")},
                      QStringLiteral("描述子匹配器")),
        makeIntParam(QStringLiteral("nFeatures"), 2000, 0, 20000,
                     QStringLiteral("特征点上限（0=不限）")),
        makeDoubleParam(QStringLiteral("ratioThreshold"), 0.75, 0.1, 1.0,
                        QStringLiteral("Lowe 比值阈值")),
        makeIntParam(QStringLiteral("minMatches"), 6, 2, 1000,
                     QStringLiteral("最少候选点对")),
        makeIntParam(QStringLiteral("minInliers"), 5, 2, 1000,
                     QStringLiteral("最少 RANSAC 内点")),
        makeDoubleParam(QStringLiteral("minScore"), 0.3, 0.0, 1.0,
                        QStringLiteral("最低内点率")),
        makeDoubleParam(QStringLiteral("ransacThreshold"), 3.0, 0.5, 20.0,
                        QStringLiteral("RANSAC 阈值"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("roiCenterCol"), 0.0, 0.0, 100000.0,
                        QStringLiteral("模板框中心列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("roiCenterRow"), 0.0, 0.0, 100000.0,
                        QStringLiteral("模板框中心行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("roiWidth"), 0.0, 0.0, 100000.0,
                        QStringLiteral("模板框宽（0=自动中心 50%）"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("roiHeight"), 0.0, 0.0, 100000.0,
                        QStringLiteral("模板框高（0=自动中心 50%）"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("roiAngle"), 0.0, -180.0, 180.0,
                        QStringLiteral("模板框角度"), QStringLiteral("°")),
        makeStringParam(QStringLiteral("writeFixtureName"), QString(),
                        QStringLiteral("匹配位姿写入 Fixture 名（空=不写）")),
    });
    m_params[QStringLiteral("matchRow")] = 0.0;
    m_params[QStringLiteral("matchCol")] = 0.0;
    m_params[QStringLiteral("matchScore")] = 0.0;
    m_params[QStringLiteral("matchAngle")] = 0.0;
    m_params[QStringLiteral("matchScale")] = 1.0;
    m_params[QStringLiteral("templateKeypoints")] = 0;
    m_params[QStringLiteral("sceneKeypoints")] = 0;
    m_params[QStringLiteral("candidateMatches")] = 0;
    m_params[QStringLiteral("inlierCount")] = 0;
    m_params[QStringLiteral("trainStatus")] = QString();
    m_params[QStringLiteral("matchNote")] = QString();
}

RoiShape OpencvFeatureMatchNode::geometryRoi() const
{
    const double w = m_params.value(QStringLiteral("roiWidth"), 0.0).toDouble();
    const double h = m_params.value(QStringLiteral("roiHeight"), 0.0).toDouble();
    if (w <= 1 || h <= 1)
        return RoiShape();
    RoiShape s;
    s.type = RoiType::RotatedRect;
    s.width = w;
    s.height = h;
    s.angleDeg = m_params.value(QStringLiteral("roiAngle"), 0.0).toDouble();
    s.p1 = QPointF(m_params.value(QStringLiteral("roiCenterCol"), 0.0).toDouble(),
                   m_params.value(QStringLiteral("roiCenterRow"), 0.0).toDouble());
    return s;
}

void OpencvFeatureMatchNode::applyGeometryRoi(const RoiShape &shape)
{
    if (shape.type == RoiType::None) {
        setParam(QStringLiteral("roiWidth"), 0);
        setParam(QStringLiteral("roiHeight"), 0);
        setParam(QStringLiteral("roiAngle"), 0.0);
        return;
    }
    double cx = 0.0, cy = 0.0, w = 0.0, h = 0.0, ang = 0.0;
    if (shape.type == RoiType::RotatedRect) {
        cx = shape.p1.x();
        cy = shape.p1.y();
        w = shape.width;
        h = shape.height;
        ang = shape.angleDeg;
    } else if (shape.type == RoiType::Rect) {
        // Rect 只带两角点，宽高须由轴对齐包围盒算出（直接读 shape.width 会存成 0）
        const QRectF box = roiAxisAlignedBounds(shape);
        cx = box.center().x();
        cy = box.center().y();
        w = box.width();
        h = box.height();
    } else {
        return;
    }
    if (w <= 1 || h <= 1)
        return;
    setParam(QStringLiteral("roiCenterCol"), cx);
    setParam(QStringLiteral("roiCenterRow"), cy);
    setParam(QStringLiteral("roiWidth"), qMax(1, int(w + 0.5)));
    setParam(QStringLiteral("roiHeight"), qMax(1, int(h + 0.5)));
    setParam(QStringLiteral("roiAngle"), ang);
    setParam(QStringLiteral("trainFromImage"), true);
}

void OpencvFeatureMatchNode::run(bool /*autoSwitch*/)
{
    const auto fail = [this](const QString &why) {
        m_params[QStringLiteral("matchNote")] = why;
        m_params["moduleStatus"] = false;
        m_outputImage = m_inputImage;
        setOutputData(1, QSharedPointer<DataObject>());
    };

    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat gray = OpencvUtil::himageToMat(input);
        if (gray.empty()) {
            fail(QStringLiteral("图像桥接失败（himageToMat 返回空）"));
            return;
        }
        if (gray.channels() == 3)
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);

        const QString templatePath =
            m_params.value(QStringLiteral("templatePath"), QString()).toString().trimmed();
        const bool train = m_params.value(QStringLiteral("trainFromImage"), true).toBool();

        cv::Mat tmpl;
        if (train) {
            tmpl = OpencvTemplateMatchNode::extractTemplatePatch(gray, geometryRoi());
            if (tmpl.empty()) {
                const int x = static_cast<int>(gray.cols * 0.25);
                const int y = static_cast<int>(gray.rows * 0.25);
                const int w = std::max(1, static_cast<int>(gray.cols * 0.50));
                const int h = std::max(1, static_cast<int>(gray.rows * 0.50));
                tmpl = gray(cv::Rect(x, y, w, h)).clone();
            }
            if (!templatePath.isEmpty()) {
                QFileInfo fi(templatePath);
                if (!fi.dir().exists())
                    QDir().mkpath(fi.absolutePath());
                m_params[QStringLiteral("trainStatus")] =
                    cv::imwrite(templatePath.toStdString(), tmpl)
                        ? QStringLiteral("已教学并保存: %1").arg(templatePath)
                        : QStringLiteral("模板保存失败: %1").arg(templatePath);
            } else {
                m_params[QStringLiteral("trainStatus")] = QStringLiteral("已教学（未保存文件）");
            }
        } else {
            if (templatePath.isEmpty()) {
                fail(QStringLiteral("匹配模式需要设置模板文件路径"));
                return;
            }
            tmpl = cv::imread(templatePath.toStdString(), cv::IMREAD_GRAYSCALE);
            if (tmpl.empty()) {
                fail(QStringLiteral("模板加载失败: %1").arg(templatePath));
                return;
            }
            m_params[QStringLiteral("trainStatus")] =
                QStringLiteral("已加载模板: %1").arg(templatePath);
        }

        if (tmpl.cols < 8 || tmpl.rows < 8) {
            fail(QStringLiteral("模板过小（%1×%2）").arg(tmpl.cols).arg(tmpl.rows));
            return;
        }

        const int detectorIdx = m_params.value(QStringLiteral("detector"), 0).toInt();
        const bool useFlann = (m_params.value(QStringLiteral("matcher"), 0).toInt() == 1);
        const double ratio = m_params.value(QStringLiteral("ratioThreshold"), 0.75).toDouble();
        const double reproj = m_params.value(QStringLiteral("ransacThreshold"), 3.0).toDouble();

        cv::Ptr<cv::Feature2D> f2d =
            makeDetector(detectorIdx, m_params.value(QStringLiteral("nFeatures"), 2000).toInt());
        std::vector<cv::KeyPoint> objKp, sceneKp;
        cv::Mat objDesc, sceneDesc;
        f2d->detectAndCompute(tmpl, cv::noArray(), objKp, objDesc);
        f2d->detectAndCompute(gray, cv::noArray(), sceneKp, sceneDesc);
        m_params[QStringLiteral("templateKeypoints")] = static_cast<int>(objKp.size());
        m_params[QStringLiteral("sceneKeypoints")] = static_cast<int>(sceneKp.size());
        m_params[QStringLiteral("matchNote")] = QString();

        if (objDesc.rows < 2 || sceneDesc.rows < 2) {
            fail(QStringLiteral("特征点不足（模板 %1 / 场景 %2），比值检验至少各需 2 条")
                     .arg(objDesc.rows).arg(sceneDesc.rows));
            return;
        }

        std::vector<std::vector<cv::DMatch>> knn;
        QString note;
        matchWithKnn(objDesc, sceneDesc, useFlann, &knn, &note);
        if (!note.isEmpty())
            m_params[QStringLiteral("matchNote")] = note;

        std::vector<cv::Point2f> objPts, scenePts;
        for (const auto &pair : knn) {
            if (pair.size() < 2 || pair[0].distance >= ratio * pair[1].distance)
                continue;
            const cv::KeyPoint &a = objKp[pair[0].queryIdx];
            const cv::KeyPoint &b = sceneKp[pair[0].trainIdx];
            objPts.push_back(a.pt);
            scenePts.push_back(b.pt);
        }
        m_params[QStringLiteral("candidateMatches")] = static_cast<int>(objPts.size());

        const int minMatches = m_params.value(QStringLiteral("minMatches"), 6).toInt();
        if (static_cast<int>(objPts.size()) < minMatches) {
            fail(QStringLiteral("比值检验后仅 %1 个点对（下限 %2）").arg(objPts.size()).arg(minMatches));
            return;
        }

        cv::Mat inlierMask;
        // 4.x 签名：(from, to, inliers, method, ransacReprojThreshold, ...) —— 掩膜在第 3 位
        const cv::Mat M = cv::estimateAffinePartial2D(objPts, scenePts, inlierMask,
                                                      cv::RANSAC, reproj);
        if (M.empty()) {
            fail(QStringLiteral("相似变换估计失败（点对退化或全部判为外点）"));
            return;
        }
        int inliers = 0;
        for (int i = 0; i < inlierMask.rows; ++i)
            inliers += (inlierMask.at<uchar>(i) != 0);
        m_params[QStringLiteral("inlierCount")] = inliers;

        const double m00 = M.at<double>(0, 0), m01 = M.at<double>(0, 1), m02 = M.at<double>(0, 2);
        const double m10 = M.at<double>(1, 0), m11 = M.at<double>(1, 1), m12 = M.at<double>(1, 2);
        const double scale = std::sqrt(m00 * m00 + m10 * m10);
        // M 把模板映到场景：场景若由 getRotationMatrix2D(+θ) 生成，则 m00=s·cosθ、m01=s·sinθ、
        // m10=−s·sinθ ⇒ θ = atan2(m01, m00)。取 m10 会差一个负号，而本节点的位姿要和
        // OpencvTemplateMatchNode 报同一个角（它回报的就是 getRotationMatrix2D 的输入角），
        // 否则 PositionCorrect 对两种定位节点会朝相反方向纠偏。
        const double angle = std::atan2(m01, m00) * 180.0 / CV_PI;
        const cv::Point2f c(tmpl.cols * 0.5f, tmpl.rows * 0.5f);
        const double col = m00 * c.x + m01 * c.y + m02;
        const double row = m10 * c.x + m11 * c.y + m12;

        const double score = inliers / double(std::max<size_t>(1, objPts.size()));
        const bool valid = inliers >= m_params.value(QStringLiteral("minInliers"), 5).toInt()
                           && score >= m_params.value(QStringLiteral("minScore"), 0.3).toDouble();

        m_params[QStringLiteral("matchRow")] = row;
        m_params[QStringLiteral("matchCol")] = col;
        m_params[QStringLiteral("matchScore")] = score;
        m_params[QStringLiteral("matchAngle")] = angle;
        m_params[QStringLiteral("matchScale")] = scale;
        m_params["moduleStatus"] = valid;

        const QString fixtureName =
            m_params.value(QStringLiteral("writeFixtureName")).toString().trimmed();
        if (!fixtureName.isEmpty() && valid) {
            if (FlowScene *fs = flowSceneRef())
                fs->setFixturePose(fixtureName, row, col, angle, scale);
        }
        m_outputImage = m_inputImage;
        VFP_DEBUG << kTag << (valid ? "定位成功" : "定位失败")
                  << QStringLiteral("候选=%1 内点=%2 得分=%3").arg(objPts.size()).arg(inliers).arg(score);

        MeasureResult res;
        res.type = QStringLiteral("feature");
        res.valueName = QStringLiteral("内点率");
        res.valid = valid;
        res.value = score;
        res.point1 = QPointF(col, row);
        res.extraValues = QVector<double>({row, col, score, angle, scale,
                                          tmpl.cols * scale, tmpl.rows * scale});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << kTag << "error:" << e.what();
        fail(QStringLiteral("异常: %1").arg(QString::fromLocal8Bit(e.what())));
    } catch (...) {
        fail(QStringLiteral("未知异常"));
    }
}

QWidget *OpencvFeatureMatchNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvFeatureMatchNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
