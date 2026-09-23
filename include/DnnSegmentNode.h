#pragma once

#include "HalconNode.h"
#include "DataObject.h"
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <QMap>
#include <QVector>

/// ONNX 分割节点：cv::dnn 加载 ONNX 模型，输出**像素级**掩膜。
/// 对应路线图 FR13.4 / G-P0-2（深度学习无分割），是 DnnDetectNode（检测）之后的下一环。
/// 模型由外部训练导出（Ultralytics / PyTorch -> ONNX）；OpenCV 不负责训练。
///
/// 支持两类主流输出布局（`segMode` = 自动 / 语义 / 实例）：
///   · 语义分割（UNet / DeepLab / FCN 等）：单个 [1, C, H, W] 输出
///       C == 1 → sigmoid（可关）+ 阈值；C >= 2 → argmax == classIndex；
///   · 实例分割（YOLO-Seg v8/v11-seg）：双输出
///       out0 = [1, 4+numClasses+K, N]（cx,cy,w,h + 类分数 + K 个掩膜系数）
///       out1 = [1, K, mh, mw]（掩膜原型），掩膜 = 系数 · 原型 > 0。
///
/// 输入预处理与掩膜逆映射（重要，含一个实测陷阱）：
///   · `keepRatio=false`（**默认**）：直接拉伸到输入尺寸，掩膜按同一比例反拉伸 → 几何精确、无灰边污染；
///   · `keepRatio=true`：letterbox 保比例，灰边填 `paddingValue`（默认 114，应与训练/导出时一致）。
///     ⚠ 实测陷阱：灰边会被模型当成前景，而卷积会把这份前景**向有效区内扩一个感受野宽度**——
///     裁边只能去掉灰边本身，去不掉这条贯通污染带（96×64 图片 + 3×3 卷积模型实测：掩膜外接框变成
///     整幅 (0,0,96,64)、面积 866 而非 484；真模型上表现为"图像上下边缘各一条假缺陷带"）。
///     故 letterbox 下按 `padTrim`（默认 1，单位＝网络输入空间像素）清零紧邻灰边的行/列；
///     感受野更大（深 UNet 等）时把 padTrim 调大即可。
///
/// 为什么**不输出 HALCON 区域（Region）端口**（重要，勿按"惯例"补）：
///   本仓对替换版环境的既有实证是**区域算子跨进程反转**（`ThresholdNode` 与形态学 6 节点因此被移出
///   注册，见 NodeRegistry.cpp 注释）；故所有 OpenCV 替代节点（OpencvBlob / OpencvMorph / OpencvThreshold …）
///   一律**不构造 HRegion**，只走图像域 + 数值端口。本节点沿用该纪律：掩膜以**图像**形式输出
///   （8U 0/255、原图尺寸），下游可接 OpencvMorph（去噪）/ OpencvBlob（连通域与面积统计）等图像域节点，
///   链路完整且不引入跨进程风险。
class DnnSegmentNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DnnSegmentNode(QObject *parent = nullptr);
    ~DnnSegmentNode() override;

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    /// 语义分割输出后处理：接受 [1,C,H,W] / [C,H,W] / [H,W]（C==1 时忽略 classIndex）。
    /// C == 1：useSigmoid 时先 sigmoid 再按 thresh 二值化（thresh=0.5 + sigmoid 等价于 logit > 0）；
    /// C >= 2：逐像素 argmax，取 == classIndex 者为前景（argmax 对单调变换不变，故无需知道是否 softmax）。
    /// mask 输出：CV_8U 0/255，尺寸 = 输出空间（网络输出分辨率，未映射回原图）。
    /// classIndex 越界（C >= 2 时）返回 false。
    static bool parseSemanticOutput(const cv::Mat &out, int classIndex, double thresh,
                                    bool useSigmoid, cv::Mat &mask);

    /// 判断**单通道**输出是否还需要再做一次 sigmoid（mode：0 自动 / 1 强制开 / 2 强制关）。
    ///
    /// 为什么要有"自动"：这是实测踩到的静默陷阱——不少 ONNX 分割模型**图里已含 Sigmoid**（导出即概率），
    /// 若再压一次：sigmoid(x) > 0.5 ⟺ x > 0，而概率背景恰好 = 0.5 → 判据恒真，
    /// **整幅图都变前景**（实测：应 486 px 的掩膜变成 4096 px 全图，且不报任何错）。
    /// 自动判据（只对 C == 1 生效，多类走 argmax 与此无关）：
    ///   · 值域**越出 [0,1]**（存在 < 0 或 > 1 的值）⇒ 未经压缩的 logits ⇒ 需要 sigmoid；
    ///   · 完全落在 [0,1] 内 ⇒ 视为已是概率 ⇒ 不再 sigmoid。
    /// 该判据与实际不符时可用"强制开/关"覆盖（结果面板的 sigmoidApplied 会显示本次实际取值）。
    static bool resolveSigmoid(const cv::Mat &out, int mode);

    /// YOLO-Seg（v8/v11-seg）双输出后处理：
    ///   out0 = [1, 4+numClasses+K, N]、out1 = [1, K, mh, mw]
    /// 假设 R = min(dim1, dim2) 为通道维、N 为锚点数（真实模型如 116 × 8400；与 DnnDetectNode
    /// 对 v8 布局的同类假设一致），故 R > 4 + K 必须成立，否则返回 false。
    /// 掩膜判据沿用 Ultralytics 语义：**coeffs · protos > 0**（严格大于，= 0 视为背景）。
    /// 输出：mask（CV_8U 0/255，**网络输入空间** inputW×inputH）+ 每实例框（同空间，已按框裁剪）。
    static bool parseYoloSegOutput(const cv::Mat &out0, const cv::Mat &out1,
                                   int inputW, int inputH, double confThresh, double nmsThresh,
                                   cv::Mat &mask, QVector<DetectionBox> &boxes);

    /// 掩膜连通域统计（8 连通）：保留面积 >= minArea 的连通域，**原地回写清理后的掩膜**
    /// （不达标的小域从掩膜里一并去掉，保证 掩膜/面积/数量/框 四者一致），返回保留个数。
    /// boxes：各连通域外接框；约定 classId = -1、confidence = **该连通域像素面积**（连通域模式下
    /// confidence 字段承载面积，实例模式下才承载置信度——由调用方按模式解释）。
    static int maskToBoxes(cv::Mat &mask, int minArea, QVector<DetectionBox> &boxes);

private:
    /// 获取或加载 ONNX 模型（带缓存，最多 10 个；与 Detect / Infer 节点的缓存**隔离**，互不淘汰）
    cv::dnn::Net getOrLoadNet(const QString &modelPath);

    /// 读取类别名（显式路径优先，否则取模型同目录 classes.txt）
    QStringList loadClassNames(const QString &modelPath, const QString &explicitPath) const;

    static QMap<QString, cv::dnn::Net> s_netCache;
};
