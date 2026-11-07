#pragma once

#include <QImage>
#include <QSize>
#include <QString>

/// 标定板图案类型（对应相机标定常用的两类平面靶标）
enum class CalibrationBoardType {
    Chessboard,   ///< 棋盘格：黑白交替方格；OpenCV findChessboardCorners 的内角点数 = (列数-1, 行数-1)
    CirclesGrid   ///< 圆点阵列：白底黑圆，相邻圆心间距 = 格距
};

/// 标定板生成参数（物理尺寸与打印分辨率）
///
/// 尺寸约定：整板物理宽 = squaresX × squareSizeMm + 2 × marginMm（高同理）。
/// 像素尺寸先把「格距」与「留白」各自换算取整，再合成整板——保证所有方格等宽，
/// 避免逐格四舍五入造成的格宽抖动；pixelSize() 与 render() 共用同一算法，尺寸严格一致。
struct CalibrationBoardSpec {
    CalibrationBoardType type = CalibrationBoardType::Chessboard;
    int squaresX = 11;            ///< 横向方格数 / 圆点数（3..100）
    int squaresY = 8;             ///< 纵向方格数 / 圆点数（3..100）
    double squareSizeMm = 10.0;   ///< 方格边长 / 相邻圆点间距（mm，0.1..1000）
    double marginMm = 10.0;       ///< 四周留白（mm，0..1000）
    int dpi = 300;                ///< 打印分辨率（72..1200）
};

/// 标定板生成器（纯函数面，不依赖主窗口 / 场景，便于单测）
class CalibrationBoard final {
public:
    /// 参数校验；合法返回 true。否则返回 false 且 error 给出可直接展示的中文原因
    /// （error 可为 nullptr）。
    static bool validate(const CalibrationBoardSpec &spec, QString *error = nullptr);

    /// 整板像素尺寸（按 render() 同一取整算法）；参数非法返回空 QSize。
    static QSize pixelSize(const CalibrationBoardSpec &spec, QString *error = nullptr);

    /// 渲染整板：白底，棋盘格左上角为黑格；圆点板为白底黑圆（直径 = 格距 / 2）。
    /// 参数非法或图像分配失败返回空 QImage。
    static QImage render(const CalibrationBoardSpec &spec, QString *error = nullptr);

    /// 渲染并保存为图片文件：无扩展名默认 .png；有损格式按最高质量写入
    /// （标定板建议用 PNG/BMP，避免 JPEG 压缩影响角点精度）。
    /// 失败返回 false 且 error 含具体上下文（空路径 / 目录不存在 / 写入失败）。
    static bool save(const QString &filePath, const CalibrationBoardSpec &spec,
                     QString *error = nullptr);
};
