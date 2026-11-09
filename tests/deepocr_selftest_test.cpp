// DeepOCR 现场自检工具的数据面（不跑引擎：引擎需要 HALCON 模型，CI 里没有）。
// 只钉"收集图片"与"报告/CSV 格式"这两个换机时最容易出错的环节。
// 注意：测试类名不能叫 DeepOcrSelfTest —— 会和同名命名空间冲突。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "DeepOcrSelfTest.h"

class DeepOcrToolTest : public QObject
{
    Q_OBJECT

private slots:
    void testCollectImages();
    void testCollectImagesRecursesSubdirs();
    void testFormatReport();
    void testZeroWordsHintDependsOnMode();
    void testFormatCsv();
};

void DeepOcrToolTest::testCollectImages()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const char *n : {"a.png", "b.jpg", "c.bmp"}) {
        QImage img(8, 8, QImage::Format_Grayscale8);
        img.fill(128);
        QVERIFY(img.save(QDir(dir.path()).filePath(QString::fromLatin1(n))));
    }
    QFile junk(QDir(dir.path()).filePath(QStringLiteral("d.txt")));
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write("x");
    junk.close();

    const QStringList images = DeepOcrSelfTest::collectImages(dir.path());
    QCOMPARE(images.size(), 3);
    for (const QString &p : images)
        QVERIFY2(!p.endsWith(QStringLiteral(".txt")), "非图片扩展名不应被收集");

    const QStringList one = DeepOcrSelfTest::collectImages(images.first());
    QCOMPARE(one.size(), 1);
    QCOMPARE(one.first(), images.first());

    QVERIFY(DeepOcrSelfTest::collectImages(dir.filePath(QStringLiteral("nope"))).isEmpty());
}

// 现场样本常按批次放子目录；头文件写「递归」而旧实现只扫一层（2026-11-09 复核）。
void DeepOcrToolTest::testCollectImagesRecursesSubdirs()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sub = QDir(dir.path()).filePath(QStringLiteral("batch_01"));
    const QString sub2 = QDir(sub).filePath(QStringLiteral("nested"));
    QVERIFY(QDir().mkpath(sub2));
    QImage img(8, 8, QImage::Format_Grayscale8);
    img.fill(128);
    for (const QString &p : {QDir(dir.path()).filePath(QStringLiteral("root.png")),
                             QDir(sub).filePath(QStringLiteral("a.jpg")),
                             QDir(sub2).filePath(QStringLiteral("b.png"))}) {
        QVERIFY(img.save(p));
    }
    QFile junk(QDir(sub2).filePath(QStringLiteral("note.txt")));
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write("x");
    junk.close();

    const QStringList images = DeepOcrSelfTest::collectImages(dir.path());
    QCOMPARE(images.size(), 3);   // 三层各一张，非图片排除
    QVERIFY2(images.contains(QDir(sub2).filePath(QStringLiteral("b.png"))),
             "嵌套子目录的图片必须被收集");
    // --limit 生效且顺序稳定
    const QStringList limited = DeepOcrSelfTest::collectImages(dir.path(), 2);
    QCOMPARE(limited.size(), 2);
    QCOMPARE(limited, images.mid(0, 2));
}

void DeepOcrToolTest::testFormatReport()
{
    QVector<DeepOcrSelfTest::ImageResult> results;
    DeepOcrSelfTest::ImageResult ok;
    ok.path = QStringLiteral("/tmp/ok.png");
    ok.ok = true;
    ok.text = QStringLiteral("ABC123");
    ok.wordCount = 1;
    ok.avgConfidence = 0.912;
    ok.maxConfidence = 0.912;
    ok.elapsedMs = 42;
    results << ok;

    DeepOcrSelfTest::ImageResult bad;
    bad.path = QStringLiteral("/tmp/bad.png");
    bad.ok = false;
    bad.error = QStringLiteral("图片无法读取（路径或格式不支持）");
    results << bad;

    const QString report = DeepOcrSelfTest::formatReport(results);
    QVERIFY2(report.contains(QStringLiteral("[OK]")), "成功行要有 OK 标记");
    QVERIFY2(report.contains(QStringLiteral("ABC123")), "报告要带识别文本");
    QVERIFY2(report.contains(QStringLiteral("[FAIL]")), "失败行要有 FAIL 标记");
    QVERIFY2(report.contains(QStringLiteral("1/2 成功")), "汇总要给出成功率");
    QVERIFY2(report.contains(QStringLiteral("1 失败")), "汇总要给出失败数");
    QVERIFY2(report.contains(QStringLiteral("最慢")), "汇总要给出最慢耗时");
    QVERIFY2(report.contains(QStringLiteral("词数 1")), "汇总要给出总词数");
    QVERIFY2(report.contains(QStringLiteral("平均置信度 0.912")), "汇总要给出平均置信度");
    QVERIFY2(report.contains(QStringLiteral("失败明细")), "失败项要单独列明细，别让人翻日志");
    QVERIFY2(report.contains(QStringLiteral("bad.png")), "失败明细要带文件名");
}

