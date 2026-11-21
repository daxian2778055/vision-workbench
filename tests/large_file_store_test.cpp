// 采集策略·大文件存图（VM 4.4 手册 11.5）：批量写盘 / 读取 / 解包
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

#include "LargeFileImageStore.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/core.hpp>

class LargeFileStoreTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void batchWritesWhenFull();
    void disabledAppendReportsReason();
    void settingsRejectsEnabledWithoutDirectory();
    void extractProducesPngFiles();
    void settingsClampBatchSize();
    void flushReportsThreeStates();          // P10：无数据/写成功/写失败必须可区分
    void readBatchRejectsOversizedEntry();   // P10：坏文件声明超大长度不得触发巨量分配

private:
    LargeFileImageStore::Settings m_original;
};

void LargeFileStoreTest::initTestCase()
{
    m_original = LargeFileImageStore::instance()->settings();
}

void LargeFileStoreTest::cleanupTestCase()
{
    LargeFileImageStore::instance()->flush();
    QString why;
    LargeFileImageStore::instance()->setSettings(m_original, &why);
}

void LargeFileStoreTest::batchWritesWhenFull()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto *store = LargeFileImageStore::instance();

    LargeFileImageStore::Settings s;
    s.enabled = true;
    s.directory = dir.path();
    s.batchSize = 2;
    QString error;
    QVERIFY2(store->setSettings(s, &error), qPrintable(error));
    QCOMPARE(store->pendingCount(), 0);

    cv::Mat img1(10, 20, CV_8UC1, cv::Scalar(10));
    cv::Mat img2(30, 40, CV_8UC1, cv::Scalar(20));
    cv::Mat img3(50, 60, CV_8UC1, cv::Scalar(30));

    QString batch;
    QVERIFY2(store->append(img1, QStringLiteral("m1"), &batch, &error), qPrintable(error));
    QVERIFY2(batch.isEmpty(), "未满一批时不应落盘");
    QCOMPARE(store->pendingCount(), 1);

    QVERIFY2(store->append(img2, QStringLiteral("m2"), &batch, &error), qPrintable(error));
    QVERIFY2(!batch.isEmpty(), "攒满 batchSize 应立即落盘");
    QVERIFY2(QFile::exists(batch), "批次文件应存在");
    QCOMPARE(store->pendingCount(), 0);

    QVERIFY2(store->append(img3, QStringLiteral("m3"), nullptr, &error), qPrintable(error));
    QCOMPARE(store->pendingCount(), 1);
    QCOMPARE(store->flush(), LargeFileImageStore::FlushResult::Written);
    QCOMPARE(store->pendingCount(), 0);

    const QStringList dats = QDir(dir.path()).entryList({QStringLiteral("*.dat")}, QDir::Files,
                                                         QDir::Name);
    QCOMPARE(dats.size(), 2);

    QVector<LargeImageEntry> entries;
    QVERIFY2(LargeFileImageStore::readBatch(QDir(dir.path()).filePath(dats.first()),
                                            &entries, &error),
             qPrintable(error));
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].name, QStringLiteral("m1"));
    QCOMPARE(entries[1].name, QStringLiteral("m2"));

    std::vector<uchar> bytes(entries[0].pngData.begin(), entries[0].pngData.end());
    const cv::Mat decoded = cv::imdecode(bytes, cv::IMREAD_UNCHANGED);
    QVERIFY2(!decoded.empty(), "批次条目应能解码成图像");
    QCOMPARE(decoded.cols, img1.cols);
    QCOMPARE(decoded.rows, img1.rows);

    // 关闭开关，避免污染同进程后续用例
    s.enabled = false;
    QVERIFY2(store->setSettings(s, &error), qPrintable(error));
}

void LargeFileStoreTest::disabledAppendReportsReason()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto *store = LargeFileImageStore::instance();
    LargeFileImageStore::Settings s;
    s.enabled = false;
    s.directory = dir.path();
    s.batchSize = 2;
    QVERIFY(store->setSettings(s));

    cv::Mat img(8, 8, CV_8UC1, cv::Scalar(7));
    QString error;
    QVERIFY2(!store->append(img, QStringLiteral("m"), nullptr, &error),
             "未启用时 append 应失败");
    QVERIFY2(!error.isEmpty(), "失败必须给出原因");
}

void LargeFileStoreTest::settingsRejectsEnabledWithoutDirectory()
{
    auto *store = LargeFileImageStore::instance();
    LargeFileImageStore::Settings s;
    s.enabled = true;
    s.directory = QStringLiteral("   ");
    s.batchSize = 5;
    QString error;
    QVERIFY2(!store->setSettings(s, &error), "启用但未设置路径应被拒绝");
    QVERIFY2(!error.isEmpty(), "拒绝必须给出原因");
}

