#pragma once

#include "HalconNode.h"
#include "DataObject.h"

/// 模板匹配算子（形状匹配）：支持从图像训练并持久化模板，或加载外部 .shm 模板
class TemplateMatchNode : public HalconNode
{
    Q_OBJECT
public:
    explicit TemplateMatchNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    double matchedScore() const { return m_params.value(QStringLiteral("score"), 0.0).toDouble(); }

private:
    /// 输出匹配结果到图像端口与 Measure 端口
    void setMatchOutput(const HalconCpp::HImage &gray, double row, double col,
                        double score, int numFound, bool found);

    HalconCpp::HTuple m_modelId;      // Template ID after creation/loading
    bool m_modelCreated = false;
    QString m_loadedTemplatePath;     // 已加载模板文件路径（缓存，避免每帧重复读文件）
};
