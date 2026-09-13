#include "CalibrationManager.h"
#include <QJsonArray>
#include <QJsonObject>

CalibrationManager *CalibrationManager::instance()
{
    static CalibrationManager s_instance;
    return &s_instance;
}

void CalibrationManager::setHomography(const QString &name, const QVector<double> &hom)
{
    if (!name.isEmpty())
        m_homographies[name] = hom;
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
    if (hom.size() < 6)
        return QPointF(x, y);
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

void CalibrationManager::fromJson(const QJsonObject &json)
{
    m_homographies.clear();
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        QVector<double> hom;
        for (const QJsonValue &v : it.value().toArray())
            hom.append(v.toDouble());
        if (hom.size() >= 6)
            m_homographies[it.key()] = hom;
    }
}
