#include "CalibrationManager.h"
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>

CalibrationManager *CalibrationManager::instance()
{
    static CalibrationManager s_instance;
    return &s_instance;
}

bool CalibrationManager::setHomography(const QString &name, const QVector<double> &hom)
{
    if (name.isEmpty())
        return false;
    const auto it = m_homographies.constFind(name);
    if (it != m_homographies.constEnd() && it.value().size() != hom.size())
        return false;           // 项数不同＝另一种载荷，不许把已有的那一条整条顶掉（R-5）
    m_homographies[name] = hom;
    return true;
}

QVector<double> CalibrationManager::homography(const QString &name) const
{
    return m_homographies.value(name);
}

bool CalibrationManager::hasHomography(const QString &name) const
{
    return m_homographies.contains(name);
}

QStringList CalibrationManager::names() const
{
    return m_homographies.keys();
}

void CalibrationManager::remove(const QString &name)
{
    m_homographies.remove(name);
}

void CalibrationManager::clear()
{
    m_homographies.clear();
}

QPointF CalibrationManager::applyHomography(const QVector<double> &hom, double x, double y)
{
    if (hom.size() != 6)
        return QPointF(x, y);     // 过短取不到矩阵、过长是另一种载荷（9 元内参）——都不许当 6 元用
    return QPointF(hom[0] * x + hom[1] * y + hom[2],
                   hom[3] * x + hom[4] * y + hom[5]);
}

QJsonObject CalibrationManager::toJson() const
{
    QJsonObject o;
    for (auto it = m_homographies.constBegin(); it != m_homographies.constEnd(); ++it) {
        QJsonArray a;
        for (double d : it.value())
            a.append(d);
        o[it.key()] = a;
    }
    return o;
}

void CalibrationManager::fromJson(const QJsonObject &json, QStringList *rejected)
{
    m_homographies.clear();
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        const QJsonValue value = it.value();
        QVector<double> hom;
        QString why;
        if (it.key().isEmpty())
            why = QStringLiteral("键名为空");
        else if (!value.isArray())
            why = QStringLiteral("值不是数组");
        else {
            const QJsonArray arr = value.toArray();
            for (int i = 0; i < arr.size(); ++i) {
                const QJsonValue &v = arr.at(i);
                // 不查类型直接 toDouble()：字符串/null/bool 一律给 0.0，坏载荷会变成"整条全 0"，
                // 而不是"只有读不出来的那一项为 0"（NaN/Inf 序列化出来本来就是 null，同一族）。
                if (!v.isDouble()) {
                    why = QStringLiteral("第 %1 项不是数值").arg(i + 1);
                    break;
                }
                if (!std::isfinite(v.toDouble())) {
                    why = QStringLiteral("第 %1 项不是有限值").arg(i + 1);
                    break;
                }
                hom.append(v.toDouble());
            }
        }
        if (why.isEmpty() && hom.size() < 6)
            why = QStringLiteral("只有 %1 项（少于 6）").arg(hom.size());
        if (!why.isEmpty()) {
            if (rejected)
                rejected->append(QStringLiteral("%1 :: %2").arg(it.key(), why));
            continue;
        }
        m_homographies[it.key()] = hom;
    }
}
