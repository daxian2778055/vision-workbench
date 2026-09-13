#pragma once

#include "HalconNode.h"
#include <opencv2/dnn.hpp>
#include <QMap>

/// ONNX 深度学习推理节点：cv::dnn 加载 ONNX 模型做分类推理
/// （模型由外部训练导出，如 PyTorch -> ONNX；OpenCV 不负责训练）
class DnnInferNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DnnInferNode(QObject *parent = nullptr);
    ~DnnInferNode() override;

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

private:
    /// 获取或加载 ONNX 模型（带缓存）
    cv::dnn::Net getOrLoadNet(const QString &modelPath);

    /// 清除模型缓存
    void clearNetCache();

    /// 模型缓存：路径 -> 网络
    static QMap<QString, cv::dnn::Net> s_netCache;
    /// 当前使用的模型路径（用于检测路径变化）
    QString m_currentModelPath;
};
