#include "NPointCalibNode.h"
#include "DataObject.h"
#include "Port.h"
#include "CalibrationManager.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include <QRegularExpression>
#include <cmath>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>

/// 解析标定点对文本：每行 "像素X,像素Y 世界X,世界Y"
static bool parsePointPairs(const QString &text, QVector<QPointF> &pixels,
                            QVector<QPointF> &worlds, QString *rejectNote)
{
    pixels.clear();
    worlds.clear();
    if (rejectNote) rejectNote->clear();
    int lineNo = 0;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                         Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QString l = line.trimmed();
        if (l.isEmpty() || l.startsWith(QStringLiteral("#"))) continue;
        l.replace(QStringLiteral("->"), QStringLiteral(" "));
        const QStringList parts = l.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                          Qt::SkipEmptyParts);
        // 逐行点名拒收（2026-10-25 复核 P2-3）：不足 4 个数或含非有限值的行必须报告，
        // 不得静默 continue——坏行被 RANSAC 剔掉后矩阵照样"成功"，现场看不出点对配错了。
        if (parts.size() < 4) {
            if (rejectNote) {
                *rejectNote += QStringLiteral("第 %1 行：数字不足 4 个（需 像素X,像素Y 世界X,世界Y）；").arg(lineNo);
            }
            ++lineNo;
            continue;
        }
        bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
        const double px = parts[0].toDouble(&ok1);
        const double py = parts[1].toDouble(&ok2);
        const double wx = parts[2].toDouble(&ok3);
        const double wy = parts[3].toDouble(&ok4);
        const bool finite = std::isfinite(px) && std::isfinite(py)
                            && std::isfinite(wx) && std::isfinite(wy);
        if (ok1 && ok2 && ok3 && ok4 && finite) {
            pixels.append(QPointF(px, py));
            worlds.append(QPointF(wx, wy));
        } else {
            if (rejectNote) {
                *rejectNote += QStringLiteral("第 %1 行：数值无效或非有限（%2）；")
                                   .arg(lineNo)
                                   .arg(l.left(40));
            }
        }
        ++lineNo;
    }
    return pixels.size() >= 3;
}

NPointCalibNode::NPointCalibNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("N点标定"));
    m_type = SHAPE_ANALYSIS;
}

void NPointCalibNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("变换矩阵"), PortDataType::Matrix);
    addOutputPort(QStringLiteral("标定结果"), PortDataType::String);
    registerParams({
        makeStringParam(QStringLiteral("pointsText"), QStringLiteral(""),
                        QStringLiteral("点对（每行: 像素X,像素Y 世界X,世界Y）")),
        makeStringParam(QStringLiteral("saveName"), QStringLiteral("calib1"),
                        QStringLiteral("保存名称（供坐标系换算复用）")),
    });
    // 结果字段（不进参数面板）：判红时输出端口会被 process() 清空，原因只能留在这里
    m_params[QStringLiteral("calibNote")] = QString();
}

void NPointCalibNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const QString text = m_params.value(QStringLiteral("pointsText")).toString();
        const QString saveName = m_params.value(QStringLiteral("saveName"), QStringLiteral("calib1")).toString();

        QVector<QPointF> pixels, worlds;
        QString rejectNote;
        // 语义对齐手眼侧 R-1（2026-10-25 复核 P2-3）：**有坏行即判红**，不跳过坏行继续算——
        // 否则坏行被 RANSAC 静默剔除、矩阵照样"成功"，现场看不出点对配错了。
        parsePointPairs(text, pixels, worlds, &rejectNote);
        if (!rejectNote.isEmpty()) {
            const QString why = QStringLiteral("N点标定点对有坏行，拒绝标定（本节点不跳过坏行）：%1").arg(rejectNote);
            auto strObj = QSharedPointer<DataObject>::create();
            strObj->setType(DataObject::DataType::String);
            strObj->setData(why);
            setOutputData(1, QSharedPointer<DataObject>());
            setOutputData(2, strObj);
            m_params[QStringLiteral("calibNote")] = why;
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            return;
        }
        if (pixels.size() < 3) {
            auto strObj = QSharedPointer<DataObject>::create();
            strObj->setType(DataObject::DataType::String);
            strObj->setData(QStringLiteral("标定点对数不足（至少 3 对）"));
            setOutputData(2, strObj);
            m_params[QStringLiteral("calibNote")] = QStringLiteral("标定点对数不足（至少 3 对）");
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            return;
        }

        std::vector<cv::Point2f> from, to;
        from.reserve(size_t(pixels.size()));
        to.reserve(size_t(worlds.size()));
        for (int i = 0; i < pixels.size(); ++i) {
            from.emplace_back(float(pixels[i].x()), float(pixels[i].y()));
            to.emplace_back(float(worlds[i].x()), float(worlds[i].y()));
        }

        const cv::Mat H = cv::estimateAffine2D(from, to);
        // 非有限值必须判红（2026-10-25 复核 P2-2）：三点共线时 OpenCV 返回**非空** 2×3 且
        // 六个元素全是 -nan，原判据只查 empty/rows/cols，NaN 矩阵会一路存进标定管理器与
        // 夹具、模块状态判绿，下游只查 size()==6 照样放行。
        if (H.empty() || H.rows != 2 || H.cols != 3 || !cv::checkRange(H)) {
            auto strObj = QSharedPointer<DataObject>::create();
            strObj->setType(DataObject::DataType::String);
            const QString why = QStringLiteral("标定失败: 仿射估计无效或含非有限值（点对共线/退化）");
            strObj->setData(why);
            setOutputData(1, QSharedPointer<DataObject>());
            setOutputData(2, strObj);
            m_params[QStringLiteral("calibNote")] = why;
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            return;
        }

        QVector<double> homVec;
        homVec << H.at<double>(0, 0) << H.at<double>(0, 1) << H.at<double>(0, 2)
               << H.at<double>(1, 0) << H.at<double>(1, 1) << H.at<double>(1, 2);

        if (!saveName.isEmpty()) {
            if (!CalibrationManager::instance()->setHomography(saveName, homVec)) {
                // R-5：该键上已经住着另一种载荷（项数不同，例如 9 元 OpenCV 内参），单例拒绝写入。
                // 这时矩阵算出来了却没存下 ⇒ 必须判红，静默判绿就是"自己绿、下游拿不到"（§3.15 同形态）。
                const auto held = CalibrationManager::instance()->homography(saveName).size();
                const QString why = QStringLiteral("N点标定结果无法存入：键 \"%1\" 上已有 %2 项的另一种载荷，本次矩阵是 6 项")
                                        .arg(saveName).arg(held);
                setOutputData(1, QSharedPointer<DataObject>());
                auto strObj = QSharedPointer<DataObject>::create();
                strObj->setType(DataObject::DataType::String);
                strObj->setData(why);
                setOutputData(2, strObj);
                m_params[QStringLiteral("calibNote")] = why;
                m_params["moduleStatus"] = false;
                m_outputImage = m_inputImage;
                return;
            }
            if (FlowScene *fs = flowSceneRef())
                fs->setFixtureHomography(saveName, homVec);
        }

        auto matObj = QSharedPointer<DataObject>::create();
        matObj->setType(DataObject::DataType::Matrix);
        matObj->setData(QVariant::fromValue(homVec));
        setOutputData(1, matObj);

        QString desc = QStringLiteral("N点标定成功（%1 对点）\n矩阵: [%2]")
            .arg(pixels.size())
            .arg(QStringList(
                {QString::number(homVec[0], 'f', 4), QString::number(homVec[1], 'f', 4),
                 QString::number(homVec[2], 'f', 4), QString::number(homVec[3], 'f', 4),
                 QString::number(homVec[4], 'f', 4), QString::number(homVec[5], 'f', 4)})
                     .join(QStringLiteral(", ")));
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(desc);
        setOutputData(2, strObj);

        m_params[QStringLiteral("calibNote")] = QString();
        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;
    } catch (const cv::Exception &e) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("标定失败: %1").arg(QString::fromUtf8(e.what())));
        setOutputData(2, strObj);
        m_params[QStringLiteral("calibNote")] = QStringLiteral("标定失败(cv::Exception): %1")
                                                     .arg(QString::fromUtf8(e.what()));
        m_params["moduleStatus"] = false;
    } catch (const std::exception &e) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("标定失败: %1").arg(QString::fromUtf8(e.what())));
        setOutputData(2, strObj);
        m_params[QStringLiteral("calibNote")] = QStringLiteral("标定失败(std::exception): %1")
                                                     .arg(QString::fromUtf8(e.what()));
        m_params["moduleStatus"] = false;
    }
}
