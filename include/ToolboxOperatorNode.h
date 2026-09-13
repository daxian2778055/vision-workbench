#pragma once

#include "HalconNode.h"

/// 内置工具箱算子种类（与 NodeFactory / 工具库条目一一对应）
enum class ToolboxBuiltin {
    Blur,
    Threshold,
    BlobAnalysis,
    EdgeDetection,
    Area,
    Distance,
    Conditional,
    Loop,
    DisplaySink,
    WriteFile
};

/// 对工具箱中「模糊 / 阈值 / Blob / …」等提供真实 Halcon 处理；逻辑/显示类为轻量透传或占位
class ToolboxOperatorNode : public HalconNode
{
public:
    explicit ToolboxOperatorNode(QObject *parent, ToolboxBuiltin kind);

    void init() override;
    void run(bool autoSwitch = true) override;

    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    ToolboxBuiltin builtinKind() const { return m_kind; }

private:
    ToolboxBuiltin m_kind {};

    static HalconCpp::HImage ensureGray(const HalconCpp::HObject &input);
};
