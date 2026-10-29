#pragma once

#include "HalconNode.h"
#include "DataObject.h"
#include <opencv2/dnn.hpp>
#include <QMap>
#include <QMutex>
#include <QVector>

/// ONNX 目标检测节点（YOLO 系列）：cv::dnn 加载 ONNX 模型，输出类别 + 位置框。
/// 对应路线图 FR13.3 / G-P0-1（缺陷"定位+分类"最高频刚需），替代被禁用的 HALCON 区域检测链路。
/// 模型由外部训练导出（如 Ultralytics -> ONNX）；OpenCV 不负责训练。
class DnnDetectNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DnnDetectNode(QObject *parent = nullptr);
    ~DnnDetectNode() override;

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    /// 解析 YOLO 网络输出（兼容 v5 [1,N,C] 与 v8 [1,C,N] 布局），置信过滤 + NMS，
    /// 并把框坐标从网络输入空间（inputSize×inputSize）letterbox 映射回原图坐标。
    /// out: net.forward() 原始输出；orig: 原图尺寸。返回是否成功解析。
    /// yoloLayout: 0 = v8/v11（[N, 4+类数]，无 objectness）；1 = v5 / YOLOX（[N, 5+类数]）
    static bool parseYoloOutput(const cv::Mat &out, int inputSize, const cv::Size &orig,
                                double confThresh, double nmsThresh,
                                QVector<DetectionBox> &result, int yoloLayout = 0);

    /// 获取或加载 ONNX 模型（带缓存，最多 10 个）。public static（2026-10-28）：
    /// 流水线计数节点复用同一套加载/解码，避免复制一份 YOLO 前处理（复制就会漂移）。
    static cv::dnn::Net getOrLoadNet(const QString &modelPath);

    /// 读取类别名（显式路径优先，否则取模型同目录 classes.txt）。同上，供计数节点复用。
    static QStringList loadClassNames(const QString &modelPath, const QString &explicitPath);

    /// 输出布局候选标签（2026-10-28）：检测与计数两个节点共用，避免文案分叉
    static QStringList yoloLayoutLabels();

    /// YOLO 前处理（letterbox 保持长宽比 + 灰边 114 + blobFromImage）。检测与计数共用同一套，
    /// 避免复制漂移（2026-10-29 复核 P1-7：计数节点原先直接拉伸进正方形、且 scale 未登记为 0
    /// ⇒ 送进网络的是全零 blob，解码再按 letterbox 反算 ⇒ 框位也偏）。
    /// scale <= 0 直接返回 false（宁可不跑，也不静默出全零结果）。
    static bool yoloBlob(const cv::Mat &bgr, int inputSize, double scale, bool swapRB,
                         const cv::Scalar &mean, cv::Mat *blob);

    /// 共享推理互斥（2026-10-29 复核 P2-14）：s_netCache 是静态的，cv::dnn::Net 副本共享
    /// 同一份实现；两台执行器并发 forward（或一边改 target 一边 forward）会踩同一份缓存。
    /// 取网络与推理都在同一把锁内。
    static QMutex &dnnMutex();

    /// 检测与计数共用的参数标签与前处理失败提示（2026-10-29）：两个节点必须一致，
    /// 集中一处——复制同一句中文会被仓库的中文重复闸拦下。
    static QString labelScale();
    static QString labelSwapRB();
    static QString labelMean();
    static QString msgYoloPreprocessFailed();

private:

    static QMap<QString, cv::dnn::Net> s_netCache;
    QString m_currentModelPath;
};
