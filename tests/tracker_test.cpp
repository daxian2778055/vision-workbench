// OpenCV 目标跟踪器闭环测试：合成移动目标序列，验证跟踪位置误差
#include "OpenCvTracker.h"
#include <opencv2/imgproc.hpp>
#include <cstdio>
#include <cmath>

int main()
{
    SimpleTracker::Tracker tr;
    const int W = 320, H = 240;

    // 第 0 帧：目标在 (60,50) 处 40x40 方块（带内部纹理，模拟真实目标）
    auto makeFrame = [&](int cx, int cy) {
        cv::Mat f(H, W, CV_8UC1, cv::Scalar(20));
        cv::rectangle(f, cv::Rect(cx - 20, cy - 20, 40, 40), cv::Scalar(200), -1);
        // 内部纹理：十字
        cv::line(f, cv::Point(cx, cy - 12), cv::Point(cx, cy + 12), cv::Scalar(60), 3);
        cv::line(f, cv::Point(cx - 12, cy), cv::Point(cx + 12, cy), cv::Scalar(60), 3);
        return f;
    };

    cv::Mat f0 = makeFrame(60, 50);
    if (!tr.init(f0, cv::Rect(40, 30, 40, 40))) {
        std::printf("FAIL: init\n");
        return 1;
    }

    // 目标沿对角线运动 30 帧，每帧 (+3,+2)
    double maxErr = 0.0;
    int lost = 0;
    for (int i = 1; i <= 30; ++i) {
        const int cx = 60 + 3 * i, cy = 50 + 2 * i;
        cv::Mat f = makeFrame(cx, cy);
        cv::Rect r;
        double score;
        if (!tr.update(f, r, score, 60)) {
            ++lost;
            continue;
        }
        const double err = std::hypot(r.x + r.width / 2.0 - cx, r.y + r.height / 2.0 - cy);
        maxErr = std::max(maxErr, err);
    }
    std::printf("跟踪 30 帧完成: 丢失=%d, 最大中心误差=%.2f px\n", lost, maxErr);
    if (lost > 0 || maxErr > 3.0) {
        std::printf("FAIL: 跟踪精度不足\n");
        return 1;
    }
    std::printf("ALL PASSED: 目标跟踪可用\n");
    return 0;
}
