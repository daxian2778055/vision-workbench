#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <QFile>
#include <QRegularExpression>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>

/// 检测框标注的读写（2026-10-28，YOLOX 路线）。
///
/// YOLO 系列的标注格式是**每张图一个同名 txt**：`cls cx cy w h`（全部归一化到 0..1，
/// 原点左上）。Ultralytics 与 YOLOX 都直接吃这个格式——所以标注器只要写对 txt，
/// 外部训练工具就能用，不需要再"导出"。
///
/// 抽成纯函数的原因：归一化/反归一化的边界（裁剪、越界、非法行）是标注器最容易错的地方，
/// 而对话框本身难以单测。
struct YoloBoxLabel {
    int classId = 0;
    /// 归一化框（0..1，xy 为左上角）
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;

    QRectF toPixels(int imgW, int imgH) const
    {
        return QRectF(x * imgW, y * imgH, w * imgW, h * imgH);
    }

    /// 由像素框构造（自动裁剪到图内并归一化；越界部分丢弃）
    static bool fromPixels(const QRectF &px, int imgW, int imgH, int clsId, YoloBoxLabel *out)
    {
        if (!out || imgW <= 0 || imgH <= 0)
            return false;
        const QRectF clipped = px.normalized().intersected(QRectF(0, 0, imgW, imgH));
        if (clipped.width() < 1.0 || clipped.height() < 1.0)
            return false;   // 太小或完全在图外：不产生标注
        out->classId = clsId;
        out->x = clipped.left() / imgW;
        out->y = clipped.top() / imgH;
        out->w = clipped.width() / imgW;
        out->h = clipped.height() / imgH;
        // 数值兜底：浮点误差可能让右/下边稍微越界
        out->x = std::max(0.0, std::min(out->x, 1.0));
        out->y = std::max(0.0, std::min(out->y, 1.0));
        out->w = std::max(0.0, std::min(out->w, 1.0 - out->x));
        out->h = std::max(0.0, std::min(out->h, 1.0 - out->y));
        return out->w > 0.0 && out->h > 0.0;
    }
};

class YoloAnnotation
{
public:
    /// 读同名的 .txt（不存在 → 空；坏行跳过并计数）
    static QVector<YoloBoxLabel> load(const QString &imagePath, int *badLines = nullptr)
    {
        QVector<YoloBoxLabel> out;
        if (badLines)
            *badLines = 0;
        QFile f(labelPath(imagePath));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return out;
        QTextStream ts(&f);
        while (!ts.atEnd()) {
            // 任何空白分隔都认（2026-10-28 复核 P3）：只按空格切会让制表符分隔的 txt
            // 整行被当成非法——而制表符是很多标注工具的默认输出
            static const QRegularExpression ws(QStringLiteral("[\\t ]+"));
            const QStringList parts = ts.readLine().trimmed().split(ws, Qt::SkipEmptyParts);
            if (parts.size() < 5) {
                if (badLines)
                    ++*badLines;
                continue;
            }
            bool ok = true;
            YoloBoxLabel b;
            b.classId = parts[0].toInt(&ok);
            if (ok) b.x = parts[1].toDouble(&ok);
            if (ok) b.y = parts[2].toDouble(&ok);
            if (ok) b.w = parts[3].toDouble(&ok);
            if (ok) b.h = parts[4].toDouble(&ok);
            // 归一化越界的一律丢弃：训练时会静默裁掉，不如现在就说
            // classId 也校验（2026-10-28 复核 P3）：负数会让训练工具静默丢类
            if (!ok || b.classId < 0
                || b.x < 0 || b.y < 0 || b.w <= 0 || b.h <= 0
                || b.x + b.w > 1.0001 || b.y + b.h > 1.0001) {
                if (badLines)
                    ++*badLines;
                continue;
            }
            out.append(b);
        }
        return out;
    }

    /// 写同名 .txt（空标注也写空文件：**空文件=这张图没有目标**，与"没标注过"语义不同）
    static bool save(const QString &imagePath, const QVector<YoloBoxLabel> &labels)
    {
        QFile f(labelPath(imagePath));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return false;
        QTextStream ts(&f);
        for (const YoloBoxLabel &b : labels)
            ts << b.classId << ' ' << QString::number(b.x, 'f', 6) << ' '
               << QString::number(b.y, 'f', 6) << ' ' << QString::number(b.w, 'f', 6) << ' '
               << QString::number(b.h, 'f', 6) << '\n';
        ts.flush();
        // 2026-10-28 复核 P3：只查 QTextStream 不够，磁盘满在 close/flush 时才暴露
        const bool ok = ts.status() == QTextStream::Ok && f.flush();
        f.close();
        return ok;
    }

    /// 同名的标注文件路径（image.jpg → image.txt）
    static QString labelPath(const QString &imagePath)
    {
        const QFileInfo fi(imagePath);
        return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
               + QStringLiteral(".txt");
    }

    /// 读 classes.txt（每行一个类名）
    static QStringList loadClasses(const QString &dirOrFile)
    {
        QString path = dirOrFile;
        if (QFileInfo(path).isDir())
            path = path + QStringLiteral("/classes.txt");
        QStringList names;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return names;
        QTextStream ts(&f);
        while (!ts.atEnd()) {
            const QString line = ts.readLine().trimmed();
            if (!line.isEmpty())
                names.append(line);
        }
        return names;
    }

    static bool saveClasses(const QString &dir, const QStringList &names)
    {
        QFile f(dir + QStringLiteral("/classes.txt"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return false;
        QTextStream ts(&f);
        for (const QString &n : names)
            ts << n << '\n';
        ts.flush();
        return ts.status() == QTextStream::Ok;
    }
};
