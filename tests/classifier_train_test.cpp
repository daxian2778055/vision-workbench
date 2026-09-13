// OpenCV ML 分类器训练->推理闭环测试（纯 C++，不依赖 Qt/GUI）
// 用法：classifier_train_test.exe [tempDir]
// 退出码 0 = 闭环通过（训练保存->加载->预测准确率达标）
#include "ClassifierTrainer.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

static cv::Mat makeCircleImg(int off)
{
    cv::Mat img(100, 100, CV_8UC1, cv::Scalar(20));
    cv::circle(img, cv::Point(50 + off, 50 + off), 22, cv::Scalar(220), -1);
    return img;
}

static cv::Mat makeSquareImg(int off)
{
    cv::Mat img(100, 100, CV_8UC1, cv::Scalar(20));
    cv::rectangle(img, cv::Rect(28 + off, 28 + off, 44, 44), cv::Scalar(220), -1);
    return img;
}

int main(int argc, char **argv)
{
    fs::path tmp = (argc > 1) ? fs::path(argv[1])
                              : fs::temp_directory_path() / "vfp_cls_test";
    const fs::path dir0 = tmp / "circle";
    const fs::path dir1 = tmp / "square";
    fs::remove_all(tmp);
    fs::create_directories(dir0);
    fs::create_directories(dir1);

    // 每类 12 张（轻微位置扰动）
    for (int i = 0; i < 12; ++i) {
        const int off = (i % 3) - 1;
        cv::imwrite((dir0 / ("c" + std::to_string(i) + ".png")).string(),
                    makeCircleImg(off));
        cv::imwrite((dir1 / ("s" + std::to_string(i) + ".png")).string(),
                    makeSquareImg(off));
    }

    const fs::path model = tmp / "model.yaml";
    std::printf("模型路径: %s\n", model.string().c_str());
    ClsTrainer::TrainStats st;
    try {
        st = ClsTrainer::trainFromDir(tmp.string(), model.string(), 0, 32, 800);
    } catch (const cv::Exception &e) {
        std::printf("FAIL: 训练异常: %s\n", e.what());
        return 1;
    }
    if (!st.ok) {
        std::printf("FAIL: 训练失败: %s\n", st.error.c_str());
        return 1;
    }
    std::printf("训练 OK: %d 样本 / %d 类 / %d 维特征\n", st.samples, st.classes,
                st.featDim);

    ClsTrainer::Model m;
    try {
        m = ClsTrainer::loadModel(model.string());
    } catch (const cv::Exception &e) {
        std::printf("FAIL: 模型加载异常: %s\n", e.what());
        return 1;
    }
    if (!m.loaded) {
        std::printf("FAIL: 模型加载失败: %s\n", m.error.c_str());
        return 1;
    }
    std::printf("模型加载 OK: 类别=%s / %s, 特征模式=%d\n", m.classNames[0].c_str(),
                m.classNames[1].c_str(), m.featureMode);

    int correct = 0, total = 0;
    for (int i = 0; i < 12; ++i) {
        const int off = (i % 3) - 1;
        ClsTrainer::PredictResult r1 =
            ClsTrainer::predict(m, makeCircleImg(off));
        ++total;
        if (r1.ok && r1.className == "circle") ++correct;
        ClsTrainer::PredictResult r2 =
            ClsTrainer::predict(m, makeSquareImg(off));
        ++total;
        if (r2.ok && r2.className == "square") ++correct;
    }
    const double acc = double(correct) / double(total);
    std::printf("闭环预测: %d/%d 正确 (%.1f%%)\n", correct, total, acc * 100.0);
    if (acc < 0.9) {
        std::printf("FAIL: 准确率低于 90%%\n");
        return 1;
    }
    std::printf("ALL PASSED: 训练->保存->加载->推理 闭环可用\n");
    fs::remove_all(tmp);
    return 0;
}
