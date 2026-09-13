#pragma once
// OpenCV ML 分类器训练/推理核心（纯 C++，供节点与测试共用）
// 训练：目录 <trainDir>/<类别名>/*.png|jpg|bmp -> ANN_MLP 模型 (.yaml)
// 推理：加载模型 -> 特征 -> 类别 + 置信度
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/ml.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace ClsTrainer {

struct TrainStats {
    bool ok = false;
    int samples = 0;
    int classes = 0;
    int featDim = 0;
    std::string error;
};

struct Model {
    bool loaded = false;
    int featureMode = 0;
    int featDim = 0;
    std::vector<std::string> classNames;
    cv::Ptr<cv::ml::ANN_MLP> mlp;
    std::string error;
};

struct PredictResult {
    bool ok = false;
    int classId = -1;
    std::string className;
    float confidence = 0.f;
    std::string error;
};

// 特征提取：mode 0 = HOG 64x64（1764 维，归一化到 [0,1]）
//           mode 1 = 32x32 灰度拉平（1024 维，除以 255）
inline bool extractFeature(const cv::Mat &img, int mode, std::vector<float> &feat)
{
    cv::Mat g;
    if (img.empty()) return false;
    if (img.channels() == 3)
        cv::cvtColor(img, g, cv::COLOR_BGR2GRAY);
    else if (img.channels() == 1)
        g = img;
    else
        return false;

    if (mode == 1) {
        cv::Mat small, f;
        cv::resize(g, small, cv::Size(32, 32), 0, 0, cv::INTER_AREA);
        small.convertTo(f, CV_32F);
        feat.assign(f.begin<float>(), f.end<float>());
        for (auto &v : feat) v /= 255.0f;
        return !feat.empty();
    }
    // HOG 64x64
    cv::Mat small;
    cv::resize(g, small, cv::Size(64, 64), 0, 0, cv::INTER_AREA);
    cv::HOGDescriptor hog(cv::Size(64, 64), cv::Size(16, 16), cv::Size(8, 8),
                          cv::Size(8, 8), 9);
    hog.compute(small, feat);
    float mx = 0.f;
    for (float v : feat) mx = std::max(mx, std::fabs(v));
    if (mx > 0.f)
        for (auto &v : feat) v /= mx;
    return !feat.empty();
}

// 训练：trainDir/<类别名>/*.img -> savePath(.yaml)
inline TrainStats trainFromDir(const std::string &dir, const std::string &savePath,
                               int featureMode, int hiddenNeurons, int maxIterations)
{
    TrainStats st;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        st.error = "训练目录不存在: " + dir;
        return st;
    }
    // 收集类别目录（按名字排序，保证标签稳定）
    std::vector<std::string> classDirs;
    for (const auto &e : fs::directory_iterator(dir, ec)) {
        if (!e.is_directory(ec)) continue;
        const std::string name = e.path().filename().string();
        if (!name.empty() && name[0] != '.') classDirs.push_back(name);
    }
    std::sort(classDirs.begin(), classDirs.end());
    if (classDirs.size() < 2) {
        st.error = "类别数不足（至少 2 个子目录）";
        return st;
    }

    std::vector<std::vector<float>> feats;
    std::vector<int> labels;
    const std::vector<std::string> exts = {".png", ".jpg", ".jpeg", ".bmp"};
    for (size_t ci = 0; ci < classDirs.size(); ++ci) {
        const fs::path cdir = fs::path(dir) / classDirs[ci];
        for (const auto &e : fs::directory_iterator(cdir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            const std::string ext = e.path().extension().string();
            std::string lower = ext;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (std::find(exts.begin(), exts.end(), lower) == exts.end()) continue;
            cv::Mat img = cv::imread(e.path().string(), cv::IMREAD_GRAYSCALE);
            if (img.empty()) continue;
            std::vector<float> f;
            if (!extractFeature(img, featureMode, f) || f.empty()) continue;
            feats.push_back(std::move(f));
            labels.push_back((int)ci);
        }
    }
    if ((int)feats.size() < (int)classDirs.size() * 2) {
        st.error = "样本不足（每类至少 2 张有效图）";
        return st;
    }
    const int dim = (int)feats[0].size();
    const int C = (int)classDirs.size();
    const int N = (int)feats.size();

    cv::Mat samples(N, dim, CV_32F);
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < dim; ++j)
            samples.at<float>(i, j) = feats[i][j];
    // one-hot 目标
    cv::Mat responses(N, C, CV_32F, cv::Scalar(0.f));
    for (int i = 0; i < N; ++i) responses.at<float>(i, labels[i]) = 1.f;

    cv::Ptr<cv::ml::ANN_MLP> mlp = cv::ml::ANN_MLP::create();
    const cv::Mat layers = (cv::Mat_<int>(1, 3) << dim, std::max(4, hiddenNeurons), C);
    mlp->setLayerSizes(layers);
    mlp->setActivationFunction(cv::ml::ANN_MLP::SIGMOID_SYM, 1.0, 1.0);
    mlp->setTrainMethod(cv::ml::ANN_MLP::BACKPROP, 0.001, 0.1);
    mlp->setTermCriteria(cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS,
                                          std::max(50, maxIterations), 1e-5));
    if (!mlp->train(samples, cv::ml::ROW_SAMPLE, responses)) {
        st.error = "训练失败";
        return st;
    }

    // 保存：先写模型根节点，再以 APPEND 模式追加元数据（同一 yaml 文件）
    try {
        mlp->save(savePath);
        cv::FileStorage fs(savePath, cv::FileStorage::APPEND);
        if (!fs.isOpened()) {
            st.error = "无法写入模型文件: " + savePath;
            return st;
        }
        fs << "featureMode" << featureMode;
        fs << "featDim" << dim;
        fs << "classCount" << C;
        for (int i = 0; i < C; ++i)
            fs << ("className_" + std::to_string(i)) << classDirs[i];
        fs.release();
        {
            const fs::path clsPath = fs::path(savePath).parent_path() / "classes.txt";
            FILE *fp = nullptr;
#ifdef _WIN32
            fopen_s(&fp, clsPath.string().c_str(), "w");
#else
            fp = std::fopen(clsPath.string().c_str(), "w");
#endif
            if (fp) {
                for (const auto &n : classDirs)
                    std::fprintf(fp, "%s\n", n.c_str());
                std::fclose(fp);
            }
        }
    } catch (const cv::Exception &e) {
        st.error = std::string("保存模型异常: ") + e.what();
        return st;
    }

    st.ok = true;
    st.samples = N;
    st.classes = C;
    st.featDim = dim;
    return st;
}

