#include "OpencvCalibNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "CalibrationManager.h"
#include "AppLog.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <QWidget>

OpencvCalibNode::OpencvCalibNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("相机标定"));
    m_type = SHAPE_ANALYSIS;
}

OpencvCalibNode::~OpencvCalibNode() = default;

void OpencvCalibNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("标定状态"), PortDataType::String);
    addOutputPort(QStringLiteral("内参"), PortDataType::Matrix);
    registerParams({
        makeIntParam(QStringLiteral("patternW"), 9, 2, 50,
                     QStringLiteral("棋盘格内角点数（宽）")),
        makeIntParam(QStringLiteral("patternH"), 6, 2, 50,
                     QStringLiteral("棋盘格内角点数（高）")),
        makeDoubleParam(QStringLiteral("squareSize"), 10.0, 0.01, 1000.0,
                        QStringLiteral("方格边长"), QStringLiteral("mm")),
        makeIntParam(QStringLiteral("requiredFrames"), 10, 3, 100,
                     QStringLiteral("所需有效标定帧数")),
    });
    m_params[QStringLiteral("collectedFrames")] = 0;
    m_params[QStringLiteral("calibrated")] = false;
    m_params[QStringLiteral("reprojectionError")] = 0.0;
}

void OpencvCalibNode::run(bool /*autoSwitch*/)
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

        const int pw = m_params.value(QStringLiteral("patternW"), 9).toInt();
        const int ph = m_params.value(QStringLiteral("patternH"), 6).toInt();
        const double sq = m_params.value(QStringLiteral("squareSize"), 10.0).toDouble();
        const int required = m_params.value(QStringLiteral("requiredFrames"), 10).toInt();

        // 棋盘格角点检测：优先 SB 检测器（4.5.4+ 新算法，本环境已验证可绕过
        // 旧检测器在大图上的缺陷），失败时回退经典检测器
        std::vector<cv::Point2f> corners;
        bool found = cv::findChessboardCornersSB(gray, cv::Size(pw, ph), corners,
                                                 cv::CALIB_CB_NORMALIZE_IMAGE);
        if (!found) {
            found = cv::findChessboardCorners(gray, cv::Size(pw, ph), corners,
                                              cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
        }

        QString status;
        if (found) {
            m_imageSize = cv::Size(gray.cols, gray.rows);
            // 亚像素精化
            cv::Mat refined;
            gray.convertTo(refined, CV_8U);
            cv::cornerSubPix(refined, corners, cv::Size(5, 5), cv::Size(-1, -1),
                             cv::TermCriteria(cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 30, 0.01));
            m_cornerSets.append(corners);

            // 3D 棋盘格点（z=0 平面，单位 mm）
            std::vector<cv::Point3f> objPts;
            for (int r = 0; r < ph; ++r)
                for (int c = 0; c < pw; ++c)
                    objPts.emplace_back(c * sq, r * sq, 0.0f);
            m_objectPoints.append(objPts);

            const int collected = static_cast<int>(m_cornerSets.size());
            m_params[QStringLiteral("collectedFrames")] = collected;
            m_consecutiveFailures = 0;
            status = QStringLiteral("检测到棋盘格（%1/%2 帧）").arg(collected).arg(required);

            // 帧数足够 → 执行标定
            if (collected >= required) {
                QString detail;
                if (performCalibration(&detail)) {
                    status = QStringLiteral("标定完成。%1").arg(detail);
                    m_params[QStringLiteral("calibrated")] = true;
                    m_params["moduleStatus"] = true;
                } else {
                    status = QStringLiteral("标定失败: %1").arg(detail);
                    // 失败：清空已采集帧，允许重新采集
                    m_cornerSets.clear();
                    m_objectPoints.clear();
                    m_params[QStringLiteral("collectedFrames")] = 0;
                    m_params[QStringLiteral("calibrated")] = false;
                    m_params["moduleStatus"] = false;
                }
            } else {
                m_params["moduleStatus"] = true;
            }
        } else {
            ++m_consecutiveFailures;
            const int collected = static_cast<int>(m_cornerSets.size());
            QString hint;
            if (m_consecutiveFailures >= 10) {
                hint = QStringLiteral("；标定板检测持续失败，请检查：棋盘格内角数与参数一致、"
                                      "图像清晰、棋盘格未超出画面边界（当前 OpenCV 4.13 含 "
                                      "findChessboardCornersSB 鲁棒检测）");
            }
            status = QStringLiteral("未检测到棋盘格（已采集 %1/%2 帧）%3")
                         .arg(collected).arg(required).arg(hint);
            m_params[QStringLiteral("collectedFrames")] = collected;
            m_params["moduleStatus"] = false;
        }

        m_outputImage = m_inputImage;

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(status);
        setOutputData(1, strObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvCalibNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

bool OpencvCalibNode::performCalibration(QString *detail)
{
    if (m_cornerSets.size() < 3 || m_objectPoints.size() != m_cornerSets.size()) {
        if (detail) *detail = QStringLiteral("有效帧数不足（需 ≥3）");
        return false;
    }

    cv::Mat cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat distCoeffs = cv::Mat::zeros(8, 1, CV_64F);
    std::vector<cv::Mat> rvecs, tvecs;

    // 用最近检测帧的图像尺寸作为相机分辨率
    if (m_imageSize.width <= 0 || m_imageSize.height <= 0) {
        if (detail) *detail = QStringLiteral("缺少图像尺寸信息");
        return false;
    }
    std::vector<std::vector<cv::Point2f>> corners(m_cornerSets.begin(), m_cornerSets.end());
    std::vector<std::vector<cv::Point3f>> objPts(m_objectPoints.begin(), m_objectPoints.end());

    double rms = 0.0;
    try {
        rms = cv::calibrateCamera(objPts, corners, m_imageSize, cameraMatrix, distCoeffs,
                                  rvecs, tvecs);
    } catch (const cv::Exception &e) {
        if (detail) *detail = QStringLiteral("calibrateCamera: %1").arg(QString::fromLocal8Bit(e.what()));
        return false;
    }
    if (!std::isfinite(rms)) {
        if (detail) *detail = QStringLiteral("标定结果无效（重投影误差非有限值）");
        return false;
    }

    // 保存内参（fx, fy, cx, cy, k1, k2, p1, p2, 误差）
    const double fx = cameraMatrix.at<double>(0, 0);
    const double fy = cameraMatrix.at<double>(1, 1);
    const double cx = cameraMatrix.at<double>(0, 2);
    const double cy = cameraMatrix.at<double>(1, 2);
    const double k1 = distCoeffs.at<double>(0);
    const double k2 = distCoeffs.at<double>(1);
    const double p1 = distCoeffs.at<double>(2);
    const double p2 = distCoeffs.at<double>(3);

    QVector<double> params = {fx, fy, cx, cy, k1, k2, p1, p2, rms};
    CalibrationManager::instance()->setHomography(QStringLiteral("cam_params"), params);

    m_params[QStringLiteral("reprojectionError")] = rms;
    m_params[QStringLiteral("calibFx")] = fx;
    m_params[QStringLiteral("calibFy")] = fy;
    m_params[QStringLiteral("calibCx")] = cx;
    m_params[QStringLiteral("calibCy")] = cy;

    auto matObj = QSharedPointer<DataObject>::create();
    matObj->setType(DataObject::DataType::Matrix);
    matObj->setData(QVariant::fromValue(params));
    setOutputData(2, matObj);

    if (detail)
        *detail = QStringLiteral("重投影误差 %1 px，fx=%2 fy=%3 cx=%4 cy=%5")
                      .arg(rms, 0, 'f', 3)
                      .arg(fx, 0, 'f', 2)
                      .arg(fy, 0, 'f', 2)
                      .arg(cx, 0, 'f', 1)
                      .arg(cy, 0, 'f', 1);
    return true;
}

QWidget *OpencvCalibNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvCalibNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
