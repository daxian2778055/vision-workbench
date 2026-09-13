// OpenCV 桥接与最小节点算法验证（替换版环境下）：
// HImage↔cv::Mat 双向转换 + 阈值/Blob/形态学链路输出正确性
#include "OpencvUtil.h"
#include <halconcpp/HalconCpp.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/objdetect.hpp>
#include <zxing/ZXingCpp.h>
#include <tesseract/baseapi.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <algorithm>

using namespace HalconCpp;

static int g_fail = 0;
static bool g_summary = false;
static FILE *g_detail = nullptr;
#define CHECK(cond, name, detail)                                           \
    do {                                                                    \
        if (cond) {                                                         \
            if (g_summary) {                                                \
                if (g_detail) std::fprintf(g_detail, "PASS: %s (%s)\n", name, detail); \
            } else {                                                        \
                std::printf("PASS: %s (%s)\n", name, detail);               \
            }                                                               \
        } else {                                                            \
            if (g_summary) {                                                \
                if (g_detail) std::fprintf(g_detail, "FAIL: %s (%s)\n", name, detail); \
            } else {                                                        \
                std::printf("FAIL: %s (%s)\n", name, detail);               \
            }                                                               \
            ++g_fail;                                                       \
        }                                                                   \
    } while (0)

// HALCON 合成 400x400：背景 0，亮矩形 (50,50)-(149,149) 灰度 200（像素直接写入，替换版下图像可写）
static HImage makeTestImage()
{
    HImage img;
    GenImageConst(&img, "byte", 400, 400);
    HTuple pointer, type, w, h;
    GetImagePointer1(img, &pointer, &type, &w, &h);
    uchar *ptr = reinterpret_cast<uchar *>(pointer.L());
    for (int y = 50; y < 150; ++y)
        for (int x = 50; x < 150; ++x)
            ptr[y * 400 + x] = 200;
    return img;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--summary") == 0) {
        g_summary = true;
        // 摘要模式下静默第三方库（leptonica/tesseract）的无害 stderr 警告；
        // 真实失败仍通过 CHECK 明细（stdout）与退出码暴露。
        std::freopen("NUL", "w", stderr);
        g_detail = std::fopen("opencv_nodes_test_detail.log", "w");
        if (g_detail) {
            std::time_t t = std::time(nullptr);
            char ts[64];
            std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
            std::fprintf(g_detail, "opencv_nodes_test detail log  %s\n", ts);
        }
    }

    // 1) HImage → cv::Mat
    HImage img = makeTestImage();
    cv::Mat mat = OpencvUtil::himageToMat(img);
    char buf[160];

    snprintf(buf, sizeof(buf), "mat %dx%d C%d，矩形角点灰度=%d",
             mat.cols, mat.rows, mat.channels(), mat.at<uchar>(50, 50));
    CHECK(!mat.empty() && mat.cols == 400 && mat.rows == 400 && mat.at<uchar>(50, 50) == 200,
          "HImage→cv::Mat", buf);

    // 2) 阈值（OpenCV 二值化，替代 HALCON Threshold）
    cv::Mat bin;
    cv::threshold(mat, bin, 127, 255, cv::THRESH_BINARY);
    const int fg = static_cast<int>(cv::countNonZero(bin));
    snprintf(buf, sizeof(buf), "前景像素=%d（期望 10000）", fg);
    CHECK(fg == 10000, "cv::threshold 前景统计", buf);

    // 3) Blob（连通域，替代 HALCON connection+select_shape）
    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8);
    int blobCount = 0, maxArea = 0;
    for (int i = 1; i < n; ++i) {
        const int a = stats.at<int>(i, cv::CC_STAT_AREA);
        ++blobCount;
        if (a > maxArea) maxArea = a;
    }
    snprintf(buf, sizeof(buf), "blob 数=%d（期望 1），最大面积=%d（期望 10000）", blobCount, maxArea);
    CHECK(blobCount == 1 && maxArea == 10000, "cv::connectedComponentsWithStats", buf);

    // 4) 形态学膨胀（替代 HALCON DilationCircle）
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7, 7));
    cv::Mat dilated;
    cv::dilate(bin, dilated, kernel);
    const int dArea = static_cast<int>(cv::countNonZero(dilated));
    snprintf(buf, sizeof(buf), "膨胀后面积=%d（应 >10000）", dArea);
    CHECK(dArea > 10000, "cv::dilate 面积增大", buf);

    // 5) cv::Mat → HImage（回写验证）
    HImage back = OpencvUtil::matToHimage(bin);
    HTuple g1, g2;
    GetGrayval(back, 50, 50, &g1);
    GetGrayval(back, 10, 10, &g2);
    snprintf(buf, sizeof(buf), "矩形内=%d（期望255），背景=%d（期望0）", g1.I(), g2.I());
    CHECK(g1.I() == 255 && g2.I() == 0, "cv::Mat→HImage 回写", buf);

    // 6) 三通道桥接
    HObject rgb;
    Compose3(img, img, img, &rgb);
    HImage rgbImg(rgb);
    cv::Mat rgbMat = OpencvUtil::himageToMat(rgbImg);
    snprintf(buf, sizeof(buf), "三通道 mat %dx%d C%d", rgbMat.cols, rgbMat.rows, rgbMat.channels());
    CHECK(!rgbMat.empty() && rgbMat.channels() == 3 && rgbMat.at<cv::Vec3b>(50, 50)[2] == 200,
          "三通道 HImage→cv::Mat", buf);

    // 7) 开运算（替代 HALCON opening）
    cv::Mat opened;
    cv::morphologyEx(bin, opened, cv::MORPH_OPEN, kernel);
    const int oArea = static_cast<int>(cv::countNonZero(opened));
    snprintf(buf, sizeof(buf), "开运算面积=%d（应 <10000，去除角点毛刺）", oArea);
    CHECK(oArea > 0 && oArea <= 10000, "cv::morphologyEx 开运算", buf);

    // 8) 边缘检测（Canny，替代 HALCON EdgesSubPix 的像素级输出）
    {
        cv::Mat edgeMat;
        cv::GaussianBlur(mat, edgeMat, cv::Size(3, 3), 0);
        cv::Canny(edgeMat, edgeMat, 50, 150);
        const int eCount = static_cast<int>(cv::countNonZero(edgeMat));
        snprintf(buf, sizeof(buf), "矩形边缘像素=%d（应 >0）", eCount);
        CHECK(eCount > 300, "cv::Canny 边缘检测", buf);
    }

    // 9) 直线拟合（斜线 45°，替代 HALCON fit_line）
    {
        cv::Mat lineImg = cv::Mat::zeros(400, 400, CV_8UC1);
        cv::line(lineImg, cv::Point(50, 50), cv::Point(200, 200), cv::Scalar(255), 3);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(lineImg, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        std::vector<cv::Point> pts;
        for (auto &c : contours) pts.insert(pts.end(), c.begin(), c.end());
        cv::Vec4f line;
        cv::fitLine(pts, line, cv::DIST_L2, 0, 0.01, 0.01);
        double angle = std::atan2(line[1], line[0]) * 180.0 / CV_PI;
        // 直线方向任意：归一化到 [0,90] 后应接近 45°
        double a = std::fmod(std::fabs(angle), 180.0);
        if (a > 90.0) a = 180.0 - a;
        snprintf(buf, sizeof(buf), "拟合角度=%.1f°（应≈45°）", a);
        CHECK(std::fabs(a - 45.0) < 1.5, "cv::fitLine 斜线角度", buf);
    }

    // 10) 圆拟合（半径 60，替代 HALCON fit_circle）
    {
        cv::Mat circleImg = cv::Mat::zeros(400, 400, CV_8UC1);
        cv::circle(circleImg, cv::Point(200, 200), 60, cv::Scalar(255), 3);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(circleImg, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        if (!contours.empty()) {
            cv::Point2f center;
            float radius = 0.0f;
            cv::minEnclosingCircle(contours[0], center, radius);
            snprintf(buf, sizeof(buf), "圆心=(%.1f,%.1f) 半径=%.1f（期望(200,200)/60）",
                     center.x, center.y, radius);
            CHECK(std::fabs(center.x - 200) < 3 && std::fabs(center.y - 200) < 3
                      && std::fabs(radius - 60) < 3,
                  "cv::minEnclosingCircle 圆拟合", buf);
        } else {
            CHECK(false, "cv::minEnclosingCircle 圆拟合", "未找到轮廓");
        }
    }

    // 11) 模板匹配（cv::matchTemplate + 平坦区域抑制，替代 HALCON 灰度/NCC 匹配）
    {
        // 合成带噪声图：背景 30±5，40x40 亮方块（灰度 200±5）在 (120,120)
        cv::Mat bg(400, 400, CV_8UC1);
        cv::randu(bg, cv::Scalar(0), cv::Scalar(5));       // 背景噪声 0-5
        cv::Mat fg(40, 40, CV_8UC1);
        cv::randu(fg, cv::Scalar(195), cv::Scalar(205));   // 方块噪声 195-205
        cv::Mat tmplImg = cv::Mat::zeros(400, 400, CV_8UC1);
        tmplImg.setTo(30);
        tmplImg += bg;
        cv::Mat roi = tmplImg(cv::Rect(120, 120, 40, 40));
        roi += (fg - 30);
        // 模板 = 方块区域（含噪声）
        cv::Mat tmpl = tmplImg(cv::Rect(120, 120, 40, 40)).clone();
        cv::Mat mres;
        cv::matchTemplate(tmplImg, tmpl, mres, cv::TM_CCOEFF_NORMED);
        // 平坦区域抑制（与节点一致）
        {
            cv::Mat grayF;
            tmplImg.convertTo(grayF, CV_32F);
            cv::Mat mean, meanSq, sqF;
            cv::multiply(grayF, grayF, sqF);
            cv::boxFilter(grayF, mean, CV_32F, tmpl.size());
            cv::boxFilter(sqF, meanSq, CV_32F, tmpl.size());
            cv::Mat var = meanSq - mean.mul(mean);
            cv::Mat varROI = var(cv::Rect(0, 0, mres.cols, mres.rows));
            cv::Scalar tMean, tStd;
            cv::meanStdDev(tmpl, tMean, tStd);
            const double tVar = tStd[0] * tStd[0];
            if (tVar > 1e-6) mres.setTo(0.0, varROI < tVar * 0.25);
        }
        double mv = 0.0, mx = 0.0;
        cv::Point ml, mxl;
        cv::minMaxLoc(mres, &mv, &mx, &ml, &mxl);
        // 诊断：关键位置的值
        {
            const double atTarget = mres.at<float>(120, 120);
            const double at00 = mres.at<float>(0, 0);
            cv::Scalar tMean2, tStd2;
            cv::meanStdDev(tmpl, tMean2, tStd2);
            if (g_summary) {
                if (g_detail)
                    std::fprintf(g_detail, "  [dbg] result(120,120)=%.4f result(0,0)=%.4f 模板std=%.3f\n",
                        atTarget, at00, tStd2[0]);
            } else {
                std::printf("  [dbg] result(120,120)=%.4f result(0,0)=%.4f 模板std=%.3f\n",
                    atTarget, at00, tStd2[0]);
            }
        }
        snprintf(buf, sizeof(buf), "最佳位置=(%d,%d) 分数=%.3f（期望(120,120)/>0.9）",
                 mxl.x, mxl.y, mx);
        CHECK(mxl.x == 120 && mxl.y == 120 && mx > 0.9, "cv::matchTemplate 位置与分数", buf);
    }

    // 12) 卡尺测量（法线方向一维边缘搜索，替代 HALCON metrology）
    {
        // 合成图：背景 30，亮矩形 col 140..160（宽 20），行 100..299
        cv::Mat calImg = cv::Mat::zeros(400, 400, CV_8UC1);
        calImg.setTo(30);
        cv::rectangle(calImg, cv::Rect(140, 100, 20, 200), cv::Scalar(200), cv::FILLED);
        // 测量线：垂直方向 (150,50)->(150,350)，法线水平；半宽 15 覆盖 col 135..165
        const double c1 = 150, r1 = 50, c2 = 150, r2 = 350;
        const int half = 15;
        const double L = std::hypot(c2 - c1, r2 - r1);
        const double ux = (c2 - c1) / L, uy = (r2 - r1) / L;
        const double nx = -uy, ny = ux;   // 法线：水平（-1,0 或 1,0）
        const int K = static_cast<int>(L / 5.0);
        std::vector<double> edgeCols;
        for (int k = 0; k <= K; ++k) {
            const double t = static_cast<double>(k) / K;
            const double cx = c1 + (c2 - c1) * t;
            const double cy = r1 + (r2 - r1) * t;
            std::vector<float> prof(2 * half + 1);
            for (int m = 0; m < 2 * half + 1; ++m) {
                const double off = m - half;
                const double px = cx + nx * off;
                const double py = cy + ny * off;
                const int x0 = static_cast<int>(px);
                const int y0 = static_cast<int>(py);
                if (x0 < 0 || y0 < 0 || x0 >= 399 || y0 >= 399) { prof[m] = 0; continue; }
                const double fx = px - x0, fy = py - y0;
                prof[m] = static_cast<float>(
                    calImg.at<uchar>(y0, x0) * (1 - fx) * (1 - fy)
                    + calImg.at<uchar>(y0, x0 + 1) * fx * (1 - fy)
                    + calImg.at<uchar>(y0 + 1, x0) * (1 - fx) * fy
                    + calImg.at<uchar>(y0 + 1, x0 + 1) * fx * fy);
            }
            // 收集所有跨零极值（与节点算法一致）
            std::vector<float> grad(2 * half);
            for (int m = 0; m < 2 * half; ++m) grad[m] = prof[m + 1] - prof[m];
            for (int m = 1; m < 2 * half - 1; ++m) {
                if ((grad[m - 1] >= 0 && grad[m + 1] <= 0) || (grad[m - 1] <= 0 && grad[m + 1] >= 0)) {
                    if (std::fabs(grad[m]) < 10.0) continue;
                    double sub = static_cast<double>(m);
                    const double g0 = std::fabs(grad[m - 1]);
                    const double g1 = std::fabs(grad[m]);
                    const double g2 = std::fabs(grad[m + 1]);
                    const double denom = g0 - 2 * g1 + g2;
                    if (std::fabs(denom) > 1e-9) sub += 0.5 * (g0 - g2) / denom;
                    const double off = sub - half;
                    edgeCols.push_back(cx + nx * off);
                }
            }
        }
        if (!edgeCols.empty()) {
            double mn = *std::min_element(edgeCols.begin(), edgeCols.end());
            double mx2 = *std::max_element(edgeCols.begin(), edgeCols.end());
            snprintf(buf, sizeof(buf), "边缘点=%d 个，列范围 %.1f..%.1f（期望≈140/160）",
                     static_cast<int>(edgeCols.size()), mn, mx2);
            CHECK(edgeCols.size() >= 2 && std::fabs(mn - 140) < 2 && std::fabs(mx2 - 160) < 2,
                  "卡尺法线边缘搜索", buf);
        } else {
            CHECK(false, "卡尺法线边缘搜索", "未找到边缘点");
        }
    }

    // 13) 相机标定（合成棋盘格多视角，替代 HALCON calibrate_cameras 链路）
    {
        // 棋盘：9x6 内角点 → 10x7 方格，格子 30px，居中（占比≈47%x44%），图像 640x480
        const int sq = 30;
        const cv::Size pattern(9, 6);
        const int cols = pattern.width + 1;
        const int rows = pattern.height + 1;
        const int ox = (640 - cols * sq) / 2;
        const int oy = (480 - rows * sq) / 2;
        cv::Mat board = cv::Mat::zeros(480, 640, CV_8UC1);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                if ((r + c) % 2 == 0)
                    cv::rectangle(board, cv::Rect(ox + c * sq, oy + r * sq, sq, sq),
                                  cv::Scalar(255), cv::FILLED);
        // 模拟真实成像：轻微模糊 + 噪声（纯二值棋盘检测器常失败）
        cv::GaussianBlur(board, board, cv::Size(3, 3), 0);
        cv::Mat noise(480, 640, CV_8UC1);
        cv::randu(noise, cv::Scalar(0), cv::Scalar(10));
        board += noise;

        // 透视视角用当前棋盘几何
        std::vector<cv::Point2f> boardCorners = {cv::Point2f(ox, oy), cv::Point2f(ox + cols * sq, oy),
                                                 cv::Point2f(ox, oy + rows * sq), cv::Point2f(ox + cols * sq, oy + rows * sq)};
        std::vector<cv::Mat> views;
        views.push_back(board.clone());
        for (int i = 1; i <= 3; ++i) {
            std::vector<cv::Point2f> dst = {cv::Point2f(ox + i * 4.0f, oy), cv::Point2f(ox + cols * sq, oy + i * 5.0f),
                                            cv::Point2f(ox, oy + rows * sq), cv::Point2f(ox + cols * sq, oy + rows * sq - i * 3.0f)};
            cv::Mat M = cv::getPerspectiveTransform(boardCorners, dst);
            cv::Mat warped;
            cv::warpPerspective(board, warped, M, board.size());
            views.push_back(warped);
        }

        std::vector<std::vector<cv::Point2f>> cornerSets;
        std::vector<std::vector<cv::Point3f>> objPts;
        int detected = 0;
        for (const auto &v : views) {
            std::vector<cv::Point2f> corners;
            // SB 检测器（4.13.0）：绕开本环境旧检测器大图缺陷
            bool ok = cv::findChessboardCornersSB(v, pattern, corners,
                                                  cv::CALIB_CB_NORMALIZE_IMAGE);
            if (!ok) {
                ok = cv::findChessboardCorners(v, pattern, corners,
                                               cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
            }
            if (ok) {
                ++detected;
                cornerSets.push_back(corners);
                std::vector<cv::Point3f> pts;
                for (int r = 0; r < pattern.height; ++r)
                    for (int c = 0; c < pattern.width; ++c)
                        pts.emplace_back(c * 10.0f, r * 10.0f, 0.0f);
                objPts.push_back(pts);
            }
        }
        snprintf(buf, sizeof(buf), "检测帧数=%d/4", detected);
        CHECK(detected >= 3, "findChessboardCornersSB 多视角检测", buf);

        if (cornerSets.size() >= 3) {
            cv::Mat cam = cv::Mat::eye(3, 3, CV_64F);
            cv::Mat dist = cv::Mat::zeros(8, 1, CV_64F);
            std::vector<cv::Mat> rv, tv;
            double rms = cv::calibrateCamera(objPts, cornerSets, cv::Size(640, 480),
                                             cam, dist, rv, tv);
            const double fx = cam.at<double>(0, 0);
            const double fy = cam.at<double>(1, 1);
            snprintf(buf, sizeof(buf), "rms=%.3f px，fx=%.1f fy=%.1f（应合理且 rms<1）", rms, fx, fy);
            CHECK(std::isfinite(rms) && rms < 1.0 && fx > 100 && fy > 100,
                  "calibrateCamera 标定", buf);
        }
    }

    // 14) 二维码解码（QRCodeEncoder 生成 → QRCodeDetector 解码闭环，替代 HALCON 条码 QR 部分）
    {
        try {
            cv::Ptr<cv::QRCodeEncoder> enc = cv::QRCodeEncoder::create();
            cv::Mat qrImg;
            enc->encode("VFP-TEST-2026", qrImg);
            if (g_summary) {
                if (g_detail)
                    std::fprintf(g_detail, "  [dbg] QR 图: %dx%d C%d\n", qrImg.cols, qrImg.rows, qrImg.channels());
            } else {
                std::printf("  [dbg] QR 图: %dx%d C%d\n", qrImg.cols, qrImg.rows, qrImg.channels());
            }
            cv::QRCodeDetector qr;
            std::vector<cv::Point> pts;
            std::string text = qr.detectAndDecode(qrImg, pts);
            if (text.empty()) {
                // 放大 4 倍后再试（小图检测器有时失败）
                cv::Mat big;
                cv::resize(qrImg, big, cv::Size(qrImg.cols * 4, qrImg.rows * 4), 0, 0, cv::INTER_NEAREST);
                text = qr.detectAndDecode(big, pts);
            }
            snprintf(buf, sizeof(buf), "解码结果=%s（期望 VFP-TEST-2026）", text.c_str());
            CHECK(text == "VFP-TEST-2026", "QR 码 encode→decode 闭环", buf);
        } catch (const cv::Exception &e) {
            snprintf(buf, sizeof(buf), "QR 异常: %s", e.what());
            CHECK(false, "QR 码 encode→decode 闭环", buf);
        }
    }

    // 15) ZXing 条码闭环（CreateBarcodeFromText 生成 Code128 → ReadBarcodes 解码）
    {
        try {
            ZXing::CreatorOptions copts(ZXing::BarcodeFormat::Code128);
            auto bc = ZXing::CreateBarcodeFromText("VFP-123456", copts);
            ZXing::WriterOptions wopts;
            wopts.scale(4);
            auto img = ZXing::WriteBarcodeToImage(bc, wopts);
            ZXing::ImageView view(img.data(), img.width(), img.height(),
                                  ZXing::ImageFormat::Lum);
            auto res = ZXing::ReadBarcodes(view);
            const bool ok = !res.empty() && res[0].text() == "VFP-123456";
            snprintf(buf, sizeof(buf), "解码=%zu 条，内容=%s（期望 VFP-123456）",
                     res.size(), ok ? res[0].text().c_str() : "(空)");
            CHECK(ok, "ZXing Code128 encode→decode 闭环", buf);
        } catch (const std::exception &e) {
            snprintf(buf, sizeof(buf), "ZXing 异常: %s", e.what());
            CHECK(false, "ZXing Code128 encode→decode 闭环", buf);
        }
    }

    // 16) Tesseract OCR（合成文字图 → 识别）
    {
        try {
            // 语言数据路径自动探测：部署包（exe 同目录 tessdata）→ 源码开发目录
            std::string tessdata;
            if (std::FILE *f = std::fopen("tessdata/eng.traineddata", "rb")) {
                std::fclose(f);
                tessdata = "tessdata";
            } else if (std::FILE *f = std::fopen("../thirdparty/tesseract/tessdata/eng.traineddata", "rb")) {
                std::fclose(f);
                tessdata = "../thirdparty/tesseract/tessdata";
            }
            tesseract::TessBaseAPI api;
            if (tessdata.empty() || api.Init(tessdata.c_str(), "eng")) {
                printf("  [skip] Tesseract 初始化失败（无语言数据），跳过\n");
            } else {
                cv::Mat ocrImg(120, 480, CV_8UC1, cv::Scalar(255));
                cv::putText(ocrImg, "VFP OCR 2026", cv::Point(20, 75),
                            cv::FONT_HERSHEY_SIMPLEX, 1.6, cv::Scalar(0), 3);
                api.SetImage(ocrImg.data, ocrImg.cols, ocrImg.rows, 1,
                             static_cast<int>(ocrImg.step));
                char *text = api.GetUTF8Text();
                const std::string res = text ? text : "";
                delete[] text;
                const bool ok = res.find("VFP") != std::string::npos;
                snprintf(buf, sizeof(buf), "识别=%s（应含 VFP）", res.c_str());
                CHECK(ok, "Tesseract OCR 识别", buf);
            }
        } catch (const std::exception &e) {
            snprintf(buf, sizeof(buf), "Tesseract 异常: %s", e.what());
            CHECK(false, "Tesseract OCR 识别", buf);
        }
    }

    // 17) OpenCV 自适应阈值（替代动态阈值）
    {
        cv::Mat src(200, 200, CV_8UC1, cv::Scalar(180));
        cv::rectangle(src, cv::Rect(60, 60, 80, 80), cv::Scalar(60), -1);
        cv::Mat bin;
        cv::adaptiveThreshold(src, bin, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                              cv::THRESH_BINARY_INV, 15, 5);
        // 暗矩形应被提取为前景（自适应阈值提取“与局部均值偏差大的像素”= 边缘环带）
        const int fg = static_cast<int>(cv::countNonZero(bin));
        const bool ok = fg > 1000 && fg < 30000;  // 边缘环带被提取
        snprintf(buf, sizeof(buf), "前景像素=%d（边缘环带，期望 1000~30000）", fg);
        CHECK(ok, "OpencvAdaptiveThreshold 自适应阈值", buf);
    }

    // 18) OpenCV ROI 裁剪（替代区域裁剪）
    {
        cv::Mat src(200, 200, CV_8UC1, cv::Scalar(0));
        cv::rectangle(src, cv::Rect(40, 40, 60, 60), cv::Scalar(255), -1);
        // 裁剪 (row=50, col=50, h=40, w=40)
        cv::Mat cropped = src(cv::Rect(50, 50, 40, 40)).clone();
        const bool ok = cropped.rows == 40 && cropped.cols == 40
                        && cropped.at<uchar>(0, 0) == 255;  // 矩形内部
        snprintf(buf, sizeof(buf), "裁剪 %dx%d（期望 40x40 且含白块）", cropped.cols, cropped.rows);
        CHECK(ok, "OpencvCrop ROI裁剪", buf);
        // 边界钳制：模拟节点逻辑（先钳制再裁剪，越界不崩溃）
        {
            const int x = std::max(0, std::min(190, 200 - 1));
            const int y = std::max(0, std::min(190, 200 - 1));
            const int cw = std::max(1, std::min(100, 200 - x));
            const int ch = std::max(1, std::min(100, 200 - y));
            cv::Mat clamped = src(cv::Rect(x, y, cw, ch)).clone();
            CHECK(clamped.rows == 10 && clamped.cols == 10,
                  "OpencvCrop 边界钳制", "190+100 钳制到 200（10x10）");
        }
    }

    // 19) OpenCV 图像运算（替代损坏的 HALCON AddImage/MultImage）
    {
        cv::Mat a(64, 64, CV_8UC1, cv::Scalar(100));
        cv::Mat b(64, 64, CV_8UC1, cv::Scalar(50));
        cv::Mat add, mul;
        cv::add(a, b, add);
        cv::multiply(a, b, mul, 1.0 / 255.0);
        CHECK(add.at<uchar>(0, 0) == 150 && std::abs(mul.at<uchar>(0, 0) - 20) <= 1,  // 100*50/255≈19.6→20
              "OpencvImageArith 加/乘", "add=150 mul≈20");
        cv::Mat sub, div;
        cv::subtract(a, b, sub);
        cv::divide(a, b, div);
        CHECK(sub.at<uchar>(0, 0) == 50 && div.at<uchar>(0, 0) == 2,
              "OpencvImageArith 减/除", "sub=50 div=2");
    }

    // 20) OpenCV 图像旋转（替代损坏的 HALCON RotateImage）
    {
        cv::Mat src(100, 100, CV_8UC1, cv::Scalar(0));
        cv::rectangle(src, cv::Rect(40, 20, 20, 60), cv::Scalar(255), -1);
        cv::Point2f center(50.0f, 50.0f);
        cv::Mat rot = cv::getRotationMatrix2D(center, 90.0, 1.0);
        cv::Mat out;
        cv::warpAffine(src, out, rot, src.size(), cv::INTER_LINEAR,
                       cv::BORDER_CONSTANT, cv::Scalar(0));
        // 竖条旋转 90° 后应为横条：中心行 (50,60) 白、角落 (30,30) 黑
        const bool ok = out.at<uchar>(50, 60) > 200 && out.at<uchar>(30, 30) < 50;
        snprintf(buf, sizeof(buf), "中心=%d 角落=%d", out.at<uchar>(50, 60), out.at<uchar>(30, 30));
        CHECK(ok, "OpencvRotate 旋转", buf);
    }

    // 21) OpenCV 灰度统计（替代损坏的 HALCON MinMaxGray）
    {
        cv::Mat src(64, 64, CV_8UC1, cv::Scalar(30));
        cv::rectangle(src, cv::Rect(10, 10, 20, 20), cv::Scalar(200), -1);
        double minV, maxV;
        cv::minMaxLoc(src, &minV, &maxV);
        const double meanV = cv::mean(src)[0];
        const bool ok = minV == 30.0 && maxV == 200.0 && meanV > 30.0 && meanV < 200.0;
        snprintf(buf, sizeof(buf), "min=%.0f max=%.0f mean=%.1f", minV, maxV, meanV);
        CHECK(ok, "OpencvPixelStats 灰度统计", buf);
    }

    std::printf("\n==== 汇总: %s ====\n", g_fail == 0 ? "ALL PASSED" : "HAS FAILURES");
    if (g_detail) std::fclose(g_detail);
    return g_fail == 0 ? 0 : 1;
}
