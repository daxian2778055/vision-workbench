#pragma once

#include "HalconNode.h"
#include <QProcess>
#include <QStringList>

/// Script execution node that runs Python/Lua scripts inline
class ScriptNode : public HalconNode
{
    Q_OBJECT

public:
    explicit ScriptNode(QObject *parent = nullptr);

    void init() override;
    /// 图像对脚本是可选输入（纯计算脚本无图也应执行），空输入不得判失败
    bool requiresInputImage() const override { return false; }
    void run(bool autoSwitch) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private:
    QString executePythonScript(const QString &script, const QStringList &args);
    QString executeLuaScript(const QString &script, const QStringList &args);
    /// 可取消等待脚本进程结束：周期性轮询，流程被停止时立即终止进程并返回（P4）
    bool waitCancellable(QProcess &process, int maxMs, QString &errOut);

    /// 最近一次执行是否失败（启动失败/超时/取消/沙箱不可用/解释器非正常退出）
    bool m_lastRunFailed = false;
};
