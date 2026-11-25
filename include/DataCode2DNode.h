#pragma once

#include "HalconNode.h"

/// 二维码/DataMatrix 识别算子
class DataCode2DNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DataCode2DNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    /// P2 复核：把"解码结果是否有有效内容"的判据抽成纯函数，供行为腿注入三组输入，
    /// 不再依赖真机码图（原判据改回 Length()>0 不会被任何测试判红）。
    static bool decodedContentIsValid(const QStringList &decoded);
};
