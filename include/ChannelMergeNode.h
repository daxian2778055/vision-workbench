#pragma once

#include "HalconNode.h"

/// 通道合并：把 2–4 个单通道图像合并为一个多通道图像（VM「图像合并」类）。
/// 输入端口按通道序排列；实际合并的通道数 = 有有效输入图像的端口数（至少 2）。
class ChannelMergeNode : public HalconNode
{
    Q_OBJECT

public:
    explicit ChannelMergeNode(QObject *parent = nullptr);
    ~ChannelMergeNode() override;

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
