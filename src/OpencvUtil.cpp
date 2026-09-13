#include "OpencvUtil.h"
#include <halconcpp/HalconCpp.h>
#include <opencv2/imgproc.hpp>
#include <QImage>

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

} // namespace OpencvUtil
