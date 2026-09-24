#include "OpencvUtil.h"
#include <halconcpp/HalconCpp.h>
#include <opencv2/imgproc.hpp>
#include <QImage>
#include <algorithm>
#include <cmath>

using namespace HalconCpp;

namespace OpencvUtil {

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
            const uchar *ptr = reinterpret_cast<const uchar *>(pointer.L());
            if (!ptr) return cv::Mat();
            // 共享像素内存：行对齐按 HALCON 惯例为偶数对齐，多数情况与 Mat 连续一致
            return cv::Mat(h, w, CV_8UC1, const_cast<uchar *>(ptr)).clone();
        }
        // 三通道（RGB）
        HTuple pR, pG, pB;
        GetImagePointer3(img, &pR, &pG, &pB, &type, &width, &height);
        const uchar *r = reinterpret_cast<const uchar *>(pR.L());
        const uchar *g = reinterpret_cast<const uchar *>(pG.L());
        const uchar *b = reinterpret_cast<const uchar *>(pB.L());
        if (!r || !g || !b) return cv::Mat();
        cv::Mat rgb(h, w, CV_8UC3);
        // HALCON RGB 平面 → OpenCV BGR 交织
        for (int y = 0; y < h; ++y) {
            const uchar *pr = r + y * w;
            const uchar *pg = g + y * w;
            const uchar *pb = b + y * w;
            uchar *dst = rgb.ptr<uchar>(y);
            for (int x = 0; x < w; ++x) {
                dst[x * 3 + 0] = pb[x];
                dst[x * 3 + 1] = pg[x];
                dst[x * 3 + 2] = pr[x];
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
    cv::Mat mat;
    if (matIn.type() != CV_8UC1 && matIn.type() != CV_8UC3) {
        // 非 8bit 转 8bit（归一化到 0-255）
        cv::Mat tmp;
        matIn.convertTo(tmp, CV_8U, 255.0 / 255.0);
        mat = tmp;
    } else {
        mat = matIn;
    }
    // 保证连续，避免行对齐问题
    if (!mat.isContinuous()) mat = mat.clone();

    try {
        if (mat.channels() == 1) {
            HImage img;
            GenImage1(&img, "byte", mat.cols, mat.rows,
                      reinterpret_cast<Hlong>(mat.data));
            return img;
        }
        // BGR → HALCON RGB 平面
        const int w = mat.cols;
        const int h = mat.rows;
        std::vector<uchar> r(h * w), g(h * w), b(h * w);
        for (int y = 0; y < h; ++y) {
            const uchar *src = mat.ptr<uchar>(y);
            uchar *pr = r.data() + y * w;
            uchar *pg = g.data() + y * w;
            uchar *pb = b.data() + y * w;
            for (int x = 0; x < w; ++x) {
                pb[x] = src[x * 3 + 0];
                pg[x] = src[x * 3 + 1];
                pr[x] = src[x * 3 + 2];
            }
        }
        HObject imgR, imgG, imgB, merged;
        GenImage1(&imgR, "byte", w, h, reinterpret_cast<Hlong>(r.data()));
        GenImage1(&imgG, "byte", w, h, reinterpret_cast<Hlong>(g.data()));
        GenImage1(&imgB, "byte", w, h, reinterpret_cast<Hlong>(b.data()));
        Compose3(imgR, imgG, imgB, &merged);
        return HImage(merged);
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
