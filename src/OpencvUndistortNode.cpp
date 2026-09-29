#include "OpencvUndistortNode.h"
#include "CalibrationManager.h"
#include "DataObject.h"
#include "Port.h"
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <cmath>
#include <vector>

namespace {

/// 9 元组读侧口径：写侧见 OpencvCalibNode.cpp 的 {fx, fy, cx, cy, k1, k2, p1, p2, rms}。
/// 项数不足或含非有限值都不能"凑合用"——拿半份内参去畸变更糟于不校正，
/// 且这里绝不回退到手填默认值（静默回退正是推进计划 §3.3 记的 R-2 那种缺陷形态）。
/// ⚠️ fx/fy 的正性**不在这里查**：两条通路共用 run() 里那一道（U-29），别再抄第二份。
bool readStoredIntrinsics(const QString &key, QVector<double> *out, QString *why)
{
    const QVector<double> v = CalibrationManager::instance()->homography(key);
    if (v.isEmpty()) {
        if (why)
            *why = QStringLiteral("标定键 \"%1\" 不存在或没有内参（不回退手填参数）").arg(key);
        return false;
    }
    if (v.size() != 9) {
        if (why)
            *why = v.size() < 9
                       ? QStringLiteral("标定键 \"%1\" 只有 %2 项，内参需要 9 元（fx fy cx cy k1 k2 p1 p2 rms）")
                             .arg(key).arg(v.size())
                       : QStringLiteral("标定键 \"%1\" 有 %2 项，不是 9 元内参载荷（fx fy cx cy k1 k2 p1 p2 rms）")
                             .arg(key).arg(v.size());
        return false;
    }
    for (int i = 0; i < 9; ++i) {
        if (!std::isfinite(v[i])) {
            if (why)
                *why = QStringLiteral("标定键 \"%1\" 的第 %2 项不是有限值").arg(key).arg(i);
            return false;
        }
    }
    if (out)
        *out = v;
    return true;
}

bool allFinite(const std::vector<double> &vs)
{
    for (double d : vs)
        if (!std::isfinite(d))
            return false;
    return true;
}

} // namespace

OpencvUndistortNode::OpencvUndistortNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("畸变校正"));
    m_type = IMAGE_PROCESSING;
}

void OpencvUndistortNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("校正结果"), PortDataType::String);
    registerParams({
        makeEnumParam(QStringLiteral("intrinsicSource"), 0,
                      {QStringLiteral("标定单例（按键读取）"), QStringLiteral("手填内参")},
                      QStringLiteral("内参来源")),
        makeStringParam(QStringLiteral("calibKey"), QStringLiteral("cam_params"),
                        QStringLiteral("标定单例键名")),
        makeDoubleParam(QStringLiteral("fx"), 0.0, 0.0, 100000.0,
                        QStringLiteral("fx（手填：0=未填）"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("fy"), 0.0, 0.0, 100000.0,
                        QStringLiteral("fy（手填：0=未填）"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("cx"), 0.0, -100000.0, 100000.0,
                        QStringLiteral("主点 cx"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("cy"), 0.0, -100000.0, 100000.0,
                        QStringLiteral("主点 cy"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("k1"), 0.0, -10.0, 10.0, QStringLiteral("径向 k1")),
        makeDoubleParam(QStringLiteral("k2"), 0.0, -10.0, 10.0, QStringLiteral("径向 k2")),
        makeDoubleParam(QStringLiteral("p1"), 0.0, -1.0, 1.0, QStringLiteral("切向 p1")),
        makeDoubleParam(QStringLiteral("p2"), 0.0, -1.0, 1.0, QStringLiteral("切向 p2")),
    });
    // 结果字段（不进参数面板）：判红时输出端口会被 process() 清空，原因只能留在这里
    m_params[QStringLiteral("calibNote")] = QString();
}

void OpencvUndistortNode::run(bool)
{
    // 失败原因只能留在 m_params：process() 判红时会清空 m_outputImage 与全部输出端口
    // （P0-1 / S-1 契约），现场只能从这一栏分辨"为什么没校正"。
    const auto fail = [this](const QString &why) {
        m_params[QStringLiteral("calibNote")] = why;
        m_params["moduleStatus"] = false;
    };

    try {
        double fx = 0, fy = 0, cx = 0, cy = 0, k1 = 0, k2 = 0, p1 = 0, p2 = 0;
        QString origin;

        if (m_params.value(QStringLiteral("intrinsicSource"), 0).toInt() == 0) {
            const QString key =
                m_params.value(QStringLiteral("calibKey"), QStringLiteral("cam_params"))
                    .toString().trimmed();
            if (key.isEmpty()) {
                fail(QStringLiteral("标定键名为空"));
                return;
            }
            QVector<double> v;
            QString why;
            if (!readStoredIntrinsics(key, &v, &why)) {
                fail(why);
                return;
            }
            fx = v[0]; fy = v[1]; cx = v[2]; cy = v[3];
            k1 = v[4]; k2 = v[5]; p1 = v[6]; p2 = v[7];
            origin = QStringLiteral("标定键 %1").arg(key);
        } else {
            auto pv = [this](const char *n) {
                return m_params.value(QString::fromLatin1(n), 0.0).toDouble();
            };
            fx = pv("fx"); fy = pv("fy"); cx = pv("cx"); cy = pv("cy");
            k1 = pv("k1"); k2 = pv("k2"); p1 = pv("p1"); p2 = pv("p2");
            if (!allFinite({fx, fy, cx, cy, k1, k2, p1, p2})) {
                fail(QStringLiteral("手填内参含非有限值"));
                return;
            }
            origin = QStringLiteral("手填内参");
        }

        // 两条通路**共用**这一道正性闸（U-29：改前只有手填侧有，键侧没有）。
        // fx/fy<=0 时 OpenCV 不报错、图也不颠倒（负号在归一化与再投影里抵消），
        // 于是"全黑产出"或"看着正常但用错了内参"都配着绿灯（推进计划 §3.32 表 1 的⑤c、§3.33 键侧实测）。
        if (fx <= 0.0 || fy <= 0.0) {
            fail(QStringLiteral("%1：fx/fy 必须为正（当前 fx=%2 fy=%3）")
                     .arg(origin).arg(fx, 0, 'f', 2).arg(fy, 0, 'f', 2));
            return;
        }

        const cv::Mat src = OpencvUtil::himageToMat(m_inputImage);
        if (src.empty()) {
            fail(QStringLiteral("输入图像为空矩阵"));
            return;
        }

        const cv::Mat K = (cv::Mat_<double>(3, 3) << fx, 0, cx, 0, fy, cy, 0, 0, 1);
        const cv::Mat D = (cv::Mat_<double>(8, 1) << k1, k2, p1, p2, 0, 0, 0, 0);
        cv::Mat map1, map2, dst;
        // newCameraMatrix 取 K ⇒ 主点/焦距不变，只把畸变拉直（不改变视场，便于与标定前的图对位）
        cv::initUndistortRectifyMap(K, D, cv::Mat(), K, src.size(), CV_32FC1, map1, map2);
        cv::remap(src, dst, map1, map2, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));

        const HImage out = OpencvUtil::matToHimage(dst);
        if (!out.IsInitialized()) {
            fail(QStringLiteral("去畸变结果无法转为图像"));
            return;
        }
        m_outputImage = (HObject)out;

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("畸变校正完成（来源: %1，fx=%2 fy=%3 cx=%4 cy=%5 k1=%6 k2=%7 p1=%8 p2=%9）")
                            .arg(origin)
                            .arg(fx, 0, 'f', 2).arg(fy, 0, 'f', 2)
                            .arg(cx, 0, 'f', 1).arg(cy, 0, 'f', 1)
                            .arg(k1, 0, 'f', 4).arg(k2, 0, 'f', 4)
                            .arg(p1, 0, 'f', 4).arg(p2, 0, 'f', 4));
        setOutputData(1, strObj);

        m_params[QStringLiteral("calibNote")] = QString();
        m_params["moduleStatus"] = true;
    } catch (const cv::Exception &e) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        m_params[QStringLiteral("calibNote")] = QStringLiteral("畸变校正失败(cv::Exception): %1")
                                                     .arg(QString::fromUtf8(e.what()));
        m_params["moduleStatus"] = false;
    } catch (const HException &e) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        m_params[QStringLiteral("calibNote")] = QStringLiteral("畸变校正失败(HException): %1")
                                                     .arg(QString::fromUtf8(e.ErrorMessage().Text()));
        m_params["moduleStatus"] = false;
    } catch (const std::exception &e) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        m_params[QStringLiteral("calibNote")] = QStringLiteral("畸变校正失败(std::exception): %1")
                                                     .arg(QString::fromUtf8(e.what()));
        m_params["moduleStatus"] = false;
    }
}
