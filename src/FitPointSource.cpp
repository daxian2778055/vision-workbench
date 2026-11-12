#include "FitPointSource.h"
#include "DataObject.h"

#include <halconcpp/HalconCpp.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <QRegularExpression>
#include <QStringList>

using namespace HalconCpp;

namespace {

/// 卡尺点集最少需要的数值个数（2 个点 = 4 个数）
constexpr int kMinCaliperValues = 4;

void setReason(QString *reason, const QString &text)
{
    if (reason)
        *reason = text;
}

/// 把一个 XLD 对象 tuple 拆成"每条轮廓一组"。多边形（XLD polygon）不是 contour，
/// get_contour_xld 会抛，按顶点取即可——两种对象对拟合都只是一批点。
void appendXldGroups(const HObject &objects, QVector<QVector<QPointF>> *groups)
{
    HTuple count;
    CountObj(objects, &count);
    const int objectCount = static_cast<int>(count.I());
    for (int i = 1; i <= objectCount; ++i) {
        HObject single;
        SelectObj(objects, &single, HTuple(i));

        HTuple rows, cols;
        bool extracted = true;
        try {
            HXLDCont contour(single);
            contour.GetContourXld(&rows, &cols);
        } catch (const HException &) {
            try {
                HXLDPoly polygon(single);
                HTuple lengths, phis;
                polygon.GetPolygonXld(&rows, &cols, &lengths, &phis);
            } catch (const HException &) {
                extracted = false;   // 既不是轮廓也不是多边形：跳过该对象
            }
        }
        if (!extracted)
            continue;

        QVector<QPointF> group;
        const int pointCount = static_cast<int>(rows.Length());
        group.reserve(pointCount);
        for (int k = 0; k < pointCount; ++k)
            group.append(QPointF(cols[k].D(), rows[k].D()));   // HALCON 是 (Row, Col)
        if (!group.isEmpty())
            groups->append(group);
    }
}

/// 卡尺点集约定：extraValues = [col0,row0,col1,row1,...]（见 OpencvCaliperNode 输出注释）
bool collectFromCaliper(const MeasureResult &result, QVector<QVector<QPointF>> *groups,
                        QString *reason)
{
    if (!result.valid) {
        setReason(reason, QStringLiteral("订阅的卡尺结果本轮无效（没有可用的边缘点）"));
        return false;
    }
    if (result.extraValues.size() < kMinCaliperValues || result.extraValues.size() % 2 != 0) {
        setReason(reason,
                  QStringLiteral("订阅的卡尺点集格式不符（extraValues 必须是成对的 col,row）"));
        return false;
    }

    QVector<QPointF> group;
    group.reserve(result.extraValues.size() / 2);
    for (int i = 0; i + 1 < result.extraValues.size(); i += 2)
        group.append(QPointF(result.extraValues.at(i), result.extraValues.at(i + 1)));
    if (group.isEmpty()) {
        setReason(reason, QStringLiteral("订阅的卡尺点集为空"));
        return false;
    }
    groups->append(group);
    return true;
}

/// 单个对象取点；主类型不是点集时下探一层具名子结果（B1 聚合端口：图像 + 边缘轮廓 + …）
bool collectFromObject(const QSharedPointer<DataObject> &data,
                       QVector<QVector<QPointF>> *groups, QString *reason, bool allowAttached)
{
    if (!data) {
        setReason(reason, QStringLiteral("订阅输入为空（上游本轮没有输出）"));
        return false;
    }

    const int before = groups->size();
    bool recognized = false;   // 主类型本身就是受支持的点集载体

    try {
        switch (data->getType()) {
        case DataObject::DataType::XLD: {
            recognized = true;
            const HObject objects = data->getHObject();
            if (objects.IsInitialized())
                appendXldGroups(objects, groups);
            break;
        }
        case DataObject::DataType::Region: {
            recognized = true;
            HObject contours;
            GenContourRegionXld(data->getHRegion(), &contours, "border");
            if (contours.IsInitialized())
                appendXldGroups(contours, groups);
            break;
        }
        case DataObject::DataType::Point: {
            recognized = true;
            const QPointF point = data->getPoint();
            if (std::isfinite(point.x()) && std::isfinite(point.y()))
                groups->append(QVector<QPointF>{point});
            break;
        }
        case DataObject::DataType::Measure: {
            const MeasureResult result = data->getMeasureResult();
            if (result.type == QStringLiteral("caliper")) {
                recognized = true;
                collectFromCaliper(result, groups, reason);
            }
            break;
        }
        default:
            break;
        }

        if (groups->size() > before)
            return true;

        if (allowAttached) {
            const auto attached = data->attachedResults();
            for (const auto &item : attached) {
                if (item.second && collectFromObject(item.second, groups, reason, false))
                    return true;
            }
        }
    } catch (const HException &e) {
        setReason(reason, QStringLiteral("订阅点集读取失败（HALCON：%1）")
                              .arg(QString::fromLocal8Bit(e.ErrorMessage().TextA())));
        return false;
    } catch (const std::exception &e) {
        setReason(reason, QStringLiteral("订阅点集读取失败（%1）").arg(QString::fromUtf8(e.what())));
        return false;
    }

    if (!recognized)
        setReason(reason, QStringLiteral("订阅数据类型不支持点集（需要 XLD / Region / Point / 卡尺边缘点）"));
    else if (!reason || reason->isEmpty())
        setReason(reason, QStringLiteral("订阅数据里没有可用的点（点集为空）"));
    return false;
}

} // namespace