void LargeFileStoreTest::extractProducesPngFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto *store = LargeFileImageStore::instance();
    LargeFileImageStore::Settings s;
    s.enabled = true;
    s.directory = dir.path();
    s.batchSize = 1;
    QString error;
    QVERIFY2(store->setSettings(s, &error), qPrintable(error));

    cv::Mat img(12, 16, CV_8UC1, cv::Scalar(99));
    QString batch;
    QVERIFY2(store->append(img, QStringLiteral("out1"), &batch, &error), qPrintable(error));
    QVERIFY(QFile::exists(batch));

    const QString outDir = dir.filePath(QStringLiteral("extract"));
    QCOMPARE(LargeFileImageStore::extractBatch(batch, outDir, &error), 1);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(QFile::exists(QDir(outDir).filePath(QStringLiteral("0001_out1.png"))),
             "解包文件名应为 序号_名称.png");

    s.enabled = false;
    QVERIFY(store->setSettings(s));
}

void LargeFileStoreTest::settingsClampBatchSize()
{
    auto *store = LargeFileImageStore::instance();
    LargeFileImageStore::Settings s;
    s.enabled = false;
    s.directory = QString();
    s.batchSize = 0;   // 非法值：应被夹到 1
    QString error;
    QVERIFY2(store->setSettings(s, &error), qPrintable(error));
    QCOMPARE(store->settings().batchSize, 1);
}

void LargeFileStoreTest::flushReportsThreeStates()
{
    auto *store = LargeFileImageStore::instance();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LargeFileImageStore::Settings s = store->settings();
    s.enabled = true;
    s.directory = dir.path();
    s.batchSize = 10;
    QString error;
    QVERIFY2(store->setSettings(s, &error), qPrintable(error));
    store->flush();   // 清掉前面用例可能留下的待写

    // ① 无待写
    QCOMPARE(store->flush(&error), LargeFileImageStore::FlushResult::NothingToFlush);
    QVERIFY(error.isEmpty());

    // ② 有数据 → 写成功
    cv::Mat img(8, 8, CV_8UC3, cv::Scalar(1, 2, 3));
    QVERIFY(store->append(img, QStringLiteral("t"), nullptr, &error));
    QCOMPARE(store->flush(&error), LargeFileImageStore::FlushResult::Written);
    QVERIFY(error.isEmpty());

    // ③ 写失败：把目录改成不存在且不可创建的路径，再塞一张图
    LargeFileImageStore::Settings bad = s;
    bad.directory = dir.path() + QStringLiteral("/nonexistent_root/x");
    // setSettings 在无待写时允许设置无效目录（只在启用且为空时拒绝），flush 时 mkpath 可能成功；
    // 用"路径指向一个已存在的文件"来确保 mkpath 失败。
    const QString blocker = dir.path() + QStringLiteral("/blocker");
    QFile bf(blocker);
    QVERIFY(bf.open(QIODevice::WriteOnly));
    bf.close();
    bad.directory = blocker + QStringLiteral("/sub");
    QVERIFY2(store->setSettings(bad, &error), qPrintable(error));
    QVERIFY(store->append(img, QStringLiteral("t2"), nullptr, &error));
    QCOMPARE(store->flush(&error), LargeFileImageStore::FlushResult::Failed);
    QVERIFY2(!error.isEmpty(), "写失败必须给出原因");

    store->setSettings(m_original, &error);
}

void LargeFileStoreTest::readBatchRejectsOversizedEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bad = dir.path() + QStringLiteral("/bad.dat");
    // 合法文件头 + 1 条 + 名称长度 0 + 数据长度声明为 0x7FFFFFFF（2GB），但文件里没有数据体
    QFile f(bad);
    QVERIFY(f.open(QIODevice::WriteOnly));
    const char magic[8] = {'V','F','P','I','M','G','0','1'};
    f.write(magic, 8);
    const quint32 count = 1;
    const quint32 nameLen = 0;
    const quint32 dataLen = 0x7FFFFFFFu;
    f.write(reinterpret_cast<const char *>(&count), 4);
    f.write(reinterpret_cast<const char *>(&nameLen), 4);
    f.write(reinterpret_cast<const char *>(&dataLen), 4);
    f.close();

    QVector<LargeImageEntry> entries;
    QString error;
    // P10：必须在分配前拒绝，而不是尝试分配 2GB
    QVERIFY2(!LargeFileImageStore::readBatch(bad, &entries, &error), qPrintable(error));
    QVERIFY2(!error.isEmpty(), "坏文件必须给出原因");
    QVERIFY(entries.isEmpty());
}

QTEST_MAIN(LargeFileStoreTest)
#include "large_file_store_test.moc"
