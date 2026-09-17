#pragma once

#include "HalconNode.h"
#include "DataObject.h"
#include <opencv2/dnn.hpp>
#include <QMap>
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
    static bool parseYoloOutput(const cv::Mat &out, int inputSize, const cv::Size &orig,
                                double confThresh, double nmsThresh,
                                QVector<DetectionBox> &result);

private:
    /// 获取或加载 ONNX 模型（带缓存，最多 10 个）
    cv::dnn::Net getOrLoadNet(const QString &modelPath);

    /// 读取类别名（显式路径优先，否则取模型同目录 classes.txt）
    QStringList loadClassNames(const QString &modelPath, const QString &explicitPath) const;

    static QMap<QString, cv::dnn::Net> s_netCache;
    QString m_currentModelPath;
};
