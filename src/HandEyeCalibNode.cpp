#include "HandEyeCalibNode.h"
#include "DataObject.h"
#include "Port.h"
#include "CalibrationManager.h"
#include <QRegularExpression>
#include <cmath>

/// 解析标定点对文本：每行 "像素X,像素Y 机器人X,机器人Y"
/// ⚠️ R-1：改前两处 `continue`（`parts.size() < 4` 与 `ok1..ok4` 有一个 false）把坏行**静默丢掉**，
/// 剩下的行仍够 2 对就照样判绿（实测：掺一行 "150,150 15" 后报「3 对点」且全绿，操作员数不出少了几行）；
/// 而 `nan`/`inf` 会被 toDouble 正常收下来当坐标（实测：报回的 6 项矩阵含 NaN 并原样存进单例）。
/// 现在两类坏行各自逐行点名，交给 run() 判红——本节点不接受"被跳过的行"。
struct HandEyePairs
{
    QVector<QPointF> pixels;
    QVector<QPointF> robots;
    QStringList unparseable;   // 行号 + 原文：解析不出 4 个可当坐标的数
    QStringList nonFinite;     // 行号 + 原文：解析成了数但不是有限值
};

static QString quoteLine(const QString &l)
{
    return l.size() <= 48 ? l : l.left(45) + QStringLiteral("…");
}

static HandEyePairs parseHandEyePairs(const QString &text)
{
    HandEyePairs out;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                         Qt::SkipEmptyParts);
    int lineNo = 0;
    for (const QString &line : lines) {
        ++lineNo;
        QString l = line.trimmed();
        if (l.isEmpty() || l.startsWith(QStringLiteral("#"))) continue;
        l.replace(QStringLiteral("->"), QStringLiteral(" "));
        const QStringList parts = l.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                          Qt::SkipEmptyParts);
        const QString where = QStringLiteral("第 %1 行「%2」").arg(lineNo).arg(quoteLine(line.trimmed()));
        if (parts.size() < 4) {
            out.unparseable << where + QStringLiteral(" 不足 4 个数");
            continue;
        }
        bool ok[4] = {false, false, false, false};
        double v[4] = {0.0, 0.0, 0.0, 0.0};
        QStringList badTokens;
        for (int k = 0; k < 4; ++k) {
            v[k] = parts[k].toDouble(&ok[k]);
            if (!ok[k]) badTokens << parts[k];
        }
        if (!badTokens.isEmpty()) {
            out.unparseable << where + QStringLiteral(" 的 %1 无法解析为有限十进制数").arg(badTokens.join(QStringLiteral(", ")));
            continue;
        }
        if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]) || !std::isfinite(v[3])) {
            out.nonFinite << where;
            continue;
        }
        out.pixels.append(QPointF(v[0], v[1]));
        out.robots.append(QPointF(v[2], v[3]));
    }
    return out;
}

/// 2D 刚体（旋转+平移，无缩放），对应 HALCON VectorToRigid
/// ⚠️ R-1：改前这里没有任何退化处理——`std::atan2(sxy - syx, sxx + syy)` 在两个参数同时为 0 时
/// 返回 0，于是"一侧点全重合"或"镜像点对"都会拿到一份**恒等旋转 + 质心平移**并判绿
/// （实测：像素侧五对全重合报回 [1, 0, -194.8, 0, 1, -194]；镜像点对报回单位矩阵）。
/// 现在把三类客观无解的输入点名，并顺手算出实际残差读数（只报数、不设阈值：阈值要真机数据才能定）。
struct RigidFit2d
{
    QVector<double> hom;
    QString badReason;          // 空 ⇒ 点对不自相矛盾且旋转角有定义
    double maxResidual = -1.0;  // px
    double rms = -1.0;          // px
};

static bool sameXY(const QPointF &a, const QPointF &b)
{
    return a.x() == b.x() && a.y() == b.y();
}

/// 互不相同的点个数（按坐标精确相等）
static int distinctCount(const QVector<QPointF> &pts)
{
    int d = 0;
    for (int i = 0; i < pts.size(); ++i) {
        bool seen = false;
        for (int j = 0; j < i; ++j)
            if (sameXY(pts[j], pts[i])) { seen = true; break; }
        if (!seen) ++d;
    }
    return d;
}

