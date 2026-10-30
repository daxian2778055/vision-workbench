#pragma once
// 线程安全的 ONNX 模型缓存（2026-10-30 r19 P0-1 / P1-9）。
//
// 为什么要有这一个：此前四个 DNN 节点各自持静态 QMap<QString, Net>，contains/[] 无互斥
// （多流程并发是产品支持项 ⇒ 两个工作线程同时碰一张表 = UB，P0-1）；而 DnnDetectNode /
// ObjectCountingNode 用一把**全局** QMutex，从取网络一路锁到 NMS（P1-9），两个流程同跑
// 就退回单流吞吐。
//
// 这里的口径：
//   · 一张模型一个条目，条目内自带互斥：**同一模型**的 setInput/forward 串行，
//     **不同模型可以并发**；
//   · 后端/目标只在**加载时**设置一次（cv::dnn::Net 的 setPreferable* 不是线程安全的，
//     且每帧重设是纯浪费）；
//   · 键含 mtime：同路径换模型会重新加载（否则现场换文件不生效）；
//   · 前后处理（letterbox/NMS 等）在锁外做，锁只包 setInput/forward。
#include <QString>
#include <QMutex>
#include <memory>
#include <functional>
#include <opencv2/dnn.hpp>

struct DnnNetEntry {
    cv::dnn::Net net;
    QMutex mutex;          // 保护 setInput / forward（同一模型串行）
    QString absPath;
    qint64 mtime = 0;      // 加载时的文件时间，变了就重新加载
    int backend = 0;       // 记录用，便于排障
};

class DnnNetCache {
public:
    using InjectionHook = std::function<cv::dnn::Net(const QString &)>;
    using ReadyHook = std::function<void(const QString &, const cv::dnn::Net &)>;

    /// 测试钩子：注入假模型（跳过磁盘读取）。空实现 = 正常走磁盘。
    static void setInjectionHook(InjectionHook hook);
    /// 加载完成回调（测试观察用）
    static void setReadyHook(ReadyHook hook);

    /// 取（必要时加载）模型。useOpenCL 只在**首次加载**时生效。
    /// 失败返回空指针，并把原因写进 error（可空）。
    static std::shared_ptr<DnnNetEntry> acquire(const QString &absPath, bool useOpenCL,
                                                QString *error);

    /// 两个节点共用的失败文案（跨文件同句只留一处，中文重复闸要求）
    static QString textLoadFailed();

    /// 换型/测试用：丢弃缓存。**不影响已经借出的条目**（它们的互斥仍然有效）。
    static void clear();
    static int size();

private:
    DnnNetCache() = delete;
};
