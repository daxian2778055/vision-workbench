#pragma once

#include "HalconNode.h"
#include "DataObject.h"

/// 相机数字 IO 控制节点：通过 GlobalCameraManager 控制相机自身 IO 线
/// （读输入 / 驱输出；输出即可做频闪、硬触发、气缸、剔除信号）。
/// 对标 VM 4.4 的 IO 控制（FR16.12），但收敛为"仅相机 IO"（非独立 IO 板卡）。
/// 后端为海康 MVS SDK，走 GenICam 标准特性 LineSelector / LineMode / LineStatus。
class CameraIoNode : public HalconNode
{
    Q_OBJECT
public:
    explicit CameraIoNode(QObject *parent = nullptr);
    ~CameraIoNode() override;

    void init() override;
    /// 继承来的"输入图像"端口只是 HalconNode::init() 的副产品：IO 控制读的是"值/执行"两个
    /// 数据端口，不消费图像 ⇒ 不能被空输入守卫/产出守卫按图像处理节点要求。
    bool requiresInputImage() const override { return false; }
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
