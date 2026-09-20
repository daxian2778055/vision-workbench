#pragma once

// HImage 图容器基类；日常算法由派生类用 OpenCV 实现，HALCON 算法仅 DeepOCR。见 VisionIntegrationPolicy.h
#include "VisionIntegrationPolicy.h"
#include "NodeBase.h"
#include "ParamSpec.h"
#include "HalconWindow.h"
#include <QImage>
#include <halconcpp/HalconCpp.h>
#include "ThreadSafeParams.h"

using namespace HalconCpp;

class QFormLayout;
class QTimer;

class HalconNode : public NodeBase
{
    Q_OBJECT

public:
    HalconNode(QObject *parent = nullptr);
    virtual ~HalconNode();

    virtual void init() override;
    virtual void run(bool autoSwitch = true) override;
    virtual void setParam(const QString &name, const QVariant &value) override;
    virtual QVariant getParam(const QString &name) const override;
    virtual QList<QString> getAllParamNames() const override { return m_params.keys(); }
    bool hasParam(const QString &name) const;
    virtual void drawResult() override;
    virtual QJsonObject toJson() const override;
    virtual void fromJson(const QJsonObject &json) override;
    virtual bool process() override;
    virtual QWidget *createParamPanel() override;
    virtual void updateParamPanel(QWidget *panel) override;
    virtual void displayImage() override;

    HObject getInputImage() const;
    void setInputImage(const HObject &image);
    HObject getOutputImage() const;

    // ---- 声明式参数系统 ----
    /// 注册单个参数描述（默认值会自动写入 m_params）
    void registerParam(const ParamSpec &spec);
    /// 批量注册参数描述
    void registerParams(const ParamSpecList &specs);
    /// 获取参数描述列表
    const ParamSpecList &paramSpecs() const { return m_paramSpecs; }
    /// 依据 ParamSpec 自动生成参数面板（供未覆盖 createParamPanel 的算子使用）
    QWidget *createAutoParamPanel();
    /// 依据 ParamSpec 刷新参数面板控件值
    void updateAutoParamPanel(QWidget *panel);

    // ---- 实时预览功能 ----
    /// 是否启用参数修改后自动预览
    bool autoPreviewEnabled() const { return m_autoPreviewEnabled; }
    /// 设置自动预览开关
    void setAutoPreviewEnabled(bool enabled);
    /// 触发延迟预览（参数修改后调用）
    void triggerDelayedPreview();

    /// 模块编辑器：该算子需要在图上画的几何（None = 无需）
    virtual RoiType geometryRoiType() const { return RoiType::None; }
    /// 当前几何（用于回显）
    virtual RoiShape geometryRoi() const { return RoiShape(); }
    /// 图上画完后写回参数
    virtual void applyGeometryRoi(const RoiShape &shape) { Q_UNUSED(shape); }
    /// 是否支持掩膜涂抹（阈值/Blob 等）
    virtual bool supportsMaskEdit() const { return false; }

    QImage editMask() const { return m_editMask; }
    void setEditMask(const QImage &mask);
    void clearEditMask();
    bool hasEditMask() const { return !m_editMask.isNull(); }

signals:
    /// 参数修改后自动预览完成信号
    void previewCompleted(NodeBase *node, bool success);
    /// 单次执行耗时信号（供性能面板接收）
    void executionTimeMeasured(NodeBase *node, qint64 elapsedMs);

protected:
    HObject m_inputImage;
    HObject m_outputImage;
    /// 线程安全参数表（内部 QReadWriteLock）：界面线程与执行线程可并发访问。
    /// 支持 m_params[k] = v（加锁写代理）/ value / contains / keys / insert / remove / clear；
    /// 需要整体遍历时用 m_params.snapshot()（切勿遍历本对象）。
    /// 新增代码建议统一走访问器（setParam / getParam / setParamDirect）。
    ThreadSafeParams m_params;
    ParamSpecList m_paramSpecs; /// 参数描述（用于自动面板/序列化范围校验）
    QImage m_editMask;          /// 可选掩膜（白=保留，黑=忽略）

    /// 加锁写入、不触发延迟预览（供子类写状态类参数）
    void setParamDirect(const QString &name, const QVariant &value);

    // 实时预览相关
    bool m_autoPreviewEnabled = false;  /// 是否启用自动预览
    QTimer *m_previewTimer = nullptr;   /// 延迟预览定时器
    static const int PREVIEW_DELAY_MS = 300;  /// 预览延迟毫秒数

    /// 执行单次预览（子类可重写自定义预览行为）
    virtual void executePreview();
};