// 复核 P1-6 残留：旧提示在已经是 recognition 时仍建议「改 --mode 2」，误导现场。
void DeepOcrToolTest::testZeroWordsHintDependsOnMode()
{
    DeepOcrSelfTest::ImageResult r;
    r.path = QStringLiteral("/tmp/blank.png");
    r.ok = true;
    r.wordCount = 0;
    QVector<DeepOcrSelfTest::ImageResult> results{r};

    r.mode = 2;
    results[0] = r;
    const QString recognitionHint = DeepOcrSelfTest::formatReport(results);
    QVERIFY2(!recognitionHint.contains(QStringLiteral("改 --mode 2")),
             "recognition 模式下不该再建议改 --mode 2");
    QVERIFY2(recognitionHint.contains(QStringLiteral("recognition 模式下仍读出 0 个词")),
             "recognition 模式应提示检查图片/阈值/模型");

    r.mode = 0;
    results[0] = r;
    const QString autoHint = DeepOcrSelfTest::formatReport(results);
    QVERIFY2(autoHint.contains(QStringLiteral("改 --mode 2")),
             "auto 模式无词时应建议改用 recognition");
}

void DeepOcrToolTest::testFormatCsv()
{
    QVector<DeepOcrSelfTest::ImageResult> results;
    DeepOcrSelfTest::ImageResult r;
    r.path = QStringLiteral("/tmp/a.png");
    r.ok = true;
    r.text = QStringLiteral("he said \"hi\"");
    r.wordCount = 3;
    r.avgConfidence = 0.8;
    r.maxConfidence = 0.95;
    r.elapsedMs = 10;
    results << r;

    const QString csv = DeepOcrSelfTest::formatCsv(results);
    QVERIFY2(csv.startsWith(QStringLiteral("file,ok,")), "CSV 要有表头");
    QVERIFY2(csv.contains(QStringLiteral("\"he said \"\"hi\"\"\"")),
             "文本里的双引号要按 CSV 规则转义成两个");
    QCOMPARE(csv.count(QLatin1Char('\n')), 1);   // 表头 + 1 行数据
    QVERIFY2(csv.startsWith(QStringLiteral("file,ok,")),
             "mode 列追加在末尾，既有表头前缀必须保持兼容");
    QVERIFY2(csv.contains(QStringLiteral(",error,mode")), "表头末尾要有 mode 列");
    QVERIFY2(csv.contains(QStringLiteral("\"a.png\",1,3,")) ||
                 csv.contains(QStringLiteral("\"a.png\",1,3")),
             "数据行应带 ok/words");

    // 递归后同名文件用相对路径区分
    r.relativePath = QStringLiteral("batch_01/a.png");
    r.mode = 2;
    results[0] = r;
    const QString csv2 = DeepOcrSelfTest::formatCsv(results);
    QVERIFY2(csv2.contains(QStringLiteral("\"batch_01/a.png\"")),
             "有相对路径时 CSV 应写相对路径，避免子目录同名混淆");
    QVERIFY2(csv2.trimmed().endsWith(QStringLiteral(",2")),
             "CSV 最后一列应是 mode=2");
}

QTEST_MAIN(DeepOcrToolTest)
#include "deepocr_selftest_test.moc"
