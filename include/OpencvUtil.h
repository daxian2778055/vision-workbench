#pragma once

// 防止 Windows.h 的 min/max 宏破坏 OpenCV 头文件中的 std::min/std::max
// （主程序因 Qt 头文件已定义 NOMINMAX 而幸免；独立测试程序必须在此定义）
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <opencv2/core.hpp>
#include <QImage>

namespace HalconCpp {
class HImage;
}

namespace OpencvUtil {

/// HALCON HImage → cv::Mat（单通道/三通道 8bit；其他类型转为 CV_8U）
/// 替换版环境下 HALCON 图像层已实测正常（GetImagePointer1/3 读写一致）。
/// 返回的 Mat 与 HImage 共享像素内存（只读拷贝安全，写操作前请 clone）。
cv::Mat himageToMat(const HalconCpp::HImage &img);

/// cv::Mat → HALCON HImage（8bit 单/三通道；其他类型先转换）
HalconCpp::HImage matToHimage(const cv::Mat &mat);

/// 判断 HALCON 图像是否可用（含像素指针校验），用于桥接失败时的日志
bool isImageValid(const HalconCpp::HImage &img);

/// 非 8 位输入的显式策略（2026-10-25 复核 P2）：桥接修好后会如实返回 CV_16U（Mono10/12/16），
/// 而阈值的 0-255 区间、`at<uchar>` 访问、Tesseract/ZXing 的 8 位入口都按 8 位写死——不拦就是
/// 静默错值。本判据只放行 8 位（单/三通道），其余一律返回 false 并在 `why` 里给出可直接照做的
/// 建议（前级接「类型转换」）。**不在这里偷偷缩放**：缩放比例取决于相机位深与用途，猜错比拒绝更糟。
bool requireGray8(const cv::Mat &mat, QString *why);

/// 将灰度掩膜（白=保留）按最近邻缩放到 bin 尺寸后做按位与
void applyGrayMask(cv::Mat &bin, const QImage &mask);

/// 对齐结果（节点直接把这些值写进参数供回显与排查）
struct AlignInfo {
    bool applied = false;
    double dx = 0.0;
    double dy = 0.0;
    double angle = 0.0;      ///< 估计旋转角（度，正值=逆时针，沿用 getRotationMatrix2D 约定）
    double response = 0.0;   ///< 相位相关置信度
};

/// 把当前图对回参考图（G-P1-1 起用于黄金差影，G-P1-2 复用于异常检测）。
/// `mode`：0=不对齐 / 1=平移 / 2=平移+旋转。
/// `aligned` 一定拿到与 `cur` 同尺寸的图；闸门未通过（低置信度、位移超限、尺寸不符）时
/// 就是 `cur` 的拷贝而**不做任何变换**——低纹理图上"乱移"比"不移"危险得多（不移只是漏对齐，
/// 乱移会把整圈轮廓点亮成缺陷）。
AlignInfo alignToReference(const cv::Mat &ref, const cv::Mat &cur, int mode,
                           int maxShift, double minResponse, double angleRangeDeg,
                           cv::Mat &aligned);

} // namespace OpencvUtil