inline Model loadModel(const std::string &path)
{
    Model m;
    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        m.error = "无法打开模型文件: " + path;
        return m;
    }
    m.featureMode = (int)fs["featureMode"];
    m.featDim = (int)fs["featDim"];
    const int C = (int)fs["classCount"];
    for (int i = 0; i < C; ++i) {
        std::string name;
        fs["className_" + std::to_string(i)] >> name;
        m.classNames.push_back(name);
    }
    const cv::FileNode fn = fs["opencv_ml_ann_mlp"];
    fs.release();
    if (fn.empty()) {
        m.error = "模型文件缺少 ANN_MLP 数据";
        return m;
    }
    // 按文件名重新打开读模型根（比 FileNode 加载更稳，兼容 APPEND 元数据共存）
    m.mlp = cv::ml::ANN_MLP::load(path);
    if (m.mlp.empty()) {
        m.error = "ANN_MLP 加载失败";
        return m;
    }
    m.loaded = true;
    return m;
}

inline PredictResult predict(const Model &m, const cv::Mat &img)
{
    PredictResult r;
    if (!m.loaded || m.mlp.empty()) {
        r.error = m.error.empty() ? "模型未加载" : m.error;
        return r;
    }
    std::vector<float> f;
    if (!extractFeature(img, m.featureMode, f) || f.empty()) {
        r.error = "特征提取失败";
        return r;
    }
    if ((int)f.size() != m.featDim) {
        r.error = "特征维度与模型不符";
        return r;
    }
    cv::Mat row(1, (int)f.size(), CV_32F);
    for (size_t j = 0; j < f.size(); ++j) row.at<float>(0, (int)j) = f[j];
    cv::Mat out;
    m.mlp->predict(row, out);
    if (out.empty()) {
        r.error = "预测失败";
        return r;
    }
    // softmax 归一化求置信度
    float mx = -1e30f;
    int best = -1;
    for (int i = 0; i < out.cols; ++i) {
        const float v = out.at<float>(0, i);
        if (v > mx) { mx = v; best = i; }
    }
    float sum = 0.f;
    for (int i = 0; i < out.cols; ++i) sum += std::exp(out.at<float>(0, i) - mx);
    r.ok = true;
    r.classId = best;
    r.className = (best >= 0 && best < (int)m.classNames.size()) ? m.classNames[best]
                                                                 : std::string();
    r.confidence = (sum > 0.f) ? (1.f / sum) : 0.f;
    return r;
}

}  // namespace ClsTrainer
