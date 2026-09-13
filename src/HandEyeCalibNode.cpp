#include "HandEyeCalibNode.h"
#include "DataObject.h"
#include "Port.h"
#include "CalibrationManager.h"
#include <QRegularExpression>
#include <cmath>

/// 解析标定点对文本：每行 "像素X,像素Y 机器人X,机器人Y"
static bool parseHandEyePairs(const QString &text, QVector<QPointF> &pixels,
                              QVector<QPointF> &robots)
{
    pixels.clear();
    robots.clear();
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
        const double rx = parts[2].toDouble(&ok3);
        const double ry = parts[3].toDouble(&ok4);
        if (ok1 && ok2 && ok3 && ok4) {
            pixels.append(QPointF(px, py));
            robots.append(QPointF(rx, ry));
        }
    }
    return pixels.size() >= 2;
}

/// 2D 刚体（旋转+平移，无缩放），对应 HALCON VectorToRigid
static QVector<double> estimateRigid2d(const QVector<QPointF> &from, const QVector<QPointF> &to)
{
    const int n = from.size();
    QPointF cf(0, 0), ct(0, 0);
    for (int i = 0; i < n; ++i) {
        cf += from[i];
        ct += to[i];
    }
    cf /= n;
    ct /= n;

    double sxx = 0, sxy = 0, syx = 0, syy = 0;
    for (int i = 0; i < n; ++i) {
        const QPointF a = from[i] - cf;
        const QPointF b = to[i] - ct;
        sxx += a.x() * b.x();
        sxy += a.x() * b.y();
        syx += a.y() * b.x();
        syy += a.y() * b.y();
    }

    const double angle = std::atan2(sxy - syx, sxx + syy);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double tx = ct.x() - (c * cf.x() - s * cf.y());
    const double ty = ct.y() - (s * cf.x() + c * cf.y());
    return {c, -s, tx, s, c, ty};
}

HandEyeCalibNode::HandEyeCalibNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("手眼标定"));
    m_type = SHAPE_ANALYSIS;
}

void HandEyeCalibNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("变换矩阵"), PortDataType::Matrix);
    addOutputPort(QStringLiteral("标定结果"), PortDataType::String);
    registerParams({
        makeStringParam(QStringLiteral("pointsText"), QStringLiteral(""),
                        QStringLiteral("点对（每行: 像素X,像素Y 机器人X,机器人Y）")),
        makeStringParam(QStringLiteral("saveName"), QStringLiteral("handeye1"),
                        QStringLiteral("保存名称（供坐标系换算复用）")),
    });
}

void HandEyeCalibNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const QString text = m_params.value(QStringLiteral("pointsText")).toString();
        const QString saveName = m_params.value(QStringLiteral("saveName"), QStringLiteral("handeye1")).toString();

        QVector<QPointF> pixels, robots;
        if (!parseHandEyePairs(text, pixels, robots)) {
            auto strObj = QSharedPointer<DataObject>::create();
            strObj->setType(DataObject::DataType::String);
            strObj->setData(QStringLiteral("标定点对数不足（至少 2 对）"));
            setOutputData(2, strObj);
            m_outputImage = m_inputImage;
            return;
        }

        const QVector<double> homVec = estimateRigid2d(pixels, robots);

        if (!saveName.isEmpty())
            CalibrationManager::instance()->setHomography(saveName, homVec);

        auto matObj = QSharedPointer<DataObject>::create();
        matObj->setType(DataObject::DataType::Matrix);
        matObj->setData(QVariant::fromValue(homVec));
        setOutputData(1, matObj);

        QString desc = QStringLiteral("手眼标定成功（%1 对点）\n刚体变换矩阵: [%2]")
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
    } catch (const std::exception &e) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("标定失败: %1").arg(QString::fromUtf8(e.what())));
        setOutputData(2, strObj);
    }
}
