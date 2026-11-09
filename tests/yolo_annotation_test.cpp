// 检测框标注核心（2026-10-28）：归一化/裁剪/读写往返。
// 这些是标注器最容易错的地方（越界裁剪、非法行、空文件语义），而对话框本身难以单测。
#include "YoloAnnotation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class YoloAnnotationTest : public QObject
{
    Q_OBJECT
private slots:
    void pixelToNormalizedRoundTrip();
    void clampsBoxThatSticksOut();
    void rejectsTinyOrFullyOutside();
    void saveLoadRoundTrip();
    void badLinesAreCountedAndSkipped();
    void emptyFileMeansNoObjects();
    void classesRoundTrip();
    void tabSeparatedAndNegativeClassId();
    // 2026-11-09：与 Pascal VOC 标注工具互导（外部训练数据准备）
    void vocExportImportRoundTrip();
    void vocImportAddsUnknownClass();
    void vocRejectsMissingAnnotationsAndSkipsUnlabeled();
};

void YoloAnnotationTest::pixelToNormalizedRoundTrip()
{
    YoloBoxLabel b;
    QVERIFY(YoloBoxLabel::fromPixels(QRectF(100, 50, 200, 100), 400, 200, 2, &b));
    QCOMPARE(b.classId, 2);
    QVERIFY(qAbs(b.x - 0.25) < 1e-9);
    QVERIFY(qAbs(b.y - 0.25) < 1e-9);
    QVERIFY(qAbs(b.w - 0.5) < 1e-9);
    QVERIFY(qAbs(b.h - 0.5) < 1e-9);
    const QRectF back = b.toPixels(400, 200);
    QVERIFY(qAbs(back.left() - 100) < 1e-6);
    QVERIFY(qAbs(back.width() - 200) < 1e-6);
}

void YoloAnnotationTest::clampsBoxThatSticksOut()
{
    // 越过右边界的框：应裁到 1.0 以内
    YoloBoxLabel b;
    QVERIFY(YoloBoxLabel::fromPixels(QRectF(300, 10, 200, 50), 400, 200, 0, &b));
    QVERIFY2(b.x + b.w <= 1.0 + 1e-9, "右边界越界");
    QVERIFY2(b.y + b.h <= 1.0 + 1e-9, "下边界越界");
    // 负坐标同样裁掉
    YoloBoxLabel c;
    QVERIFY(YoloBoxLabel::fromPixels(QRectF(-50, -20, 100, 60), 400, 200, 0, &c));
    QCOMPARE(c.x, 0.0);
    QCOMPARE(c.y, 0.0);
}

void YoloAnnotationTest::rejectsTinyOrFullyOutside()
{
    YoloBoxLabel b;
    QVERIFY2(!YoloBoxLabel::fromPixels(QRectF(10, 10, 0.4, 0.4), 400, 200, 0, &b), "过小的框被接受");
    QVERIFY2(!YoloBoxLabel::fromPixels(QRectF(500, 500, 40, 40), 400, 200, 0, &b), "完全在图外的框被接受");
    QVERIFY2(!YoloBoxLabel::fromPixels(QRectF(10, 10, 40, 40), 0, 200, 0, &b), "非法图尺寸却接受");
}

void YoloAnnotationTest::saveLoadRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString img = dir.path() + QStringLiteral("/a.png");   // 文件本身不必存在
    QVector<YoloBoxLabel> boxes;
    YoloBoxLabel b1;
    QVERIFY(YoloBoxLabel::fromPixels(QRectF(10, 20, 40, 60), 200, 100, 0, &b1));
    YoloBoxLabel b2;
    QVERIFY(YoloBoxLabel::fromPixels(QRectF(100, 10, 50, 50), 200, 100, 3, &b2));
    boxes << b1 << b2;
    QVERIFY(YoloAnnotation::save(img, boxes));
    QVERIFY(QFile::exists(YoloAnnotation::labelPath(img)));
    QCOMPARE(QFileInfo(YoloAnnotation::labelPath(img)).fileName(), QStringLiteral("a.txt"));

    int bad = -1;
    const QVector<YoloBoxLabel> back = YoloAnnotation::load(img, &bad);
    QCOMPARE(back.size(), 2);
    QCOMPARE(bad, 0);
    QCOMPARE(back[0].classId, 0);
    QCOMPARE(back[1].classId, 3);
    QVERIFY(qAbs(back[1].x - b2.x) < 1e-5);
    QVERIFY(qAbs(back[1].h - b2.h) < 1e-5);
}

