// HALCON DL 与 OpenCV DNN 可用性诊断（替换版环境下）
#include <halconcpp/HalconCpp.h>
#include <opencv2/dnn.hpp>
#include <cstdio>
#include <string>

using namespace HalconCpp;

int main()
{
    // ===== 1) HALCON 深度学习 =====
    std::printf("--- HALCON DL ---\n");
    const char *models[] = {
        "D:/Program Files/MVTec/HALCON-24.11-Progress-Steady/dl/pretrained_dl_classifier_compact.hdl",
        "D:/Program Files/MVTec/HALCON-24.11-Progress-Steady/dl/initial_dl_anomaly_medium.hdl",
    };
    for (const char *m : models) {
        try {
            HTuple handle;
            ReadDlModel(m, &handle);
            std::printf("ReadDlModel OK: %s\n", m);
            // 查模型类型
            try {
                HTuple type;
                GetDlModelParam(handle, "type", &type);
                std::printf("  model type=%s\n", type.S().TextA());
            } catch (const HException &e) {
                std::printf("  GetDlModelParam EXC: %s\n", e.ErrorMessage().TextA());
            }
            // 实际推理：64x64 灰度图（DLSampleBatch 传空，用内部样本）
            try {
                HImage img;
                GenImageConst(&img, "byte", 64, 64);
                HTuple sample;
                // 构建样本字典（ApplyDlModel 需 DLSampleBatch；空批时用图像直接构造）
                HTuple outputs, results;
                ApplyDlModel(handle, HTuple(), HTuple(), &results);
                std::printf("  ApplyDlModel OK, results len=%d\n", results.Length());
            } catch (const HException &e) {
                std::printf("  ApplyDlModel EXC: %s\n", e.ErrorMessage().TextA());
            }
            ClearDlModel(handle);
        } catch (const HException &e) {
            std::printf("ReadDlModel EXC: %s (%s)\n", e.ErrorMessage().TextA(), m);
        }
    }

    // ===== 2) OpenCV DNN 模块 =====
    std::printf("\n--- OpenCV DNN ---\n");
    {
        const std::string info = cv::getBuildInformation();
        auto find = [&info](const std::string &key) {
            auto pos = info.find(key);
            return pos != std::string::npos;
        };
        std::printf("getBuildInformation 含 DNN 模块: %s\n", find("DNN:") ? "YES" : "NO");
        // 提取 DNN 相关行
        size_t pos = info.find("DNN:");
        if (pos != std::string::npos) {
            std::printf("  %s\n", info.substr(pos, 300).c_str());
        }
        pos = info.find("ONNX");
        if (pos != std::string::npos) {
            std::printf("  %s\n", info.substr(pos - 30, 200).c_str());
        }
        // blobFromImage 基础调用（不需要模型）
        try {
            cv::Mat img(64, 64, CV_8UC3, cv::Scalar(128, 128, 128));
            cv::Mat blob = cv::dnn::blobFromImage(img, 1.0 / 255.0, cv::Size(64, 64),
                                                  cv::Scalar(0, 0, 0), true, false);
            std::printf("blobFromImage OK: blob %d x %d x %d x %d\n",
                        blob.size[0], blob.size[1], blob.size[2], blob.size[3]);
        } catch (const cv::Exception &e) {
            std::printf("blobFromImage EXC: %s\n", e.what());
        }
        // readNetFromONNX 错误行为（无模型文件，观察异常消息判断模块可用性）
        try {
            cv::dnn::Net net = cv::dnn::readNetFromONNX("C:/Windows/Temp/nonexistent.onnx");
            std::printf("readNetFromONNX（不应成功）\n");
        } catch (const cv::Exception &e) {
            std::printf("readNetFromONNX EXC（模块可用，仅文件不存在）: %s\n", e.what());
        } catch (const std::exception &e) {
            std::printf("readNetFromONNX std EXC: %s\n", e.what());
        }
    }
    return 0;
}
