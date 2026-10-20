#include "FixtureValidation.h"

#include <QJsonArray>
#include <QJsonValue>
#include <cmath>

// 夹具校验面（词条切片 B 图元族第二抽，从场景图抽出）：
// 载入侧校验与降级与写入侧两道闸的唯一口径；原因文案与降级语义逐句复刻原体。

bool FixtureValidation::loadFixture(const QString &rawName, const QJsonObject &entry,
                                    FlowFixture *out, QStringList *reasons)
{
    // U-31：载入侧与写侧同一套形，不合形的部分**降级**（而不是整条丢弃：位姿坏了一个键不该
    // 连累矩阵），并把降级原因报出去（静默丢弃的表现是"方案打开后夹具的矩阵不见了"）。
    const QString name = rawName.trimmed();
    if (name.isEmpty()) {
        if (reasons)
            reasons->append(QStringLiteral("%1 :: 夹具名为空，整条跳过（写侧 setFixture 同样拒收）")
                                 .arg(name));
        return false;
    }

    FlowFixture ff;
    ff.name = name;

    if (entry.value(QStringLiteral("hasHom")).toBool()) {
        QString why;
        QVector<double> hom;
        const QJsonArray arr = entry.value(QStringLiteral("hom")).toArray();
        if (!isHomographyCountAccepted(arr.size()))
            why = QStringLiteral("矩阵有 %1 项（夹具矩阵槽位只接受恰好 6 项）").arg(arr.size());
        else {
            for (int i = 0; i < arr.size(); ++i) {
                const QJsonValue &v = arr.at(i);
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
        if (why.isEmpty()) {
            ff.hom = hom;
            ff.hasHom = true;
        } else if (reasons) {
            reasons->append(QStringLiteral("%1 :: 矩阵作废（%2），夹具保持没有矩阵").arg(name, why));
        }
    }

    if (entry.value(QStringLiteral("hasPose")).toBool()) {
        QString why;
        // poseScale 保留「缺键回落 1.0」的既有默认（extrasToJson 一直写全四键，回落只为兼容手写方案）；
        // 给了就必须是数值——否则和矩阵侧一样会读成 0.0，把位姿缩放悄悄变成"没有缩放"。
        const char *keys[] = {"poseRow", "poseCol", "poseAngle", "poseScale"};
        bool required[] = {true, true, true, false};
        for (int i = 0; i < 4 && why.isEmpty(); ++i) {
            const QString key = QString::fromLatin1(keys[i]);
            const QJsonValue v = entry.value(key);
            if (!entry.contains(key)) {
                if (required[i])
                    why = QStringLiteral("位姿缺 %1").arg(key);
                continue;
            }
            if (!v.isDouble())
                why = QStringLiteral("位姿 %1 不是数值").arg(key);
            else if (!std::isfinite(v.toDouble()))
                why = QStringLiteral("位姿 %1 不是有限值").arg(key);
        }
        if (why.isEmpty()) {
            ff.poseRow = entry.value(QStringLiteral("poseRow")).toDouble();
            ff.poseCol = entry.value(QStringLiteral("poseCol")).toDouble();
            ff.poseAngle = entry.value(QStringLiteral("poseAngle")).toDouble();
            ff.poseScale = entry.value(QStringLiteral("poseScale")).toDouble(1.0);
            ff.hasPose = true;
        } else if (reasons) {
            reasons->append(QStringLiteral("%1 :: 位姿作废（%2），夹具保持没有位姿").arg(name, why));
        }
    }

    if (out)
        *out = ff;
    return true;
}

bool FixtureValidation::isNameAccepted(const QString &rawName)
{
    return !rawName.trimmed().isEmpty();
}

bool FixtureValidation::isHomographyCountAccepted(int count)
{
    return count == 6;
}
