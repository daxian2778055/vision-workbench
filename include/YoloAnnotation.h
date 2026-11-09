#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QRegularExpression>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

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
        const bool streamOk = ts.status() == QTextStream::Ok && f.flush();
        f.close();
        // close() 时的错误也要看（2026-10-29）：磁盘满常在 close 才暴露
        return streamOk && f.error() == QFileDevice::NoError;
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

    /// 图像扩展名（VOC 互导与标注器共用一份）
    static QStringList imageFilters()
    {
        return {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                QStringLiteral("*.png"), QStringLiteral("*.bmp"),
                QStringLiteral("*.tif"), QStringLiteral("*.tiff")};
    }

    /// 从 Pascal VOC XML 导入到同目录的 YOLO txt（2026-11-09）。
    /// 目录约定：图像在 dir/，标注在 dir/Annotations/<同名>.xml，类别表 dir/classes.txt。
    /// XML 里出现的新类名会追加到 classes.txt（addedClasses 返回追加项）。
    /// 返回 false 仅表示目录/标注集不可用；单个坏文件计入 skippedFiles、不中断整批。
    static bool importVoc(const QString &dir, int *importedImages = nullptr,
                          int *skippedFiles = nullptr, QStringList *addedClasses = nullptr,
                          QString *error = nullptr)
    {
        if (importedImages) *importedImages = 0;
        if (skippedFiles) *skippedFiles = 0;
        if (addedClasses) addedClasses->clear();
        const QDir root(dir);
        const QDir ann(root.filePath(QStringLiteral("Annotations")));
        if (!root.exists() || !ann.exists()) {
            if (error)
                *error = QStringLiteral("缺少 Annotations 目录：%1").arg(ann.absolutePath());
            return false;
        }
        const QFileInfoList xmls = ann.entryInfoList({QStringLiteral("*.xml")},
                                                    QDir::Files, QDir::Name);
        if (xmls.isEmpty()) {
            if (error) *error = QStringLiteral("Annotations 目录里没有 .xml");
            return false;
        }

        QStringList classes = loadClasses(dir);
        QHash<QString, int> classIds;
        for (int i = 0; i < classes.size(); ++i)
            classIds.insert(classes.at(i), i);

        int ok = 0;
        int skipped = 0;
        bool classesDirty = false;
        for (const QFileInfo &xf : xmls) {
            QFile f(xf.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                ++skipped;
                continue;
            }
            QXmlStreamReader xml(&f);
            QString imageName;
            int imgW = 0;
            int imgH = 0;
            QVector<YoloBoxLabel> labels;
            bool inObject = false;
            bool hasBox = false;
            QString objName;
            double xmin = 0, ymin = 0, xmax = 0, ymax = 0;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement()) {
                    const QStringView n = xml.name();
                    if (n == QLatin1String("filename")) {
                        imageName = xml.readElementText().trimmed();
                    } else if (n == QLatin1String("width")) {
                        imgW = xml.readElementText().toInt();
                    } else if (n == QLatin1String("height")) {
                        imgH = xml.readElementText().toInt();
                    } else if (n == QLatin1String("object")) {
                        inObject = true;
                        hasBox = false;
                        objName.clear();
                        xmin = ymin = xmax = ymax = 0;
                    } else if (inObject && n == QLatin1String("name")) {
                        objName = xml.readElementText().trimmed();
                    } else if (inObject && n == QLatin1String("xmin")) {
                        xmin = xml.readElementText().toDouble();
                        hasBox = true;
                    } else if (inObject && n == QLatin1String("ymin")) {
                        ymin = xml.readElementText().toDouble();
                    } else if (inObject && n == QLatin1String("xmax")) {
                        xmax = xml.readElementText().toDouble();
                    } else if (inObject && n == QLatin1String("ymax")) {
                        ymax = xml.readElementText().toDouble();
                    }
                } else if (xml.isEndElement() && xml.name() == QLatin1String("object")) {
                    if (!objName.isEmpty() && hasBox && imgW > 0 && imgH > 0) {
                        int id = classIds.value(objName, -1);
                        if (id < 0) {
                            id = classes.size();
                            classes.append(objName);
                            classIds.insert(objName, id);
                            if (addedClasses) addedClasses->append(objName);
                            classesDirty = true;
                        }
                        // VOC 坐标是 1 基且含端点；转成像素矩形后统一走 fromPixels 裁剪
                        const QRectF px(xmin - 1.0, ymin - 1.0,
                                        std::max(0.0, xmax - xmin + 1.0),
                                        std::max(0.0, ymax - ymin + 1.0));
                        YoloBoxLabel b;
                        if (YoloBoxLabel::fromPixels(px, imgW, imgH, id, &b))
                            labels.append(b);
                    }
                    inObject = false;
                }
            }
            if (xml.hasError()) {
                ++skipped;
                continue;
            }
            QString imgPath;
            if (!imageName.isEmpty()) {
                const QString cand = root.filePath(QFileInfo(imageName).fileName());
                if (QFileInfo::exists(cand)) imgPath = cand;
            }
            if (imgPath.isEmpty()) {
                const QString base = xf.completeBaseName();
                for (const QString &filter : imageFilters()) {
                    const QString cand = root.filePath(base + filter.mid(1));   // ".jpg"
                    if (QFileInfo::exists(cand)) {
                        imgPath = cand;
                        break;
                    }
                }
            }
            if (imgPath.isEmpty()) {
                ++skipped;
                continue;
            }
            if (!save(imgPath, labels)) {
                ++skipped;
                continue;
            }
            ++ok;
        }
        if (classesDirty) saveClasses(dir, classes);
        if (importedImages) *importedImages = ok;
        if (skippedFiles) *skippedFiles = skipped;
        return true;
    }

    /// 导出为 Pascal VOC XML（dir/Annotations/<同名>.xml）。没有 txt 的图跳过（skippedFiles）。
    /// 已有 XML 会被覆盖；返回 false 仅表示目录/classes.txt 不可用。
    static bool exportVoc(const QString &dir, int *exportedImages = nullptr,
                          int *skippedFiles = nullptr, QString *error = nullptr)
    {
        if (exportedImages) *exportedImages = 0;
        if (skippedFiles) *skippedFiles = 0;
        const QDir root(dir);
        if (!root.exists()) {
            if (error) *error = QStringLiteral("目录不存在：%1").arg(dir);
            return false;
        }
        const QStringList classes = loadClasses(dir);
        if (classes.isEmpty()) {
            if (error) *error = QStringLiteral("classes.txt 缺失或为空");
            return false;
        }
        if (!root.mkpath(QStringLiteral("Annotations"))) {
            if (error) *error = QStringLiteral("无法创建 Annotations 目录");
            return false;
        }
        const QFileInfoList images = root.entryInfoList(imageFilters(), QDir::Files, QDir::Name);
        int ok = 0;
        int skipped = 0;
        for (const QFileInfo &img : images) {
            const QString txt = labelPath(img.absoluteFilePath());
            if (!QFileInfo::exists(txt)) {   // 没标注过 ≠ 空标注
                ++skipped;
                continue;
            }
            QImage image(img.absoluteFilePath());
            if (image.isNull()) {
                ++skipped;
                continue;
            }
            int bad = 0;
            const QVector<YoloBoxLabel> labels = load(img.absoluteFilePath(), &bad);
            QFile f(root.filePath(QStringLiteral("Annotations/") + img.completeBaseName()
                                  + QStringLiteral(".xml")));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                ++skipped;
                continue;
            }
            QXmlStreamWriter xml(&f);
            xml.setAutoFormatting(true);
            xml.writeStartDocument();
            xml.writeStartElement(QStringLiteral("annotation"));
            xml.writeTextElement(QStringLiteral("folder"), root.dirName());
            xml.writeTextElement(QStringLiteral("filename"), img.fileName());
            xml.writeStartElement(QStringLiteral("size"));
            xml.writeTextElement(QStringLiteral("width"), QString::number(image.width()));
            xml.writeTextElement(QStringLiteral("height"), QString::number(image.height()));
            xml.writeTextElement(QStringLiteral("depth"), QString::number(image.depth() / 8));
            xml.writeEndElement();   // size
            xml.writeTextElement(QStringLiteral("segmented"), QStringLiteral("0"));
            for (const YoloBoxLabel &b : labels) {
                const QRectF px = b.toPixels(image.width(), image.height());
                const int xmin = qBound(1, int(std::floor(px.left())) + 1, image.width());
                const int ymin = qBound(1, int(std::floor(px.top())) + 1, image.height());
                const int xmax = qBound(xmin, int(std::ceil(px.right())), image.width());
                const int ymax = qBound(ymin, int(std::ceil(px.bottom())), image.height());
                xml.writeStartElement(QStringLiteral("object"));
                xml.writeTextElement(QStringLiteral("name"),
                                     classes.value(b.classId, QString::number(b.classId)));
                xml.writeTextElement(QStringLiteral("pose"), QStringLiteral("Unspecified"));
                xml.writeTextElement(QStringLiteral("truncated"), QStringLiteral("0"));
                xml.writeTextElement(QStringLiteral("difficult"), QStringLiteral("0"));
                xml.writeStartElement(QStringLiteral("bndbox"));
                xml.writeTextElement(QStringLiteral("xmin"), QString::number(xmin));
                xml.writeTextElement(QStringLiteral("ymin"), QString::number(ymin));
                xml.writeTextElement(QStringLiteral("xmax"), QString::number(xmax));
                xml.writeTextElement(QStringLiteral("ymax"), QString::number(ymax));
                xml.writeEndElement();   // bndbox
                xml.writeEndElement();   // object
            }
            xml.writeEndElement();   // annotation
            xml.writeEndDocument();
            const bool writeOk = !xml.hasError() && f.error() == QFileDevice::NoError;
            f.close();
            if (!writeOk || f.error() != QFileDevice::NoError) {
                ++skipped;
                continue;
            }
            ++ok;
        }
        if (exportedImages) *exportedImages = ok;
        if (skippedFiles) *skippedFiles = skipped;
        return true;
    }
};
