#pragma once

#include "HalconNode.h"

/// 输出图像节点（图像采集分组）：把当前输入图像按目录 + 命名规则保存到硬盘。
/// 与「写入文件」(WriteFileNode) 的区别：那个是固定单文件覆盖，本节点按规则逐张命名，
/// 便于产线连续存图/追溯；启用「大文件存图」策略时自动改为攒批写 .dat，不绕过策略。
class OutputImageNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OutputImageNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    /// 本节点在 run() 里自行检查空输入并写 lastError，不用基类的无因判红
    bool requiresInputImage() const override { return false; }
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    /// 按 namePattern 展开出本次保存的完整路径（公开供测试/日志使用）。
    /// overwrite=false 时自动在末尾追加序号避开同名；返回空串表示参数非法。
    QString resolveSavePath(QString *error = nullptr) const;
};
