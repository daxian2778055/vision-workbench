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
    void testFormatReport();
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
}

QTEST_MAIN(DeepOcrToolTest)
#include "deepocr_selftest_test.moc"
