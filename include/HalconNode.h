#pragma once

#include <QMutex>
#include <QMutexLocker>

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
    HalconNode(QObject *parent = nullptr, bool withDefaultImagePort = true);
    bool m_withDefaultImagePort = true;   /// 是否带默认图像输出（统一输出用）
    virtual ~HalconNode();

    virtual void init() override;
    virtual void run(bool autoSwitch = true) override;
    /// VM「模块结果」默认口径：所有算子至少展示 耗时 / 模块状态；
    /// 矩形 ROI 的算子再补「检测区域」中心/宽/高/角度（值由 FlowExecutor 统一回写）。
    QVector<NodeResultField> resultDisplayFields() const override;
    /// B1 统一输出：第一个输出端口 = 唯一「输出」，第 2 个及以后只登记为具名子结果
    virtual Port *addOutputPort(const QString &name, PortDataType dataType = PortDataType::Image) override;
    bool aggregatesOutputPorts() const override { return true; }
    int logicalOutputPortCount() const override { return 1 + m_logicalOutputs.size(); }
    virtual QVector<QPair<QString, QSharedPointer<DataObject>>> namedOutputs() const override;
    /// 逻辑子输出名（不含主端口），顺序 = 原端口 1..N 的顺序
    QVector<QPair<QString, PortDataType>> logicalOutputs() const { return m_logicalOutputs; }
    virtual void setParam(const QString &name, const QVariant &value) override;
    virtual QVariant getParam(const QString &name) const override;
    virtual QList<QString> getAllParamNames() const override { return m_params.keys(); }
    /// 参数内核共享句柄（切片 C 立项书 P2 期段一）：快照捕获时随快照列入列，
    /// 壳先亡（场景销毁）时参数表仍可读（§13.1 净收益）。
    std::shared_ptr<ThreadSafeParams::Core> paramCore() const override { return m_params.core(); }
    bool hasParam(const QString &name) const;
    /// 图像对本节点是否为必需输入：本轮没有可用输入图像时，process() 据此判失败而非静默成功。
    /// 默认按"是否声明了 Image 类型输入端口"判定；图像只是可选旁路（条件判断、脚本执行等）
    /// 的节点覆写返回 false。
    virtual bool requiresInputImage() const;
    /// 本节点"成功"是否必须产出一张图像：用于 process() 的产出守卫。run() 里只
    /// `m_outputImage.Clear(); return;` 而不写 moduleStatus=false 的分支会被契约翻转
    /// 判成绿灯，故凡"既要求输入图像、0 号输出端口又对外承诺图像"的节点必须真有产出。
    /// 确实只产测量值/区域而不透传图像的节点覆写返回 false。
    virtual bool requiresImageOutput() const;
    /// ---- 以下两条是上面两条图像判据的**数据侧孪生**（W-2），由 processDataOutputs() 执行 ----
    /// 本轮"成功"所必填的数据输入端口索引。默认 = 本节点声明的全部非 Image 输入端口：
    /// 数据变换类算子的输出必须由输入数据算出来，输入空着仍吐"参数派生的常量"就是静默绿灯
    /// （分类空载出"中"、筛选空载出 false、格式化空载出模板）。
    /// 只有端口本身是**可选操作数**的节点才覆写：FormulaNode 的必填集 = 表达式实际引用到的 pN。
    virtual QSet<int> requiredInputDataPorts() const;
    /// 本节点"成功"是否必须产出至少一路数据输出。用于抓 run() 里 `if (...) return;`
    /// 却不写 moduleStatus=false 的分支（FormatNode 空模板 / ProtocolParseNode 空串 / FormulaNode 求值失败）。
    /// 确实只写副作用（落库/发送）而不承诺数据的节点覆写返回 false。
    virtual bool requiresDataOutput() const;
    virtual void drawResult() override;
    virtual QJsonObject toJson() const override;
    virtual void fromJson(const QJsonObject &json) override;
    virtual bool process() override;
    /// 框架级公共结果字段：在基类 elapsedMs 之上补矩形 ROI 的检测区域字段。
    /// 判断与结果表快照共用同一份取值（FlowExecutor::injectCommonResultVars 同源）。
    QVariantMap commonResultFields(qint64 elapsedMs) const override;
    /// VM「结果显示 → 结果判断」通用条件（resJudgeMode + resJudgeConditions JSON）。
    /// 兼容旧调用点：转发到 NodeBase::evaluateResultJudgment（空公共字段）。
    /// 实际判断统一由 NodeBase::execute() 调用，见该函数。
    bool evaluateGenericResultJudgment(QString *reason) const;
    virtual QWidget *createParamPanel() override;
    virtual void updateParamPanel(QWidget *panel) override;
    /// P2-8：把所有声明了默认值的参数恢复为默认值（不触发自动预览，由调用方决定何时重算）。
    /// 返回实际恢复的参数个数。
    int restoreDefaultParams();
    virtual void displayImage() override;

    HObject getInputImage() const;
    void setInputImage(const HObject &image);
    HObject getOutputImage() const;

    // ---- 声明式参数系统 ----
    /// 注册单个参数描述（默认值会自动写入 m_params）
    void registerParam(const ParamSpec &spec);
    /// 补充参数说明（参数面板 tooltip 与「帮助信息」页共用）；参数名不存在时忽略。
    /// 用于 registerParams 之后再补说明，避免每个节点重复抄整份 ParamSpec。
    void setParamTooltip(const QString &name, const QString &tooltip);
    /// 批量注册参数描述
    void registerParams(const ParamSpecList &specs);
    /// 获取参数描述列表
    const ParamSpecList &paramSpecs() const { return m_paramSpecs; }
    /// 每轮执行前按 ParamSpec 归一化运行时参数：覆盖 setParamDirect / fromJson /
    /// 参数引用解析 / 直接写 m_params 等绕过 setParam 范围钳制的路径，
    /// 避免越界值直达算法造成除零、负 vector size、巨核分配或循环爆炸。
    void normalizeRuntimeParams();
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
    /// 编辑器可视化：该算子真正参与匹配的特征点（图像坐标）。默认无；
    /// 模板匹配把它画成绿点——训练完能直接看到"哪些边缘被用来打分"。
    virtual QVector<QPointF> editorFeaturePoints() const { return {}; }
    /// 是否支持掩膜涂抹（阈值/Blob 等）
    virtual bool supportsMaskEdit() const { return false; }

    /// 掩膜读取：加锁并返回**拷贝**（QImage 隐式共享，跨线程直接读裸成员会与 UI 侧写形成竞态）
    QImage editMask() const;
    void setEditMask(const QImage &mask);
    void clearEditMask();
    bool hasEditMask() const;

