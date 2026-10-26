#include "OpencvUtil.h"
#include <halconcpp/HalconCpp.h>
#include <opencv2/imgproc.hpp>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace HalconCpp;

namespace OpencvUtil {

// HALCON 像素类型串 → OpenCV 深度（2026-10-25 复核 P1-1：原实现取了 type 却直接按
// CV_8UC1 包像素——uint2（Mono10/12/16、16 位图像文件）会被拆成字节对，45 个调用方
// 全部在错图上跑且判绿）。
static int halconTypeToCvDepth(const QByteArray &type)
{
    if (type == "byte") return CV_8U;
    if (type == "int1") return CV_8S;
    if (type == "uint2") return CV_16U;
    if (type == "int2") return CV_16S;
    if (type == "int4") return CV_32S;
    if (type == "real") return CV_32F;
    return -1;
}

static const char *cvDepthToHalconType(int depth)
{
    switch (depth) {
    case CV_8U:  return "byte";
    case CV_8S:  return "int1";
    case CV_16U: return "uint2";
    case CV_16S: return "int2";
    case CV_32S: return "int4";
    case CV_32F: return "real";
    default:     return nullptr;
    }
}

cv::Mat himageToMat(const HImage &img)
{
    try {
        HTuple width, height;
        GetImageSize(img, &width, &height);
        const int w = width.I();
        const int h = height.I();

        HTuple ch;
        CountChannels(img, &ch);

        HTuple type, pointer;
        if (ch.I() <= 1) {
            GetImagePointer1(img, &pointer, &type, &width, &height);
            const int depth = halconTypeToCvDepth(QByteArray(type.S()));
            if (depth < 0) return cv::Mat();
            const uchar *ptr = reinterpret_cast<const uchar *>(pointer.L());
            if (!ptr) return cv::Mat();
            // 共享像素内存：行对齐按 HALCON 惯例为偶数对齐，多数情况与 Mat 连续一致
            return cv::Mat(h, w, CV_MAKETYPE(depth, 1), const_cast<uchar *>(ptr)).clone();
        }
        // 三通道（RGB）
        HTuple pR, pG, pB;
        GetImagePointer3(img, &pR, &pG, &pB, &type, &width, &height);
        const int depth = halconTypeToCvDepth(QByteArray(type.S()));
        if (depth < 0) return cv::Mat();
        const size_t elem = static_cast<size_t>(CV_ELEM_SIZE(depth));
        const uchar *r = reinterpret_cast<const uchar *>(pR.L());
        const uchar *g = reinterpret_cast<const uchar *>(pG.L());
        const uchar *b = reinterpret_cast<const uchar *>(pB.L());
        if (!r || !g || !b) return cv::Mat();
        cv::Mat rgb(h, w, CV_MAKETYPE(depth, 3));
        // HALCON RGB 平面 → OpenCV BGR 交织（按元素宽度搬运，不再假定 1 字节）
        for (int y = 0; y < h; ++y) {
            const uchar *pr = r + static_cast<size_t>(y) * w * elem;
            const uchar *pg = g + static_cast<size_t>(y) * w * elem;
            const uchar *pb = b + static_cast<size_t>(y) * w * elem;
            uchar *dst = rgb.ptr<uchar>(y);
            for (int x = 0; x < w; ++x) {
                memcpy(dst + (static_cast<size_t>(x) * 3 + 0) * elem, pb + x * elem, elem);
                memcpy(dst + (static_cast<size_t>(x) * 3 + 1) * elem, pg + x * elem, elem);
                memcpy(dst + (static_cast<size_t>(x) * 3 + 2) * elem, pr + x * elem, elem);
            }
        }
        return rgb;
    } catch (const HException &) {
        return cv::Mat();
    }
}

HImage matToHimage(const cv::Mat &matIn)
{
    if (matIn.empty()) return HImage();
    cv::Mat mat = matIn;
    // 深度映射到 HALCON 类型（2026-10-25 复核 P2-1）：原实现把一切非 8UC1/3 都 convertTo
    // CV_8U（scale=1.0，**饱和截断**，注释写"归一化"是错的）——ConvertImageNode 的六个
    // 目标类型全部变成 byte，0/1000/60000 → 0/255/255（真探针实测）。现在保留深度。
    int depth = mat.depth();
    const char *halType = cvDepthToHalconType(depth);
    if (!halType) {
        cv::Mat tmp;
        if (depth == CV_64F) {
            mat.convertTo(tmp, CV_32F);          // 64F → 32F（值域保留）
        } else {
            mat.convertTo(tmp, CV_8U);           // 其余不支持的深度：饱和截断到 byte
        }
        mat = tmp;
        depth = mat.depth();
        halType = cvDepthToHalconType(depth);
    }
    if (!halType) return HImage();
    // 保证连续，避免行对齐问题
    if (!mat.isContinuous()) mat = mat.clone();

    try {
        const size_t elem = static_cast<size_t>(CV_ELEM_SIZE(depth));
        if (mat.channels() == 1) {
            HImage img;
            GenImage1(&img, halType, mat.cols, mat.rows,
                      reinterpret_cast<Hlong>(mat.data));
            return img;
        }
        if (mat.channels() == 3) {
            // BGR 交织 → HALCON RGB 平面
            const int w = mat.cols;
            const int h = mat.rows;
            std::vector<uchar> r(static_cast<size_t>(h) * w * elem);
            std::vector<uchar> g(static_cast<size_t>(h) * w * elem);
            std::vector<uchar> b(static_cast<size_t>(h) * w * elem);
            for (int y = 0; y < h; ++y) {
                const uchar *src = mat.ptr<uchar>(y);
                uchar *pr = r.data() + static_cast<size_t>(y) * w * elem;
                uchar *pg = g.data() + static_cast<size_t>(y) * w * elem;
                uchar *pb = b.data() + static_cast<size_t>(y) * w * elem;
                for (int x = 0; x < w; ++x) {
                    memcpy(pb + x * elem, src + (static_cast<size_t>(x) * 3 + 0) * elem, elem);
                    memcpy(pg + x * elem, src + (static_cast<size_t>(x) * 3 + 1) * elem, elem);
                    memcpy(pr + x * elem, src + (static_cast<size_t>(x) * 3 + 2) * elem, elem);
                }
            }
            HImage img;
            GenImage3(&img, halType, mat.cols, mat.rows,
                      reinterpret_cast<Hlong>(r.data()),
                      reinterpret_cast<Hlong>(g.data()),
                      reinterpret_cast<Hlong>(b.data()));
            return img;
        }
        return HImage();
    } catch (const HException &) {
        return HImage();
    }
}

bool isImageValid(const HImage &img)
{
    try {
        if (!img.IsInitialized()) return false;
        HTuple w, h;
        GetImageSize(img, &w, &h);
        if (w.I() <= 0 || h.I() <= 0) return false;
        HTuple pointer, type;
        GetImagePointer1(img, &pointer, &type, &w, &h);
        return pointer.L() != 0;
    } catch (const HException &) {
        return false;
    }
}

bool requireGray8(const cv::Mat &mat, QString *why)
{
    if (mat.empty()) {
        if (why) *why = QStringLiteral("输入图像为空");
        return false;
    }
    if (mat.depth() == CV_8U)
        return true;
    if (why) {
        const int bits = CV_ELEM_SIZE(mat.depth()) * 8;
        // 补救路径必须写对（2026-10-26 复核）：早先指向「类型转换」的 byte 目标，那是
        // convertTo(CV_8U)（scale=1，**饱和截断**）——12 位图会整片变 255。正确做法是先
        // 「灰度拉伸」把值域压到 0-255 再转 8 位。
        *why = QStringLiteral(
                   "该算子只支持 8 位图（当前 %1 位/通道、%2 通道）："
                   "请在前级先接「灰度拉伸」把值域压到 0-255，再接「类型转换」转为 8 位"
                   "（直接转 byte 会饱和截断成一片白）；或改用支持该位深的算子")
                   .arg(bits)
                   .arg(mat.channels());
    }
    return false;
}

void applyGrayMask(cv::Mat &bin, const QImage &mask)
{
    if (bin.empty() || mask.isNull())
        return;
    QImage gray = mask;
    if (gray.format() != QImage::Format_Grayscale8)
        gray = gray.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat src(gray.height(), gray.width(), CV_8UC1,
                const_cast<uchar *>(gray.constBits()), gray.bytesPerLine());
    cv::Mat resized;
    if (src.size() != bin.size())
        cv::resize(src, resized, bin.size(), 0, 0, cv::INTER_NEAREST);
    else
        resized = src;
    cv::bitwise_and(bin, resized, bin);
}

namespace {

/// 旋转估计：**多角度试探 + 平移相关响应择优**，返回"当前图相对参考图"的旋转角（度），
/// 正负沿用 cv::getRotationMatrix2D 的约定（正值 = 逆时针），符号由 OpencvDefectAlignTest 钉住。
///
/// 为什么不用"对数极坐标 + 相位相关"：cv::warpPolar/warpLogPolar 的角度轴约定极易搞错，
/// 而且**错了不报错、只是静默返回≈0 的估计值**（实测把 2° 旋转估成 0.07°，表面看"没报错"）。
/// 改用本方案后只用已经钉住符号约定的 phaseCorrelate，且天然带置信度（响应值可喂给同一套闸门）。
///
/// 性能：在长边 ≤512 的缩略图上按 0.5° 步长搜索（旋转估计对分辨率不敏感，对耗时极敏感——
/// 2448×2048 原图做 FFT 相关是几十~上百毫秒量级，缩略后整轮搜索只要几十毫秒）。
/// 代价：不做尺度归一化（调用方不处理缩放）。
double estimateRotationSearch(const cv::Mat &goldF, const cv::Mat &curF, double rangeDeg)
{
    const int longSide = std::max(goldF.cols, goldF.rows);
    const double s = (longSide > 512) ? (512.0 / longSide) : 1.0;
    cv::Mat g, c;
    cv::resize(goldF, g, cv::Size(), s, s, cv::INTER_AREA);
    cv::resize(curF, c, cv::Size(), s, s, cv::INTER_AREA);
    if (g.empty() || c.empty() || g.size() != c.size())
        return 0.0;
    const cv::Point2d center(g.cols / 2.0, g.rows / 2.0);

    double bestAngle = 0.0, bestResp = -1.0;
    const double step = 0.5;
    for (double a = -rangeDeg; a <= rangeDeg + 1e-9; a += step) {
        const cv::Mat R = cv::getRotationMatrix2D(center, -a, 1.0);   // 试：按 -a 把当前图转回
        cv::Mat t;
        cv::warpAffine(c, t, R, c.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        double resp = 0.0;
        cv::phaseCorrelate(g, t, cv::noArray(), &resp);
        if (resp > bestResp) {
            bestResp = resp;
            bestAngle = a;
        }
    }
    return bestAngle;
}

/// 8 位灰度图转 32F（phaseCorrelate 要求浮点）
cv::Mat toFloatGray(const cv::Mat &m)
{
    cv::Mat f;
    m.convertTo(f, CV_32F);
    return f;
}

}   // namespace

AlignInfo alignToReference(const cv::Mat &ref, const cv::Mat &cur, int mode,
                           int maxShift, double minResponse, double angleRangeDeg,
                           cv::Mat &aligned)
{
    AlignInfo info;
    aligned = cur.clone();
    if (mode <= 0 || ref.empty() || cur.empty() || ref.size() != cur.size())
        return info;

    const cv::Mat goldF = toFloatGray(ref);
    const cv::Mat inspF = toFloatGray(cur);
    const cv::Point2d center(cur.cols / 2.0, cur.rows / 2.0);

    // ① 旋转（仅"平移+旋转"模式）：绕图像中心估，旋转会带动内容，故平移必须在其后重估
    double angleEst = 0.0;
    if (mode >= 2)
        angleEst = estimateRotationSearch(goldF, inspF, angleRangeDeg);

    // ② 先按估计角把当前图转回，再求残余平移（亚像素）
    cv::Mat deRotated = inspF;
    if (std::abs(angleEst) > 1e-6) {
        const cv::Mat R = cv::getRotationMatrix2D(center, -angleEst, 1.0);
        cv::warpAffine(inspF, deRotated, R, inspF.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    }
    double resp = 0.0;
    const cv::Point2d shift = cv::phaseCorrelate(goldF, deRotated, cv::noArray(), &resp);

    // 注意 phaseCorrelate 是**循环相关**：位移超过图像边长一半会折回（例如 80 px ≡ -48 px），
    // 因此 maxShift 只能拦住 (maxShift, 边长/2] 区间的估计值；把上限设成"工件可能的最大位移"
    // 即可（折回区间通常已在物理上不可能）。另外折回后的对齐"碰巧"正确也无害。
    info.response = resp;
    if (resp < minResponse || std::abs(shift.x) > maxShift || std::abs(shift.y) > maxShift)
        return info;

    // ③ 旋转 + 平移合成一次仿射，避免二次插值损失
    cv::Mat M = cv::getRotationMatrix2D(center, -angleEst, 1.0);
    M.at<double>(0, 2) -= shift.x;
    M.at<double>(1, 2) -= shift.y;
    cv::Mat alignedF;
    cv::warpAffine(inspF, alignedF, M, inspF.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    alignedF.convertTo(aligned, cur.type());
    info.applied = true;
    info.dx = shift.x;
    info.dy = shift.y;
    info.angle = angleEst;
    return info;
}

} // namespace OpencvUtil