static RigidFit2d estimateRigid2d(const QVector<QPointF> &from, const QVector<QPointF> &to)
{
    RigidFit2d r;
    const int n = from.size();

    // ① 自相矛盾：同一像素位置报了两遍却指向两个不同的机器人位置 ⇒ 没有任何刚体能满足
    for (int i = 0; i < n && r.badReason.isEmpty(); ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (sameXY(from[i], from[j]) && !sameXY(to[i], to[j])) {
                int against = 0;
                for (int k = 0; k < n; ++k)
                    if (sameXY(from[k], from[i]) && !sameXY(to[k], to[i])) ++against;
                r.badReason = QStringLiteral("手眼点对自相矛盾：像素 (%1, %2) 被映射到 %3 个不同的机器人位置"
                                             "（第 %4 对与第 %5 对）⇒ 无法标定，请删掉重复录入的那一行")
                                  .arg(from[i].x()).arg(from[i].y()).arg(against + 1)
                                  .arg(i + 1).arg(j + 1);
                break;
            }
        }
    }
    if (!r.badReason.isEmpty()) return r;

    // ② 任一侧只剩一个点：旋转无从估计（残差再小也是编出来的）
    const int dFrom = distinctCount(from), dTo = distinctCount(to);
    if (dFrom < 2 || dTo < 2) {
        QStringList sides;
        if (dFrom < 2) sides << QStringLiteral("像素侧 %1 对全部重合于 (%2, %3)").arg(n).arg(from[0].x()).arg(from[0].y());
        if (dTo < 2) sides << QStringLiteral("机器人侧 %1 对全部重合于 (%2, %3)").arg(n).arg(to[0].x()).arg(to[0].y());
        r.badReason = QStringLiteral("手眼点对退化：%1 ⇒ 旋转角无从估计（至少需要 2 个互不相同的点）")
                          .arg(sides.join(QStringLiteral("；")));
        return r;
    }

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

    // ③ 叉积和与点积和同时为 0：atan2(0,0) 把角度定成 0，但这份点对其实连镜像都不是一个旋转
    //    （例：像素 (1,0)/(0,1)/(-1,0)/(0,-1) → 机器人 (0,1)/(1,0)/(0,-1)/(-1,0)，即 (x,y)→(y,x)）
    //    只拦"精确为 0"，近退化留给真机阈值（R-1b）——不自造容差就不误伤合法姿态。
    if (sxy - syx == 0.0 && sxx + syy == 0.0) {
        r.badReason = QStringLiteral("手眼点对退化：旋转角无定义（叉积和与点积和同时为 0，"
                                     "常因点对是镜像关系或左右手方向填反）⇒ 无法标定");
        return r;
    }

    const double angle = std::atan2(sxy - syx, sxx + syy);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double tx = ct.x() - (c * cf.x() - s * cf.y());
    const double ty = ct.y() - (s * cf.x() + c * cf.y());
    r.hom = {c, -s, tx, s, c, ty};

    double sumSq = 0, maxSq = 0;
    for (int i = 0; i < n; ++i) {
        const QPointF fit(r.hom[0] * from[i].x() + r.hom[1] * from[i].y() + r.hom[2],
                          r.hom[3] * from[i].x() + r.hom[4] * from[i].y() + r.hom[5]);
        const double dx = fit.x() - to[i].x(), dy = fit.y() - to[i].y();
        const double sq = dx * dx + dy * dy;
        sumSq += sq;
        if (sq > maxSq) maxSq = sq;   // 不用 std::max：本 TU 经 HALCON/Windows 头带来 max 宏
    }
    r.maxResidual = std::sqrt(maxSq);
    r.rms = std::sqrt(sumSq / n);
    return r;
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
    // 结果字段（不进参数面板）：判红时输出端口会被 process() 清空，原因只能留在这里
    m_params[QStringLiteral("calibNote")] = QString();
    // R-1 残差读数（px）：-1 = 本轮没有可报的拟合结果。只报数不判阈值（阈值属 R-1b／真机）。
    m_params[QStringLiteral("calibMaxResidualPx")] = -1.0;
    m_params[QStringLiteral("calibRmsPx")] = -1.0;
}

void HandEyeCalibNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const QString text = m_params.value(QStringLiteral("pointsText")).toString();
        const QString saveName = m_params.value(QStringLiteral("saveName"), QStringLiteral("handeye1")).toString();

        // 判红动作只此一份：端口 2 写原因 + calibNote 留档 + 残差槽清空 + 写 false + 回吐输入图。
        // ⚠️ 残差槽必须一起置 -1，否则判红轮显示的还是上一轮成功时的残差（陈旧读数）。
        auto judgeRed = [&](const QString &why) {
            setOutputData(1, QSharedPointer<DataObject>());
            auto strObj = QSharedPointer<DataObject>::create();
            strObj->setType(DataObject::DataType::String);
            strObj->setData(why);
            setOutputData(2, strObj);
            m_params[QStringLiteral("calibNote")] = why;
            m_params[QStringLiteral("calibMaxResidualPx")] = -1.0;
            m_params[QStringLiteral("calibRmsPx")] = -1.0;
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
        };

        const HandEyePairs pairs = parseHandEyePairs(text);
        if (!pairs.nonFinite.isEmpty()) {
            judgeRed(QStringLiteral("手眼点对含非有限坐标（nan/inf）：%1 ⇒ 无法标定")
                         .arg(pairs.nonFinite.join(QStringLiteral("；"))));
            return;
        }
        if (!pairs.unparseable.isEmpty()) {
            judgeRed(QStringLiteral("手眼点对有 %1 行解析不出坐标：%2 ⇒ 无法标定（本节点不会跳过坏行）")
                         .arg(pairs.unparseable.size()).arg(pairs.unparseable.join(QStringLiteral("；"))));
            return;
        }
        if (pairs.pixels.size() < 2) {
            judgeRed(QStringLiteral("标定点对数不足（至少 2 对）"));
            return;
        }

        const RigidFit2d fit = estimateRigid2d(pairs.pixels, pairs.robots);
        if (!fit.badReason.isEmpty()) {
            judgeRed(fit.badReason);
            return;
        }
        const QVector<double> homVec = fit.hom;

        if (!saveName.isEmpty()) {
            if (!CalibrationManager::instance()->setHomography(saveName, homVec)) {
                // R-5：该键上已经住着另一种载荷（项数不同，例如 9 元 OpenCV 内参），单例拒绝写入
                // ⇒ 判红。刚体矩阵算出来了却没存下还判绿，就是"自己绿、下游拿不到"（§3.15 同形态）。
                const auto held = CalibrationManager::instance()->homography(saveName).size();
                judgeRed(QStringLiteral("手眼标定结果无法存入：键 \"%1\" 上已有 %2 项的另一种载荷，本次刚体矩阵是 6 项")
                             .arg(saveName).arg(held));
                return;
            }
        }

        auto matObj = QSharedPointer<DataObject>::create();
        matObj->setType(DataObject::DataType::Matrix);
        matObj->setData(QVariant::fromValue(homVec));
        setOutputData(1, matObj);

        const QString matrixText = QStringList(
            {QString::number(homVec[0], 'f', 4), QString::number(homVec[1], 'f', 4),
             QString::number(homVec[2], 'f', 4), QString::number(homVec[3], 'f', 4),
             QString::number(homVec[4], 'f', 4), QString::number(homVec[5], 'f', 4)})
                .join(QStringLiteral(", "));
        QString desc = QStringLiteral("手眼标定成功（%1 对点）\n刚体变换矩阵: [%2]\n最大残差: %3 px（RMS %4 px）")
            .arg(pairs.pixels.size())
            .arg(matrixText)
            .arg(fit.maxResidual, 0, 'f', 4)
            .arg(fit.rms, 0, 'f', 4);
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(desc);
        setOutputData(2, strObj);

        m_params[QStringLiteral("calibNote")] = QString();
        m_params[QStringLiteral("calibMaxResidualPx")] = fit.maxResidual;
        m_params[QStringLiteral("calibRmsPx")] = fit.rms;
        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;
    } catch (const std::exception &e) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("标定失败: %1").arg(QString::fromUtf8(e.what())));
        setOutputData(2, strObj);
        m_params[QStringLiteral("calibNote")] = QStringLiteral("标定失败: %1")
                                                     .arg(QString::fromUtf8(e.what()));
        m_params[QStringLiteral("calibMaxResidualPx")] = -1.0;
        m_params[QStringLiteral("calibRmsPx")] = -1.0;
        m_params["moduleStatus"] = false;
    }
}
