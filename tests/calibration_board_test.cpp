// CalibrationBoardTest（P2 工具菜单「标定板生成」数据面）：
// 像素尺寸换算、棋盘格黑白交替、圆点阵列几何、非法参数拒绝、保存往返与错误上下文。
// 只走 QImage/QPainter 像素采样，不进 MainWindow；QTEST_MAIN 链接 Widgets 后自动建 QApplication。
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

#include "CalibrationBoard.h"

class CalibrationBoardTest : public QObject
{
    Q_OBJECT

private slots:
    void testPixelSizeExact();          // mm + DPI → 像素尺寸恒等
    void testChessboardAlternation();   // 黑白交替 + 留白 + 硬边界
    void testCircleGridGeometry();      // 圆心黑 / 格角白 / 黑像素占比
    void testInvalidParamsRejected();   // 越界、NaN、过小、过大
    void testSaveRoundTrip();           // 无扩展名补 .png、尺寸回读一致
    void testSaveErrors();              // 空路径 / 目录不存在 / 扩展名不支持
};

namespace {

/// 10 px/mm 的 DPI：254 dpi 下 1 mm 恰好 10 px，尺寸断言可以精确到像素
constexpr int kExactDpi = 254;

CalibrationBoardSpec chessboardSpec()
{
    CalibrationBoardSpec spec;
    spec.type = CalibrationBoardType::Chessboard;
    spec.squaresX = 11;
    spec.squaresY = 8;
    spec.squareSizeMm = 10.0;
    spec.marginMm = 10.0;
    spec.dpi = kExactDpi;
    return spec;
}

} // namespace

void CalibrationBoardTest::testPixelSizeExact()
{
    const CalibrationBoardSpec spec = chessboardSpec();

    QString error;
    const QSize size = CalibrationBoard::pixelSize(spec, &error);
    QVERIFY2(!size.isEmpty(), qPrintable(error));
    // 宽 = (11 格 × 10 mm + 2 × 10 mm 留白) × 10 px/mm = 1300 px；高 = (8 格 × 10 + 20) × 10 = 1000 px
    QCOMPARE(size, QSize(1300, 1000));

    const QImage image = CalibrationBoard::render(spec, &error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QCOMPARE(image.size(), size);
    QCOMPARE(image.format(), QImage::Format_Grayscale8);
}

void CalibrationBoardTest::testChessboardAlternation()
{
    const CalibrationBoardSpec spec = chessboardSpec();
    const QImage image = CalibrationBoard::render(spec);
    QVERIFY(!image.isNull());

    const int squarePx = 100;   // 10 mm × 10 px/mm
    const int marginPx = 100;   // 10 mm × 10 px/mm

    // 全板逐格采样：左上角 (0,0) 为黑，相邻格黑白交替
    for (int y = 0; y < spec.squaresY; ++y) {
        for (int x = 0; x < spec.squaresX; ++x) {
            const int cx = marginPx + x * squarePx + squarePx / 2;
            const int cy = marginPx + y * squarePx + squarePx / 2;
            const int expected = (((x + y) & 1) == 0) ? 0 : 255;
            const int actual = qGray(image.pixel(cx, cy));
            QVERIFY2(actual == expected,
                     qPrintable(QStringLiteral("格 (%1,%2) 中心灰度为 %3，期望 %4")
                                    .arg(x)
                                    .arg(y)
                                    .arg(actual)
                                    .arg(expected)));
        }
    }

    // 四周留白必须是白色（含图像四角）
    QCOMPARE(qGray(image.pixel(0, 0)), 255);
    QCOMPARE(qGray(image.pixel(image.width() - 1, image.height() - 1)), 255);
    QCOMPARE(qGray(image.pixel(50, 50)), 255);

    // 硬边界：留白最后一列像素仍白，方格第一列像素即黑（不允许抗锯齿灰缝）
    QCOMPARE(qGray(image.pixel(marginPx - 1, marginPx - 1)), 255);
    QCOMPARE(qGray(image.pixel(marginPx, marginPx)), 0);
}

void CalibrationBoardTest::testCircleGridGeometry()
{
    CalibrationBoardSpec spec = chessboardSpec();
    spec.type = CalibrationBoardType::CirclesGrid;
    spec.squaresX = 6;
    spec.squaresY = 4;

    const QImage image = CalibrationBoard::render(spec);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(800, 600));   // (6×10 + 20) × 10 = 800；高 600

    const int squarePx = 100;
    const int marginPx = 100;

    for (int y = 0; y < spec.squaresY; ++y) {
        for (int x = 0; x < spec.squaresX; ++x) {
            // 圆心：黑
            const int cx = marginPx + x * squarePx + squarePx / 2;
            const int cy = marginPx + y * squarePx + squarePx / 2;
            const int centerGray = qGray(image.pixel(cx, cy));
            QVERIFY2(centerGray == 0,
                     qPrintable(QStringLiteral("圆点 (%1,%2) 圆心灰度为 %3，期望 0")
                                    .arg(x)
                                    .arg(y)
                                    .arg(centerGray)));

            // 格子左上角（圆心左上约 70 px，圆心外半径 25 px）：白
            const int cornerGray = qGray(image.pixel(marginPx + x * squarePx + 5,
                                                     marginPx + y * squarePx + 5));
            QVERIFY2(cornerGray == 255,
                     qPrintable(QStringLiteral("格 (%1,%2) 角灰度为 %3，期望 255")
                                    .arg(x)
                                    .arg(y)
                                    .arg(cornerGray)));
        }
    }

    // 黑像素占比：每格圆面积 π×(25 px)² ≈ 1963 px，24 格约占整图 480,000 px 的 9.8%
    int darkCount = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qGray(image.pixel(x, y)) < 128)
                ++darkCount;
        }
    }
    const double ratio =
        static_cast<double>(darkCount) / (static_cast<double>(image.width()) * image.height());
    QVERIFY2(ratio > 0.085 && ratio < 0.115,
             qPrintable(QStringLiteral("圆点黑像素占比 %1 超出 [0.085, 0.115]").arg(ratio)));
}

