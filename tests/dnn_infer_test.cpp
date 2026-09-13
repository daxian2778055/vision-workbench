// ONNX 深度学习推理链路测试：验证 OpenCV dnn 能加载 ONNX 并完成推理
// （DnnInferNode 的核心依赖；模型由 tests/gen_min_onnx.py 生成）
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cstdio>
#include <cmath>
#include <string>

int main(int argc, char **argv)
{
    const std::string modelPath = (argc > 1) ? argv[1] : "min_model.onnx";
    cv::dnn::Net net = cv::dnn::readNetFromONNX(modelPath);
    if (net.empty()) {
        std::printf("FAIL: readNetFromONNX 加载失败\n");
        return 1;
    }
    std::printf("ONNX 加载 OK\n");

    cv::Mat img(224, 224, CV_8UC3, cv::Scalar(64, 128, 192));
    cv::Mat blob = cv::dnn::blobFromImage(img, 1.0 / 255.0, cv::Size(224, 224),
                                          cv::Scalar(0, 0, 0), true, false);
    net.setInput(blob);
    cv::Mat out = net.forward();
    cv::Mat flat = out.reshape(1, 1);
    if (flat.cols < 2) {
        std::printf("FAIL: 输出维度异常 cols=%d\n", flat.cols);
        return 1;
    }
    float mx = -1e30f;
    for (int i = 0; i < flat.cols; ++i) mx = std::max(mx, flat.at<float>(0, i));
    float sum = 0.f;
    std::vector<float> prob(flat.cols);
    for (int i = 0; i < flat.cols; ++i) {
        prob[i] = std::exp(flat.at<float>(0, i) - mx);
        sum += prob[i];
    }
    int best = 0;
    for (int i = 0; i < flat.cols; ++i) {
        prob[i] /= sum;
        if (prob[i] > prob[best]) best = i;
    }
    std::printf("推理 OK: 输出形状 %dx%d, top=%d conf=%.4f\n", out.rows, out.cols,
                best, prob[best]);
    std::printf("ALL PASSED: OpenCV dnn ONNX 加载+推理 可用\n");
    return 0;
}