void YoloAnnotationTest::badLinesAreCountedAndSkipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString img = dir.path() + QStringLiteral("/b.png");
    QFile f(YoloAnnotation::labelPath(img));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream ts(&f);
    ts << "0 0.5 0.5 0.2 0.2\n";        // 好
    ts << "garbage line\n";             // 列数不足
    ts << "1 1.2 0.5 0.2 0.2\n";        // 越界
    ts << "1 0.1 0.1 0.2 0.2 0.9\n";    // 多一列（容忍）
    f.close();

    int bad = 0;
    const QVector<YoloBoxLabel> back = YoloAnnotation::load(img, &bad);
    QCOMPARE(back.size(), 2);
    QCOMPARE(bad, 2);
}

void YoloAnnotationTest::emptyFileMeansNoObjects()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString img = dir.path() + QStringLiteral("/c.png");
    QVERIFY(YoloAnnotation::save(img, {}));
    QVERIFY2(QFile::exists(YoloAnnotation::labelPath(img)), "空标注应写出空文件（= 这张图没有目标）");
    QCOMPARE(YoloAnnotation::load(img).size(), 0);
    // 没写过 txt 的图：加载为空但不报错
    QCOMPARE(YoloAnnotation::load(dir.path() + QStringLiteral("/never.png")).size(), 0);
}

void YoloAnnotationTest::classesRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QStringList names{QStringLiteral("划痕"), QStringLiteral("脏污"), QStringLiteral("ok")};
    QVERIFY(YoloAnnotation::saveClasses(dir.path(), names));
    QCOMPARE(YoloAnnotation::loadClasses(dir.path()), names);
    QCOMPARE(YoloAnnotation::loadClasses(dir.path() + QStringLiteral("/classes.txt")), names);
    // 缺文件 → 空列表（不崩）
    QVERIFY(YoloAnnotation::loadClasses(dir.path() + QStringLiteral("/nope.txt")).isEmpty());
}

void YoloAnnotationTest::tabSeparatedAndNegativeClassId()
{
    // 2026-10-28 复核 P3：制表符分隔是很多标注工具的默认输出，不能当非法行；
    // 负 classId 会让训练工具静默丢类，必须算坏行。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString img = dir.path() + QStringLiteral("/t.png");
    QFile f(YoloAnnotation::labelPath(img));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream ts(&f);
    ts << "0\t0.5\t0.5\t0.2\t0.2\n";     // tab 分隔：好
    ts << "-1 0.1 0.1 0.2 0.2\n";           // 负类号：坏
    f.close();
    int bad = 0;
    const auto back = YoloAnnotation::load(img, &bad);
    QCOMPARE(back.size(), 1);
    QCOMPARE(bad, 1);
}

void YoloAnnotationTest::vocExportImportRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path();
    QVERIFY(YoloAnnotation::saveClasses(dir, {QStringLiteral("cat"), QStringLiteral("dog")}));

    QImage img(100, 80, QImage::Format_RGB32);
    img.fill(Qt::black);
    const QString imgPath = dir + QStringLiteral("/a.png");
    QVERIFY(img.save(imgPath));

    QVector<YoloBoxLabel> labels;
    YoloBoxLabel b0;
    YoloBoxLabel b1;
    QVERIFY(YoloBoxLabel::fromPixels(QRectF(10, 10, 20, 10), 100, 80, 0, &b0));
    QVERIFY(YoloBoxLabel::fromPixels(QRectF(50, 40, 30, 20), 100, 80, 1, &b1));
    labels << b0 << b1;
    QVERIFY(YoloAnnotation::save(imgPath, labels));

    int exported = 0;
    int skipped = 0;
    QString error;
    QVERIFY2(YoloAnnotation::exportVoc(dir, &exported, &skipped, &error), qPrintable(error));
    QCOMPARE(exported, 1);
    QCOMPARE(skipped, 0);
    QVERIFY(QFileInfo::exists(dir + QStringLiteral("/Annotations/a.xml")));

    // 删掉 YOLO txt 后从 XML 导回：坐标/类别必须一一对上
    QVERIFY(QFile::remove(YoloAnnotation::labelPath(imgPath)));
    int imported = 0;
    QStringList added;
    QVERIFY2(YoloAnnotation::importVoc(dir, &imported, &skipped, &added, &error),
             qPrintable(error));
    QCOMPARE(imported, 1);
    QCOMPARE(skipped, 0);
    QVERIFY2(added.isEmpty(), "已有类别不应被重复追加");

    const QVector<YoloBoxLabel> back = YoloAnnotation::load(imgPath);
    QCOMPARE(back.size(), 2);
    QCOMPARE(back.at(0).classId, 0);
    QVERIFY(qAbs(back.at(0).x - 0.10) < 1e-6);
    QVERIFY(qAbs(back.at(0).y - 0.125) < 1e-6);
    QVERIFY(qAbs(back.at(0).w - 0.20) < 1e-6);
    QVERIFY(qAbs(back.at(0).h - 0.125) < 1e-6);
    QCOMPARE(back.at(1).classId, 1);
    QVERIFY(qAbs(back.at(1).x - 0.50) < 1e-6);
    QVERIFY(qAbs(back.at(1).y - 0.50) < 1e-6);
    QVERIFY(qAbs(back.at(1).w - 0.30) < 1e-6);
    QVERIFY(qAbs(back.at(1).h - 0.25) < 1e-6);
}