void CalibrationBoardTest::testInvalidParamsRejected()
{
    auto base = []() {
        CalibrationBoardSpec spec;
        spec.dpi = kExactDpi;
        return spec;
    };

    QVector<CalibrationBoardSpec> cases;

    CalibrationBoardSpec smallX = base();
    smallX.squaresX = 2;   // 下限 3
    cases.append(smallX);

    CalibrationBoardSpec bigY = base();
    bigY.squaresY = 101;   // 上限 100
    cases.append(bigY);

    CalibrationBoardSpec zeroSize = base();
    zeroSize.squareSizeMm = 0.0;
    cases.append(zeroSize);

    CalibrationBoardSpec nanSize = base();
    nanSize.squareSizeMm = qQNaN();   // NaN 必须被显式拦下（区间比较对它恒为 false）
    cases.append(nanSize);

    CalibrationBoardSpec negativeMargin = base();
    negativeMargin.marginMm = -1.0;
    cases.append(negativeMargin);

    CalibrationBoardSpec bigMargin = base();
    bigMargin.marginMm = 1001.0;
    cases.append(bigMargin);

    CalibrationBoardSpec lowDpi = base();
    lowDpi.dpi = 71;
    cases.append(lowDpi);

    CalibrationBoardSpec highDpi = base();
    highDpi.dpi = 1201;
    cases.append(highDpi);

    CalibrationBoardSpec unknownType = base();
    unknownType.type = static_cast<CalibrationBoardType>(99);
    cases.append(unknownType);

    CalibrationBoardSpec tooLarge = base();
    tooLarge.squaresX = 100;
    tooLarge.squaresY = 100;
    tooLarge.squareSizeMm = 1000.0;
    tooLarge.marginMm = 0.0;
    tooLarge.dpi = 1200;   // 单边约 472 万像素，超过 20000 px 上限
    cases.append(tooLarge);

    CalibrationBoardSpec tooSmall = base();
    tooSmall.squaresX = 3;
    tooSmall.squaresY = 3;
    tooSmall.squareSizeMm = 0.1;
    tooSmall.marginMm = 0.0;
    tooSmall.dpi = 72;   // 整板 3 px，低于 16 px 下限
    cases.append(tooSmall);

    // 格距换算下溢为 0（0.1mm@72dpi → qRound(0.283)=0），但大留白让整板
    // 56×56 px 通过旧的尺寸上下限：旧实现会生成全白图/圆心全重合并「保存成功」
    CalibrationBoardSpec zeroPitch = base();
    zeroPitch.squaresX = 3;
    zeroPitch.squaresY = 3;
    zeroPitch.squareSizeMm = 0.1;
    zeroPitch.marginMm = 10.0;
    zeroPitch.dpi = 72;
    cases.append(zeroPitch);

    for (const CalibrationBoardSpec &spec : cases) {
        QString error;
        QVERIFY2(!CalibrationBoard::validate(spec, &error),
                 qPrintable(QStringLiteral("非法参数未被拒绝：%1×%2 %3mm margin=%4 dpi=%5 type=%6")
                                .arg(spec.squaresX)
                                .arg(spec.squaresY)
                                .arg(spec.squareSizeMm)
                                .arg(spec.marginMm)
                                .arg(spec.dpi)
                                .arg(static_cast<int>(spec.type))));
        QVERIFY2(!error.isEmpty(), "非法参数未给出错误原因");
        QVERIFY(CalibrationBoard::pixelSize(spec, &error).isEmpty());
        QVERIFY(CalibrationBoard::render(spec, &error).isNull());
        QVERIFY(!error.isEmpty());
    }

    // 合法边界：留白 0、DPI 下限、最小方格数仍应通过
    CalibrationBoardSpec boundary = base();
    boundary.squaresX = 3;
    boundary.squaresY = 3;
    boundary.squareSizeMm = 100.0;
    boundary.marginMm = 0.0;
    boundary.dpi = 72;   // 每格 100 mm × 72/25.4 ≈ 283.46 → 283 px，整板 3 格 = 849 px
    QString error;
    QVERIFY2(CalibrationBoard::validate(boundary, &error), qPrintable(error));
    // 逐格取整是刻意设计：整板所有方格必须等宽（若按整板取整会出现格宽不一致）
    QCOMPARE(CalibrationBoard::pixelSize(boundary), QSize(849, 849));
}

