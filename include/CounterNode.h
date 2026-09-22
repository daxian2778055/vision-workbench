#pragma once

#include "HalconNode.h"
#include <QAtomicInt>
#include <QSpinBox>
#include <QComboBox>

/// 条件计数算子 — 输入条件为真时计数自增，输出累计次数
/// 对应 VM 4.4 的「条件计数」工具
class CounterNode : public HalconNode
{
    Q_OBJECT
public:
    explicit CounterNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool process() override;
    void setParam(const QString &name, const QVariant &value) override;
    QVariant getParam(const QString &name) const override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

    /// 累计次数（**运行期状态**，非参数镜像）：执行线程在 run() 里自增，本访问器任意线程可读。
    /// 本轮"运行期状态字段"专项：改用 QAtomicInt —— 只需"不发生撕裂/UB"，不需要排序语义，
    /// 故一律用 relaxed 版本（热路径近零开销）。注意：它不是参数，**不入参数表**（否则会被
    /// toJson 持久化，重开方案后"接着上次计数"，属静默语义变化）。
    int count() const { return m_count.loadRelaxed(); }

private:
    QAtomicInt m_count{0};        /// 累计次数（运行期状态，非参数镜像）
    // S1 残留收口：conditionMode / threshold 的唯一来源是参数表（默认值在 init() 写入），
    // 不再保留无锁成员镜像——此前 setParam 写成员、run() 读成员，与界面线程写参数构成无保护竞态。

    QComboBox *m_modeCombo = nullptr;
    QDoubleSpinBox *m_thresholdSpin = nullptr;
};
