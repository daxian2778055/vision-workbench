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

/// U-28：sceneWhy／sizeWhy／mgrWhy／整句的四段判据，两个消费端共用这一份。
/// 文案与抽出前逐字相同（推进计划 §3.34 表 1 钉的就是"抽出前后现场读到的原因串一字不变"）。
QString CalibrationManager::affineLookupMissReason(const QString &what, const QString &tail,
                                                   const QString &fixtureName, bool hasScene,
                                                   bool fixtureNamed, bool fixtureHasHom, int homSize)
{
    QString sceneWhy;
    if (!fixtureHasHom) {
        if (!hasScene)
            sceneWhy = QStringLiteral("节点未挂到场景");
        else if (!fixtureNamed)
            sceneWhy = QStringLiteral("场景里没有该夹具");
        else
            sceneWhy = QStringLiteral("该夹具没有矩阵（只有位姿）");
    }
    QString sizeWhy;
    if (homSize == 9)
        sizeWhy = QStringLiteral("有 9 项（9 元内参载荷，不是 6 元仿射）");
    else if (homSize < 6)
        sizeWhy = QStringLiteral("只有 %1 项").arg(homSize);
    else
        // U-27：既不是 6 也不是 9 时不猜它是什么（HALCON 写侧长度本机未证到）。
        // 旧文案一律写"疑似 9 元内参"，7/8/10/20 项也照这句，现场会查错方向。
        sizeWhy = QStringLiteral("有 %1 项（不是 6 元仿射）").arg(homSize);
    QString mgrWhy;
    if (fixtureHasHom)
        mgrWhy = QStringLiteral("场景夹具的矩阵%1").arg(sizeWhy);
    else if (instance()->hasHomography(fixtureName))
        mgrWhy = QStringLiteral("标定单例里该键%1").arg(sizeWhy);
    else
        mgrWhy = QStringLiteral("标定单例里没有这个键");
    return sceneWhy.isEmpty()
        ? QStringLiteral("%1取不到 6 元矩阵：夹具 \"%2\"——%3，不回退手填 %4")
              .arg(what, fixtureName, mgrWhy, tail)
        : QStringLiteral("%1取不到 6 元矩阵：夹具 \"%2\"——%3；%4，不回退手填 %5")
              .arg(what, fixtureName, sceneWhy, mgrWhy, tail);
}
