#include "OpencvCalibNode.h"
#include "NodeResultFields.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "CalibrationManager.h"
#include "AppLog.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
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
        makeEnumParam(QStringLiteral("detectMethod"), 0,
                      {QStringLiteral("自动（SB 优先）"),
                       QStringLiteral("经典角点检测"),
                       QStringLiteral("仅 SB 检测器")},
                      QStringLiteral("角点检测方式")),
        makeIntParam(QStringLiteral("subpixWindow"), 5, 1, 21,
                     QStringLiteral("亚像素细化窗口半径"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("maxReprojError"), 0.0, 0.0, 100.0,
                        QStringLiteral("最大允许重投影误差（0=不校验）"), QStringLiteral("px")),
        makeBoolParam(QStringLiteral("fixPrincipalPoint"), false,
                      QStringLiteral("固定主点（只标定焦距与畸变）")),
        makeBoolParam(QStringLiteral("zeroTangentDist"), false,
                      QStringLiteral("切向畸变置零")),
        makeBoolParam(QStringLiteral("fixK3"), false,
                      QStringLiteral("固定 K3（仅估 K1/K2/P1/P2）")),
    });
    m_params[QStringLiteral("collectedFrames")] = 0;
    m_params[QStringLiteral("calibrated")] = false;
    m_params[QStringLiteral("reprojectionError")] = 0.0;
    m_params[QStringLiteral("framesUsed")] = 0;
    m_params[QStringLiteral("calibImageWidth")] = 0;
    m_params[QStringLiteral("calibImageHeight")] = 0;
    m_params[QStringLiteral("calibK1")] = 0.0;
    m_params[QStringLiteral("calibK2")] = 0.0;
    m_params[QStringLiteral("calibK3")] = 0.0;
    m_params[QStringLiteral("calibP1")] = 0.0;
    m_params[QStringLiteral("calibP2")] = 0.0;
    // 结果字段（不进参数面板）：判红时输出端口会被 process() 清空，原因只能留在这里
    m_params[QStringLiteral("calibNote")] = QString();
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
        // 旧检测器在大图上的缺陷），失败时回退经典检测器（可由参数强制指定）
        const int detectMethod = m_params.value(QStringLiteral("detectMethod"), 0).toInt();
        const int subpixWindow = m_params.value(QStringLiteral("subpixWindow"), 5).toInt();
        std::vector<cv::Point2f> corners;
        bool found = false;
        if (detectMethod != 1) {
            found = cv::findChessboardCornersSB(gray, cv::Size(pw, ph), corners,
                                                cv::CALIB_CB_NORMALIZE_IMAGE);
        }
        if (!found && detectMethod != 2) {
            found = cv::findChessboardCorners(gray, cv::Size(pw, ph), corners,
                                              cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
        }

        QString status;
        if (found) {
            m_imageSize = cv::Size(gray.cols, gray.rows);
            // 亚像素精化
            cv::Mat refined;
            gray.convertTo(refined, CV_8U);
            const int halfWin = qMax(1, subpixWindow);
            cv::cornerSubPix(refined, corners, cv::Size(halfWin, halfWin), cv::Size(-1, -1),
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
                    // 成功后清采集帧（2026-10-25 复核 P3）：原来只在失败分支清，
                    // 每来一帧继续追加并重新 calibrateCamera，旧场景点会污染新标定。
                    m_cornerSets.clear();
                    // 两个容器必须一起清（2026-10-26 复核）：只清 m_cornerSets 会让下一轮
                    // 攒满帧后 m_objectPoints.size() != m_cornerSets.size() 恒成立 ⇒ 必判失败。
                    // 注意**不要**在这里复位 collectedFrames：那是"当批采集数"的展示值，
                    // 既有用例按成功后仍显示当批帧数断言（复位会误伤，也不符合现场读数习惯）。
                    m_objectPoints.clear();
                    m_params["moduleStatus"] = true;
                    m_params[QStringLiteral("calibNote")] = QString();
                } else {
                    status = QStringLiteral("标定失败: %1").arg(detail);
                    // 判红会清空全部输出端口，原因同步留进 calibNote 供现场查（R-5）
                    m_params[QStringLiteral("calibNote")] = status;
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
        m_params[QStringLiteral("lastError")] =
            QString::fromUtf8(e.what());   // 原因到面板（2026-10-26）
        VFP_DEBUG << "OpencvCalibNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
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

    int flags = 0;
    if (m_params.value(QStringLiteral("fixPrincipalPoint"), false).toBool())
        flags |= cv::CALIB_FIX_PRINCIPAL_POINT;
    if (m_params.value(QStringLiteral("zeroTangentDist"), false).toBool())
        flags |= cv::CALIB_ZERO_TANGENT_DIST;
    if (m_params.value(QStringLiteral("fixK3"), false).toBool())
        flags |= cv::CALIB_FIX_K3;

    double rms = 0.0;
    try {
        rms = cv::calibrateCamera(objPts, corners, m_imageSize, cameraMatrix, distCoeffs,
                                  rvecs, tvecs, flags);
    } catch (const cv::Exception &e) {
        if (detail) *detail = QStringLiteral("calibrateCamera: %1").arg(QString::fromLocal8Bit(e.what()));
        return false;
    }
    if (!std::isfinite(rms)) {
        if (detail) *detail = QStringLiteral("标定结果无效（重投影误差非有限值）");
        return false;
    }

    m_params[QStringLiteral("framesUsed")] = static_cast<int>(m_cornerSets.size());
    m_params[QStringLiteral("calibImageWidth")] = m_imageSize.width;
    m_params[QStringLiteral("calibImageHeight")] = m_imageSize.height;

    // 重投影误差超阈值 ⇒ 判红且不写入单例（避免下游拿到坏内参）
    const double maxReproj = m_params.value(QStringLiteral("maxReprojError"), 0.0).toDouble();
    if (maxReproj > 0.0 && rms > maxReproj) {
        if (detail) {
            *detail = QStringLiteral("重投影误差 %1 px 超过阈值 %2 px（%3 帧）")
                          .arg(rms, 0, 'f', 3)
                          .arg(maxReproj, 0, 'f', 3)
                          .arg(m_cornerSets.size());
        }
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
    const double k3 = distCoeffs.at<double>(4);

    QVector<double> params = {fx, fy, cx, cy, k1, k2, p1, p2, rms};
    if (!CalibrationManager::instance()->setHomography(QStringLiteral("cam_params"), params)) {
        // R-5：键 cam_params 上已有另一种项数的载荷（例如操作员把 N 点/手眼的 6 元矩阵存成了
        // "cam_params"）⇒ 单例拒绝整条顶掉内参。此时不得继续报"标定成功"。
        const auto held = CalibrationManager::instance()->homography(QStringLiteral("cam_params")).size();
        if (detail)
            *detail = QStringLiteral("标定结果无法存入：键 cam_params 上已有 %1 项的另一种载荷，本次内参是 9 项")
                          .arg(held);
        return false;
    }

    m_params[QStringLiteral("reprojectionError")] = rms;
    m_params[QStringLiteral("calibFx")] = fx;
    m_params[QStringLiteral("calibFy")] = fy;
    m_params[QStringLiteral("calibCx")] = cx;
    m_params[QStringLiteral("calibCy")] = cy;
    m_params[QStringLiteral("calibK1")] = k1;
    m_params[QStringLiteral("calibK2")] = k2;
    m_params[QStringLiteral("calibK3")] = k3;
    m_params[QStringLiteral("calibP1")] = p1;
    m_params[QStringLiteral("calibP2")] = p2;

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

QVector<NodeResultField> OpencvCalibNode::resultDisplayFields() const
{
    // VM「模块结果」口径：模块级 耗时/模块状态 + 标定状态/重投影误差/帧数/内参与畸变系数。
    // 未完成标定时 calibFx 等键不存在，面板自动跳过，不补空行。
    QVector<NodeResultField> fields = HalconNode::resultDisplayFields();
    fields << resultValue(QStringLiteral("calibrated"), QStringLiteral("已标定"))
           << resultValue(QStringLiteral("reprojectionError"), QStringLiteral("重投影误差"),
                          QStringLiteral("px"), 4)
           << resultValue(QStringLiteral("framesUsed"), QStringLiteral("标定帧数"),
                          QString(), 0)
           << resultValue(QStringLiteral("calibFx"), QStringLiteral("焦距X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("calibFy"), QStringLiteral("焦距Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("calibCx"), QStringLiteral("主点X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("calibCy"), QStringLiteral("主点Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("calibK1"), QStringLiteral("径向畸变K1"),
                          QString(), 6)
           << resultValue(QStringLiteral("calibK2"), QStringLiteral("径向畸变K2"),
                          QString(), 6)
           << resultValue(QStringLiteral("calibK3"), QStringLiteral("径向畸变K3"),
                          QString(), 6)
           << resultValue(QStringLiteral("calibP1"), QStringLiteral("切向畸变P1"),
                          QString(), 6)
           << resultValue(QStringLiteral("calibP2"), QStringLiteral("切向畸变P2"),
                          QString(), 6);
    return fields;
}
