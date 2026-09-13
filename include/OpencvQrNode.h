#pragma once

#include "HalconNode.h"
#include <QWidget>

/// OpenCV 二维码（QR）解码节点：替代替换版环境下不可用的 HALCON 条码链路（QR 部分）
class OpencvQrNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvQrNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