void CalibrationBoardTest::testSaveRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "无法创建临时目录");

    const CalibrationBoardSpec spec = chessboardSpec();
    const QString pathWithoutSuffix = dir.filePath(QStringLiteral("board"));

    QString error;
    QVERIFY2(CalibrationBoard::save(pathWithoutSuffix, spec, &error), qPrintable(error));

    const QString pngPath = dir.filePath(QStringLiteral("board.png"));
    QVERIFY2(QFile::exists(pngPath), "未自动补 .png 扩展名");

    const QImage loaded(pngPath);
    QVERIFY2(!loaded.isNull(), "保存后的 PNG 无法回读");
    QCOMPARE(loaded.size(), CalibrationBoard::pixelSize(spec));
    // 左上黑格中心（留白 100 px + 格中心 50 px）
    QCOMPARE(qGray(loaded.pixel(150, 150)), 0);
    // 四角留白仍为白
    QCOMPARE(qGray(loaded.pixel(0, 0)), 255);
}

void CalibrationBoardTest::testSaveErrors()
{
    const CalibrationBoardSpec spec = chessboardSpec();

    QString error;
    QVERIFY(!CalibrationBoard::save(QString(), spec, &error));
    QVERIFY(!error.isEmpty());

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "无法创建临时目录");

    // 目录不存在：报错必须带上下文，不能静默返回
    error.clear();
    QVERIFY(!CalibrationBoard::save(dir.filePath(QStringLiteral("missing/board.png")),
                                    spec, &error));
    QVERIFY2(error.contains(QStringLiteral("目录不存在")), qPrintable(error));

    // 扩展名不受支持：QImage::save 失败后必须转成可读原因
    error.clear();
    QVERIFY(!CalibrationBoard::save(dir.filePath(QStringLiteral("board.xyz")), spec, &error));
    QVERIFY2(error.contains(QStringLiteral("写入图片失败")), qPrintable(error));
}

QTEST_MAIN(CalibrationBoardTest)
#include "calibration_board_test.moc"