QString FitPointSource::slotName()
{
    return QStringLiteral("点集");
}

QString FitPointSource::pointXSlotName()
{
    return QStringLiteral("点集X");
}

QString FitPointSource::pointYSlotName()
{
    return QStringLiteral("点集Y");
}

bool FitPointSource::collectNumberSeries(const QSharedPointer<DataObject> &data,
                                         QVector<double> *values, QString *reason)
{
    if (!values)
        return false;
    values->clear();
    if (reason)
        reason->clear();
    if (!data) {
        setReason(reason, QStringLiteral("坐标订阅输入为空（上游本轮没有输出）"));
        return false;
    }

    const DataObject::DataType type = data->getType();
    const QVariant var = data->getData();

    if (type == DataObject::DataType::Number) {
        if (!var.canConvert<double>()) {
            setReason(reason, QStringLiteral("坐标订阅的数值无效（当前值不是数字）"));
            return false;
        }
        values->append(var.toDouble());
        return true;
    }

    if (type == DataObject::DataType::Array) {
        if (var.canConvert<QVector<double>>()) {
            *values = var.value<QVector<double>>();
        } else if (var.canConvert<QList<double>>()) {
            *values = var.value<QList<double>>().toVector();
        } else if (var.canConvert<QStringList>()) {
            const QStringList parts = var.toStringList();
            for (const QString &part : parts) {
                bool ok = false;
                const double value = part.trimmed().toDouble(&ok);
                if (!ok) {
                    setReason(reason, QStringLiteral("坐标订阅文本里有无法解析的数值：%1").arg(part));
                    return false;
                }
                values->append(value);
            }
        } else {
            setReason(reason, QStringLiteral("坐标订阅的数组不是数值数组（当前类型：%1）")
                                  .arg(data->getTypeString()));
            return false;
        }
        if (values->isEmpty()) {
            setReason(reason, QStringLiteral("坐标订阅的数值数组为空"));
            return false;
        }
        return true;
    }

    if (type == DataObject::DataType::String) {
        const QStringList parts = var.toString().split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                       Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            bool ok = false;
            const double value = part.toDouble(&ok);
            if (!ok) {
                setReason(reason, QStringLiteral("坐标订阅文本里有无法解析的数值：%1").arg(part));
                return false;
            }
            values->append(value);
        }
        if (values->isEmpty()) {
            setReason(reason, QStringLiteral("坐标订阅的文本为空（没有可解析的数值）"));
            return false;
        }
        return true;
    }

    setReason(reason, QStringLiteral("坐标订阅数据类型不支持数值数组（当前类型：%1）")
                          .arg(data->getTypeString()));
    return false;
}