void YoloAnnotationTest::vocImportAddsUnknownClass()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path();
    QVERIFY(YoloAnnotation::saveClasses(dir, {QStringLiteral("cat")}));
    QImage img(100, 80, QImage::Format_RGB32);
    img.fill(Qt::black);
    QVERIFY(img.save(dir + QStringLiteral("/a.png")));
    QVERIFY(QDir(dir).mkpath(QStringLiteral("Annotations")));

    QFile xmlFile(dir + QStringLiteral("/Annotations/a.xml"));
    QVERIFY(xmlFile.open(QIODevice::WriteOnly | QIODevice::Text));
    xmlFile.write("<annotation><filename>a.png</filename>"
                  "<size><width>100</width><height>80</height><depth>3</depth></size>"
                  "<object><name>bird</name>"
                  "<bndbox><xmin>11</xmin><ymin>11</ymin><xmax>30</xmax><ymax>20</ymax></bndbox>"
                  "</object></annotation>");
    xmlFile.close();

    int imported = 0;
    int skipped = 0;
    QStringList added;
    QString error;
    QVERIFY2(YoloAnnotation::importVoc(dir, &imported, &skipped, &added, &error),
             qPrintable(error));
    QCOMPARE(imported, 1);
    QCOMPARE(skipped, 0);
    QCOMPARE(added, QStringList{QStringLiteral("bird")});
    QCOMPARE(YoloAnnotation::loadClasses(dir),
             (QStringList{QStringLiteral("cat"), QStringLiteral("bird")}));
    const QVector<YoloBoxLabel> back = YoloAnnotation::load(dir + QStringLiteral("/a.png"));
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.at(0).classId, 1);
}

void YoloAnnotationTest::vocRejectsMissingAnnotationsAndSkipsUnlabeled()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path();
    int imported = 0;
    int skipped = 0;
    QString error;
    QVERIFY2(!YoloAnnotation::importVoc(dir, &imported, &skipped, nullptr, &error),
             "没有 Annotations 目录却返回成功");
    QVERIFY2(!error.isEmpty(), "失败没有可读原因");

    // 导出：只标注了 a.png，b.png 没有 txt → 跳过；没有 classes.txt → 整体失败
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::black);
    QVERIFY(img.save(dir + QStringLiteral("/a.png")));
    QVERIFY(img.save(dir + QStringLiteral("/b.png")));
    QVERIFY(YoloAnnotation::save(dir + QStringLiteral("/a.png"), {}));
    int exported = 0;
    QVERIFY2(!YoloAnnotation::exportVoc(dir, &exported, &skipped, &error),
             "缺 classes.txt 却导出成功");
    QVERIFY(YoloAnnotation::saveClasses(dir, {QStringLiteral("x")}));
    QVERIFY2(YoloAnnotation::exportVoc(dir, &exported, &skipped, &error), qPrintable(error));
    QCOMPARE(exported, 1);
    QCOMPARE(skipped, 1);
    QVERIFY(QFileInfo::exists(dir + QStringLiteral("/Annotations/a.xml")));
    QVERIFY(!QFileInfo::exists(dir + QStringLiteral("/Annotations/b.xml")));
}

QTEST_MAIN(YoloAnnotationTest)
#include "yolo_annotation_test.moc"
