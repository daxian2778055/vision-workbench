#pragma once

// windows.h 的 min/max 宏会吃掉 std::max/std::min（本头常被排在 include 首位）
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
// 按 (路径, mtime, 参数) 缓存解码结果与重对象（2026-10-30 r19 P1-7）。
//
// 为什么要它：模板匹配每轮 imread 同一个模板文件、分类每轮 loadModel、特征匹配每轮
// 取 BLOB + imdecode + detectAndCompute、DeepOCR 每轮 CreateDeepOcr/ClearDlModel——
// 而这些输入在换型之前一个字节都不会变。键含 mtime：换文件自动重载。
//
// 返回的 cv::Mat 是**共享数据**（引用计数）：调用方只读使用；要改写请自行 clone。
#include <QString>
#include <memory>
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>

class ModelCache {
public:
    /// 解码图片（flags 见 cv::ImreadModes）。文件不存在/解码失败返回空 Mat。
    static cv::Mat imageFor(const QString &absPath, int flags);

    /// 模板关键点与描述子（按 模板键 + 检测器 + nFeatures 缓存）。
    /// templateKey 由调用方给（样本名/路径 + 内容标识）。
    static void keypointsFor(const QString &templateKey, int detectorIdx, int nFeatures,
                             const cv::Mat &tmpl,
                             std::vector<cv::KeyPoint> *kp, cv::Mat *desc);

    /// 换型/测试用：丢弃缓存（不影响已借出的数据）
    static void clear();
    static int size();
};
