#include "CalibrationBoard.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QRect>
#include <QtMath>

namespace {

/// 1 英寸 = 25.4 mm（打印单位换算的唯一常量来源）
constexpr double kMillimetersPerInch = 25.4;

/// 参数边界（与 MainWindow 标定板对话框的控件范围保持一致）
constexpr int kMinSquares = 3;
constexpr int kMaxSquares = 100;
constexpr double kMinSquareSizeMm = 0.1;
constexpr double kMaxSquareSizeMm = 1000.0;
constexpr double kMaxMarginMm = 1000.0;
constexpr int kMinDpi = 72;
constexpr int kMaxDpi = 1200;

/// 输出像素上限：防止误填参数把内存打满（50 MP 灰度图约 50 MB）
constexpr int kMinDimensionPx = 16;
constexpr int kMaxDimensionPx = 20000;
constexpr qint64 kMaxPixelCount = 50000000LL;

/// 毫米 → 像素（四舍五入；调用前参数范围已校验，不会为负）
int toPixels(double millimeters, int dpi)
{
    return qRound(millimeters * dpi / kMillimetersPerInch);
}

/// 已通过范围校验的参数 → 像素尺寸。
/// render() 与 pixelSize() 共用此函数，保证「声明的尺寸」与「实际产出的尺寸」恒等。
QSize metricsToPixelSize(const CalibrationBoardSpec &spec)
{
    const int squarePx = toPixels(spec.squareSizeMm, spec.dpi);
    const int marginPx = toPixels(spec.marginMm, spec.dpi);
    return QSize(marginPx * 2 + spec.squaresX * squarePx,
                 marginPx * 2 + spec.squaresY * squarePx);
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

bool CalibrationBoard::validate(const CalibrationBoardSpec &spec, QString *error)
{
    // 类型：只接受已定义的两种图案；越界枚举按未知类型拒绝
    switch (spec.type) {
    case CalibrationBoardType::Chessboard:
    case CalibrationBoardType::CirclesGrid:
        break;
    default:
        setError(error, QStringLiteral("未知的标定板类型"));
        return false;
    }

    if (spec.squaresX < kMinSquares || spec.squaresX > kMaxSquares) {
        setError(error, QStringLiteral("横向方格/圆点数必须在 %1..%2 之间（当前 %3）")
                         .arg(kMinSquares)
                         .arg(kMaxSquares)
                         .arg(spec.squaresX));
        return false;
    }
    if (spec.squaresY < kMinSquares || spec.squaresY > kMaxSquares) {
        setError(error, QStringLiteral("纵向方格/圆点数必须在 %1..%2 之间（当前 %3）")
                         .arg(kMinSquares)
                         .arg(kMaxSquares)
                         .arg(spec.squaresY));
        return false;
    }
    // 浮点先判有限性：NaN 与所有比较都为 false，会漏过下面的区间检查
    if (!qIsFinite(spec.squareSizeMm) || spec.squareSizeMm < kMinSquareSizeMm
        || spec.squareSizeMm > kMaxSquareSizeMm) {
        setError(error, QStringLiteral("格距必须在 %1..%2 mm 之间（当前 %3）")
                         .arg(kMinSquareSizeMm)
                         .arg(kMaxSquareSizeMm)
                         .arg(spec.squareSizeMm));
        return false;
    }
    if (!qIsFinite(spec.marginMm) || spec.marginMm < 0.0 || spec.marginMm > kMaxMarginMm) {
        setError(error, QStringLiteral("留白必须在 0..%1 mm 之间（当前 %2）")
                         .arg(kMaxMarginMm)
                         .arg(spec.marginMm));
        return false;
    }
    if (spec.dpi < kMinDpi || spec.dpi > kMaxDpi) {
        setError(error, QStringLiteral("DPI 必须在 %1..%2 之间（当前 %3）")
                         .arg(kMinDpi)
                         .arg(kMaxDpi)
                         .arg(spec.dpi));
        return false;
    }

    const QSize size = metricsToPixelSize(spec);
    if (size.width() < kMinDimensionPx || size.height() < kMinDimensionPx) {
        setError(error, QStringLiteral("生成的图像过小（%1×%2 px，下限 %3 px）：请增大格距、方格数或 DPI")
                         .arg(size.width())
                         .arg(size.height())
                         .arg(kMinDimensionPx));
        return false;
    }
    if (size.width() > kMaxDimensionPx || size.height() > kMaxDimensionPx
        || static_cast<qint64>(size.width()) * size.height() > kMaxPixelCount) {
        setError(error,
                 QStringLiteral("生成的图像过大（%1×%2 px）：单边上限 %3 px、总量上限 5000 万像素，"
                                "请减小格距、方格数或 DPI")
                     .arg(size.width())
                     .arg(size.height())
                     .arg(kMaxDimensionPx));
        return false;
    }
    return true;
}

QSize CalibrationBoard::pixelSize(const CalibrationBoardSpec &spec, QString *error)
{
    if (!validate(spec, error))
        return QSize();
    return metricsToPixelSize(spec);
}

QImage CalibrationBoard::render(const CalibrationBoardSpec &spec, QString *error)
{
    if (!validate(spec, error))
        return QImage();

    const int squarePx = toPixels(spec.squareSizeMm, spec.dpi);
    const int marginPx = toPixels(spec.marginMm, spec.dpi);
    const QSize size = metricsToPixelSize(spec);

    // 灰度 8 位：打印用图无需彩色，内存占用是 RGB32 的 1/4
    QImage image(size, QImage::Format_Grayscale8);
    if (image.isNull()) {
        setError(error, QStringLiteral("无法分配 %1×%2 的标定板图像（内存不足）")
                            .arg(size.width())
                            .arg(size.height()));
        return QImage();
    }
    image.fill(0xFF);   // 白底：四周留白与棋盘白格共用

    QPainter painter(&image);
    if (!painter.isActive()) {
        setError(error, QStringLiteral("无法在灰度图上绘制标定板图案"));
        return QImage();
    }
    painter.setPen(Qt::NoPen);

    if (spec.type == CalibrationBoardType::Chessboard) {
        // 方格边缘必须落在整像素上：关闭抗锯齿，避免相邻黑白块之间出现灰缝
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setBrush(Qt::black);
        for (int y = 0; y < spec.squaresY; ++y) {
            for (int x = 0; x < spec.squaresX; ++x) {
                // 左上角 (0,0) 为黑格，向右/向下黑白交替；白格由白底承担
                if (((x + y) & 1) != 0)
                    continue;
                painter.drawRect(QRect(marginPx + x * squarePx,
                                       marginPx + y * squarePx, squarePx, squarePx));
            }
        }
    } else {
        // 圆点需要抗锯齿：否则低 DPI 下圆心边缘成阶梯状，影响找圆精度
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::black);
        // 直径取格距的一半（与 OpenCV 圆点标定板的常用比例一致）
        const int diameterPx = qMax(1, qRound(squarePx * 0.5));
        for (int y = 0; y < spec.squaresY; ++y) {
            for (int x = 0; x < spec.squaresX; ++x) {
                const double centerX = marginPx + (x + 0.5) * squarePx;
                const double centerY = marginPx + (y + 0.5) * squarePx;
                painter.drawEllipse(QPointF(centerX, centerY),
                                    diameterPx / 2.0, diameterPx / 2.0);
            }
        }
    }
    painter.end();
    return image;
}

bool CalibrationBoard::save(const QString &filePath, const CalibrationBoardSpec &spec,
                            QString *error)
{
    if (filePath.trimmed().isEmpty()) {
        setError(error, QStringLiteral("保存路径为空"));
        return false;
    }

    QString path = filePath;
    QFileInfo info(path);
    if (info.suffix().isEmpty()) {
        // 未给扩展名时默认 PNG（无损，适合标定板）
        path += QStringLiteral(".png");
        info.setFile(path);
    }

    if (!info.dir().exists()) {
        setError(error, QStringLiteral("目标目录不存在：%1")
                             .arg(QDir::toNativeSeparators(info.absolutePath())));
        return false;
    }

    const QImage image = render(spec, error);
    if (image.isNull())
        return false;   // render() 已写入具体原因

    // 有损格式按最高质量写入；PNG/BMP 忽略质量参数
    if (!image.save(path, nullptr, 100)) {
        setError(error, QStringLiteral("写入图片失败：%1（请确认目录可写且扩展名受支持）")
                             .arg(QDir::toNativeSeparators(path)));
        return false;
    }
    return true;
}
