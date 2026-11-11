#pragma once

#include "HalconNode.h"

/// 通道拆分：把多通道图像拆成独立的单通道图像输出（VM「图像分割」类）。
/// 3 通道 → 3 个灰度输出（R/G/B 或 H/S/V）；2/4 通道按实际通道数输出。
class ChannelSplitNode : public HalconNode
{
    Q_OBJECT

public:
    explicit ChannelSplitNode(QObject *parent = nullptr);
    ~ChannelSplitNode() override;

    void init() override;
    bool process() override;
    bool execute();

    virtual QJsonObject toJson() const override;
    virtual void fromJson(const QJsonObject &json) override;
    virtual QWidget *createParamPanel() override;
    virtual void updateParamPanel(QWidget *panel) override;

private:
    static const int kMaxChannels = 4;
};