bool FitPointSource::collectSubscribedGroups(const SubscriptionInputs &inputs, int inputMode,
                                             QVector<QVector<QPointF>> *groups, QString *reason)
{
    if (!groups)
        return false;
    groups->clear();
    if (reason)
        reason->clear();

    if (inputMode == 0) {
        // 按点：只认「点集」槽；X/Y 仍有绑定说明配置冲突，明确报错而不是猜
        if (inputs.xBound || inputs.yBound) {
            setReason(reason, QStringLiteral("输入方式为按点，但点集X/点集Y仍有绑定；"
                                             "请清除坐标槽或把输入方式切换为按坐标"));
            return false;
        }
        if (!inputs.pointBound) {
            setReason(reason, QStringLiteral("按点订阅缺少「点集」绑定"));
            return false;
        }
        return collectGroups(inputs.point, groups, reason);
    }

    // 按坐标：两个数值序列必须同时存在且数量一致
    if (inputs.pointBound) {
        setReason(reason, QStringLiteral("输入方式为按坐标，但「点集」槽仍有绑定；"
                                         "请清除点集绑定或把输入方式切换为按点"));
        return false;
    }
    if (!inputs.xBound || !inputs.yBound) {
        setReason(reason, QStringLiteral("按坐标订阅需要同时绑定「点集X」与「点集Y」"));
        return false;
    }

    QVector<double> xs;
    QVector<double> ys;
    QString seriesReason;
    if (!collectNumberSeries(inputs.x, &xs, &seriesReason)) {
        setReason(reason, QStringLiteral("点集X：%1").arg(seriesReason));
        return false;
    }
    if (!collectNumberSeries(inputs.y, &ys, &seriesReason)) {
        setReason(reason, QStringLiteral("点集Y：%1").arg(seriesReason));
        return false;
    }
    if (xs.size() != ys.size()) {
        setReason(reason, QStringLiteral("点集X（%1 个）与点集Y（%2 个）数量不一致，无法配对成点")
                              .arg(xs.size())
                              .arg(ys.size()));
        return false;
    }

    QVector<QPointF> points;
    points.reserve(xs.size());
    for (int i = 0; i < xs.size(); ++i)
        points.append(QPointF(xs.at(i), ys.at(i)));
    if (points.isEmpty()) {
        setReason(reason, QStringLiteral("按坐标订阅结果为空（没有可配对的点）"));
        return false;
    }
    groups->append(points);
    return true;
}

bool FitPointSource::collectGroups(const QSharedPointer<DataObject> &data,
                                   QVector<QVector<QPointF>> *groups, QString *reason)
{
    if (!groups)
        return false;
    if (reason)
        reason->clear();
    return collectFromObject(data, groups, reason, true);
}

void FitPointSource::limitGroups(QVector<QVector<QPointF>> *groups, int maxGroups)
{
    if (!groups || maxGroups <= 0 || groups->size() <= maxGroups)
        return;
    std::sort(groups->begin(), groups->end(),
              [](const QVector<QPointF> &a, const QVector<QPointF> &b) {
                  return a.size() > b.size();
              });
    groups->resize(maxGroups);
}

QVector<QPointF> FitPointSource::mergeGroups(const QVector<QVector<QPointF>> &groups)
{
    int total = 0;
    for (const QVector<QPointF> &group : groups)
        total += group.size();

    QVector<QPointF> merged;
    merged.reserve(total);
    for (const QVector<QPointF> &group : groups)
        merged += group;
    return merged;
}

namespace {
/// 结果表逐点行的上限：超过此数只显示一行摘要（参数里仍保留完整逐点串）
constexpr int kMaxTablePointRows = 200;
} // namespace

int FitPointSource::maxTablePointRows()
{
    return kMaxTablePointRows;
}

QString FitPointSource::pointRowsHint(int inputCount)
{
    if (inputCount <= kMaxTablePointRows)
        return QString();
    return QStringLiteral("逐点结果共 %1 点，超过表格显示上限 %2；"
                          "完整逐点数据仍写入 fitPointDistances / fitPointMask / fitInputPoints")
        .arg(inputCount)
        .arg(kMaxTablePointRows);
}

QString FitPointSource::joinNumbers(const QVector<double> &values, int decimals)
{
    QStringList texts;
    texts.reserve(values.size());
    for (double value : values)
        texts << QString::number(value, 'f', decimals);
    return texts.join(QLatin1Char(','));
}

QString FitPointSource::joinMask(const QVector<bool> &mask)
{
    QStringList texts;
    texts.reserve(mask.size());
    for (bool used : mask)
        texts << (used ? QStringLiteral("1") : QStringLiteral("0"));
    return texts.join(QLatin1Char(','));
}

QString FitPointSource::joinPoints(const QVector<QPointF> &points, int decimals)
{
    QStringList texts;
    texts.reserve(points.size());
    for (const QPointF &point : points) {
        texts << QStringLiteral("%1,%2")
                     .arg(point.x(), 0, 'f', decimals)
                     .arg(point.y(), 0, 'f', decimals);
    }
    return texts.join(QLatin1Char(';'));
}
