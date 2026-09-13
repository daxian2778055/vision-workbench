// 独立最小实验：findChessboardCorners 在纯 OpenCV 环境的行为
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <cstdio>

int main()
{
    std::printf("OpenCV version: %s\n", cv::getVersionString().c_str());
    std::printf("useOptimized: %d\n", cv::useOptimized() ? 1 : 0);

    // 实验：禁用 IPP
    cv::ipp::setUseIPP(false);

    // 9x6 内角棋盘，格 30px，居中 640x480
    const int sq = 30;
    const int pw = 9, ph = 6;
    const int cols = pw + 1, rows = ph + 1;
    const int ox = (640 - cols * sq) / 2;
    const int oy = (480 - rows * sq) / 2;
    cv::Mat board = cv::Mat::zeros(480, 640, CV_8UC1);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            if ((r + c) % 2 == 0)
                cv::rectangle(board, cv::Rect(ox + c * sq, oy + r * sq, sq, sq),
                              cv::Scalar(255), cv::FILLED);

    std::vector<cv::Point2f> corners;
    const bool ok = cv::findChessboardCorners(board, cv::Size(pw, ph), corners,
                                              cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
    std::printf("findChessboardCorners 9x6 on 640x480: %d (corners=%d)\n", ok ? 1 : 0,
                static_cast<int>(corners.size()));

    // 规避实验：原生小图边界矩阵（排除 resize 影响）
    {
        struct S { int w, h, sq; };
        const S sizes[] = {{160, 160, 40}, {160, 160, 20}, {200, 200, 40}, {240, 240, 40},
                           {160, 120, 20}, {200, 150, 25}, {256, 256, 40}};
        for (const auto &s : sizes) {
            cv::Mat t = cv::Mat::zeros(s.h, s.w, CV_8UC1);
            const int x0 = (s.w - 4 * s.sq) / 2;
            const int y0 = (s.h - 4 * s.sq) / 2;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    if ((r + c) % 2 == 0)
                        cv::rectangle(t, cv::Rect(x0 + c * s.sq, y0 + r * s.sq, s.sq, s.sq),
                                      cv::Scalar(255), cv::FILLED);
            std::vector<cv::Point2f> cc;
            const bool ok = cv::findChessboardCorners(t, cv::Size(3, 3), cc,
                                                      cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
            std::printf("  -> 原生 %dx%d 格%dpx 3x3: %d\n", s.w, s.h, s.sq, ok ? 1 : 0);
        }
    }

    // 160x160 对照
    cv::Mat small = cv::Mat::zeros(160, 160, CV_8UC1);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if ((r + c) % 2 == 0)
                cv::rectangle(small, cv::Rect(c * 40, r * 40, 40, 40), cv::Scalar(255), cv::FILLED);
    std::vector<cv::Point2f> sc;
    const bool sok = cv::findChessboardCorners(small, cv::Size(3, 3), sc,
                                               cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
    std::printf("findChessboardCorners 3x3 on 160x160: %d\n", sok ? 1 : 0);

    // 对照：对称圆点标定板（findCirclesGrid）在 640x480
    {
        cv::Mat dots = cv::Mat::zeros(480, 640, CV_8UC1);
        const int gx = 7, gy = 7;      // 7x7 圆点
        const int sp = 60;             // 点间距
        const int x0 = (640 - (gx - 1) * sp) / 2;
        const int y0 = (480 - (gy - 1) * sp) / 2;
        for (int r = 0; r < gy; ++r)
            for (int c = 0; c < gx; ++c)
                cv::circle(dots, cv::Point(x0 + c * sp, y0 + r * sp), 12, cv::Scalar(255), cv::FILLED);
        std::vector<cv::Point2f> centers;
        const bool gok = cv::findCirclesGrid(dots, cv::Size(gx, gy), centers,
                                             cv::CALIB_CB_SYMMETRIC_GRID);
        std::printf("findCirclesGrid 7x7 on 640x480: %d\n", gok ? 1 : 0);
    }

    // 4.13 新检测器：findChessboardCornersSB（不同算法路径，可能绕过本环境缺陷）
    {
        std::vector<cv::Point2f> sb;
        const bool ok = cv::findChessboardCornersSB(board, cv::Size(pw, ph), sb,
                                                    cv::CALIB_CB_NORMALIZE_IMAGE);
        std::printf("findChessboardCornersSB 9x6 on 640x480: %d (corners=%d)\n",
                    ok ? 1 : 0, static_cast<int>(sb.size()));
        // 大棋盘 SB（11x8 内角）
        cv::Mat big = cv::Mat::zeros(480, 640, CV_8UC1);
        const int bsq = 45;
        const int bcols = 11, brows = 8;
        const int bx0 = (640 - bcols * bsq) / 2;
        const int by0 = (480 - brows * bsq) / 2;
        for (int r = 0; r < brows; ++r)
            for (int c = 0; c < bcols; ++c)
                if ((r + c) % 2 == 0)
                    cv::rectangle(big, cv::Rect(bx0 + c * bsq, by0 + r * bsq, bsq, bsq),
                                  cv::Scalar(255), cv::FILLED);
        std::vector<cv::Point2f> sb2;
        const bool ok2 = cv::findChessboardCornersSB(big, cv::Size(bcols - 1, brows - 1), sb2,
                                                     cv::CALIB_CB_NORMALIZE_IMAGE);
        std::printf("findChessboardCornersSB 10x7 on 640x480: %d (corners=%d)\n",
                    ok2 ? 1 : 0, static_cast<int>(sb2.size()));
    }
    return 0;
}
