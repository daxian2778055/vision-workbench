#include "NPointCalibNode.h"
#include "DataObject.h"
#include "Port.h"
#include "CalibrationManager.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include <QRegularExpression>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>

/// 解析标定点对文本：每行 "像素X,像素Y 世界X,世界Y"
static bool parsePointPairs(const QString &text, QVector<QPointF> &pixels,
                            QVector<QPointF> &worlds)
{
    pixels.clear();
    worlds.clear();
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                         Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QString l = line.trimmed();
        if (l.isEmpty() || l.startsWith(QStringLiteral("#"))) continue;
        l.replace(QStringLiteral("->"), QStringLiteral(" "));
        const QStringList parts = l.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                          Qt::SkipEmptyParts);
        if (parts.size() < 4) continue;
        bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
        const double px = parts[0].toDouble(&ok1);
        const double py = parts[1].toDouble(&ok2);
        const double wx = parts[2].toDouble(&ok3);
        const double wy = parts[3].toDouble(&ok4);
        if (ok1 && ok2 && ok3 && ok4) {
            pixels.append(QPointF(px, py));
            worlds.append(QPointF(wx, wy));
        }
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
}

void NPointCalibNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const QString text = m_params.value(QStringLiteral("pointsText")).toString();
        const QString saveName = m_params.value(QStringLiteral("saveName"), QStringLiteral("calib1")).toString();

        QVector<QPointF> pixels, worlds;
        if (!parsePointPairs(text, pixels, worlds)) {
            auto strObj = QSharedPointer<DataObject>::create();
            strObj->setType(DataObject::DataType::String);
            strObj->setData(QStringLiteral("标定点对数不足（至少 3 对）"));
            setOutputData(2, strObj);
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
        if (H.empty() || H.rows != 2 || H.cols != 3) {
            auto strObj = QSharedPointer<DataObject>::create();
            strObj->setType(DataObject::DataType::String);
            strObj->setData(QStringLiteral("标定失败: 仿射估计无效"));
            setOutputData(1, QSharedPointer<DataObject>());
            setOutputData(2, strObj);
            m_outputImage = m_inputImage;
            return;
        }

        QVector<double> homVec;
        homVec << H.at<double>(0, 0) << H.at<double>(0, 1) << H.at<double>(0, 2)
               << H.at<double>(1, 0) << H.at<double>(1, 1) << H.at<double>(1, 2);

        if (!saveName.isEmpty()) {
            CalibrationManager::instance()->setHomography(saveName, homVec);
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

        m_outputImage = m_inputImage;
    } catch (const cv::Exception &e) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("标定失败: %1").arg(QString::fromUtf8(e.what())));
        setOutputData(2, strObj);
    } catch (const std::exception &e) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("标定失败: %1").arg(QString::fromUtf8(e.what())));
        setOutputData(2, strObj);
    }
}