signals:
    /// 参数修改后自动预览完成信号
    void previewCompleted(NodeBase *node, bool success);
    /// 单次执行耗时信号（供性能面板接收）
    void executionTimeMeasured(NodeBase *node, qint64 elapsedMs);

protected:
    /// 数据型节点的 process() 契约实现（W-2）：把 `bool process() { run(); return true; }`
    /// 换成 `return processDataOutputs();`。四步与 process() 对图像侧做的事一一对应：
    /// 空载守卫 → 预置成功位 → run() + 失败传播 → 产出守卫；并每轮先清空输出，
    /// 使 run() 里"提前 return 不写输出"的分支不会把上一轮结果当本轮结果。
    bool processDataOutputs();
    /// 端口本轮是否有可用数据：DataObject 存在且 getData() 是有效值。
    /// **不吃图像**——把图像连线接到 Number/String 端口上不算有效输入。
    bool inputDataPresent(int portIndex) const;

    HObject m_inputImage;
    HObject m_outputImage;
    /// B1：本算子声明过的逻辑输出（第 2..N 个），只保留名称/类型，不再造 Port
    QVector<QPair<QString, PortDataType>> m_logicalOutputs;
    /// 主端口在子类 init() 里声明的类型（Image=承诺吐图；Number/Measure=只出数据）
    PortDataType m_declaredMainPortType = PortDataType::Image;
    bool m_mainPortDeclared = false;
    /// 主端口原名（"输出"/"匹配结果"/"计算结果"…；聚合后主结果沿用这个名字）
    QString m_mainPortName = QStringLiteral("输出");
    /// 清空全部逻辑输出（含真实端口与已折叠的子输出）
    void clearAllOutputData();
    /// 把本轮各逻辑输出合并成一个对象写到唯一端口
    void publishAggregatedOutputs();
    /// 线程安全参数表（内部 QReadWriteLock）：界面线程与执行线程可并发访问。
    /// 支持 m_params[k] = v（加锁写代理）/ value / contains / keys / insert / remove / clear；
    /// 需要整体遍历时用 m_params.snapshot()（切勿遍历本对象）。
    /// 新增代码建议统一走访问器（setParam / getParam / setParamDirect）。
    ThreadSafeParams m_params;
    ParamSpecList m_paramSpecs; /// 参数描述（用于自动面板/序列化范围校验）
    QImage m_editMask;          /// 可选掩膜（白=保留，黑=忽略）
    /// S1 残留专项：掩膜由 UI 线程写（ModuleEditorDialog → setEditMask）、执行线程读
    /// （OpencvBlob/Threshold/Defect 经 editMask()）——统一由这把锁保护，读写都不碰裸成员。
    mutable QMutex m_maskMutex;

    /// 加锁写入、不触发延迟预览（供子类写状态类参数）
    void setParamDirect(const QString &name, const QVariant &value);

    // 实时预览相关
    bool m_autoPreviewEnabled = false;  /// 是否启用自动预览
    QTimer *m_previewTimer = nullptr;   /// 延迟预览定时器
    static const int PREVIEW_DELAY_MS = 300;  /// 预览延迟毫秒数

    /// 执行单次预览（子类可重写自定义预览行为）
    virtual void executePreview();
};
