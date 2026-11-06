#include "OpencvCaliperNode.h"
#include "NodeResultFields.h"
#include "VmSharedText.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <QElapsedTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSet>
#include <QHash>
#include <QWidget>

namespace {

/// 给参数补分组标题（自动面板按组渲染）
ParamSpec grouped(ParamSpec spec, const QString &group)
{
    spec.group = group;
    return spec;
}

/// 单个亚像素边缘（图像坐标 + 对比度 + 上升/下降方向）
struct EdgeHit {
    double col = 0.0;
    double row = 0.0;
    double contrast = 0.0;
    double score = 0.0;    ///< VM 极值点评分（0~1）；无计分项时 = contrast/255
    double offset = 0.0;   ///< 相对投影区域中心沿法线的位移（位置评分输入）
    double gray = 0.0;     ///< 边缘处灰度（灰度评分输入）
    int polarity = 3;      ///< 官方结果：1=从黑到白，2=从白到黑，3=任意极性
    bool rising = false;   /// true=沿法线方向由暗到亮（梯度为正）
    int profile = 0;       /// 所属剖面序号（用于"按位置"排序）
};

/// 边对（两条极性相反的边缘，配对由评分贪心决定，不再按固定宽度筛）
struct PairHit {
    double col1 = 0.0;
    double row1 = 0.0;
    double col2 = 0.0;
    double row2 = 0.0;
    double width = 0.0;
    double contrast = 0.0;
    double score = 0.0;    ///< VM 极值点评分（0~1）
    double offset = 0.0;   ///< 边对中心相对投影区域中心沿法线的位移
    double gray = 0.0;     ///< 两条边缘的灰度均值
    int polarity = 3;      ///< 首边极性（官方 1=黑到白 / 2=白到黑 / 3=任意）
    int profile = 0;
};

}   // namespace

OpencvCaliperNode::OpencvCaliperNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("卡尺测量"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvCaliperNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("边缘点"), PortDataType::Measure);
    addOutputPort(QStringLiteral("边缘数"), PortDataType::Number);
    addOutputPort(QStringLiteral("边缘对"), PortDataType::Measure);
    registerParams({
        // ---- ROI（与画布上的卡尺线一致）----
        grouped(makeDoubleParam(QStringLiteral("row1"), 100.0, 0.0, 100000.0,
                                QStringLiteral("起点行")),
                QStringLiteral("卡尺")),
        grouped(makeDoubleParam(QStringLiteral("col1"), 100.0, 0.0, 100000.0,
                                QStringLiteral("起点列")),
                QStringLiteral("卡尺")),
        grouped(makeDoubleParam(QStringLiteral("row2"), 200.0, 0.0, 100000.0,
                                QStringLiteral("终点行")),
                QStringLiteral("卡尺")),
        grouped(makeDoubleParam(QStringLiteral("col2"), 100.0, 0.0, 100000.0,
                                QStringLiteral("终点列")),
                QStringLiteral("卡尺")),
        grouped(makeIntParam(QStringLiteral("halfWidth"), 10, 1, 200,
                             QStringLiteral("法线方向测量半宽"), QStringLiteral("px")),
                QStringLiteral("卡尺")),
        grouped(makeDoubleParam(QStringLiteral("step"), 5.0, 1.0, 100.0,
                                QStringLiteral("沿直线采样步长"), QStringLiteral("px")),
                QStringLiteral("卡尺")),
        // VM「投影方向」：官方「纵向投影…投向矩形宽 / 横向投影…投向矩形高」。
        // 这里「投向 X」= 一维信号沿 X 方向排列。0=横向（信号沿法线，老行为）、
        // 1=纵向（信号沿卡尺线）；两者共用同一个矩形 ROI，只是交换采样轴。
        grouped(makeEnumParam(QStringLiteral("projectionDirection"), 0,
                              {QStringLiteral("横向投影（投向矩形高）"),
                               QStringLiteral("纵向投影（投向矩形宽）")},
                              QStringLiteral("投影方向")),
                QStringLiteral("卡尺")),
        // VM「模糊边缘」：官方「开启后，可增强候选边缘点集的提取能力，获取更多候选点集数……
        // 但耗时增加明显」。当前实现为把极值门槛降到一半以纳入更弱的候选，未复刻其额外计算开销。
        grouped(makeBoolParam(QStringLiteral("blurEdge"), false,
                              QStringLiteral("模糊边缘（纳入更弱候选）")),
                QStringLiteral("边缘")),
        // ---- 边缘提取 ----
        // VM 模块 XML 默认 EdgeMode=1（边缘对）
        grouped(makeEnumParam(QStringLiteral("edgeMode"), 1,
                              {QStringLiteral("单边"), QStringLiteral("边对")},
                              QStringLiteral("边缘模式")),
                QStringLiteral("边缘")),
        // ---- VM「边缘对宽度」+ 极值点评分（官方 GUID-59D1E6A2「参数配置」）----
        // 官方：边缘对宽度「单独调节该参数无法直接筛选出期望的边缘对」，只是计分因子的
        // 缩放因子。旧 pairWidth 硬上限在第十三批移除（官方没有该参数）。
        // 模块 XML 默认 EdgePairWidth=10，范围 1~10000。
        grouped(makeDoubleParam(QStringLiteral("expectedPairWidth"), 10.0, 1.0, 10000.0,
                                QStringLiteral("边缘对宽度（计分缩放因子）"),
                                QStringLiteral("px")),
                QStringLiteral("评分")),
        // ---- VM 评分项 + 每项独立曲线 ----
        // 官方 XML（IMVSCaliperModuCs.xml）：每个计分项各带一组「曲线类型/起点 X0/X中点 X1/
        // 终点 XC/Y计分 y0/Y计分 y1」，默认值照 IMVSCaliperModuAlgorithm.xml。
        grouped(makeBoolParam(QStringLiteral("scoreSpacing"), false,
                              QStringLiteral("间距")),
                QStringLiteral("评分")),
        grouped(makeEnumParam(QStringLiteral("spacingCurve"), 0,
                              {QStringLiteral("递增"), QStringLiteral("递减")},
                              QStringLiteral("曲线类型")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingX0"), 0.0, -100000.0, 100000.0,
                                QStringLiteral("起点 X0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingX1"), 0.0, -100000.0, 100000.0,
                                QStringLiteral("X中点 X1")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingXC"), 5.0, -100000.0, 100000.0,
                                QStringLiteral("终点 XC")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingY0"), 0.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingY1"), 100.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y1")), QStringLiteral("评分")),

        grouped(makeBoolParam(QStringLiteral("scoreSpacingDiff"), true,
                              QStringLiteral("间距差")),
                QStringLiteral("评分")),
        grouped(makeEnumParam(QStringLiteral("spacingDiffCurve"), 1,
                              {QStringLiteral("递增"), QStringLiteral("递减")},
                              QStringLiteral("曲线类型")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffX0"), 0.0, -100000.0, 100000.0,
                                QStringLiteral("起点 X0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffX1"), 1.0, -100000.0, 100000.0,
                                QStringLiteral("X中点 X1")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffXC"), 1.0, -100000.0, 100000.0,
                                QStringLiteral("终点 XC")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffY0"), 0.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffY1"), 100.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y1")), QStringLiteral("评分")),

        // 相对间距差 = 双边计分：左曲线（负侧）+ 右曲线（*H，正侧）
        grouped(makeBoolParam(QStringLiteral("scoreRelativeSpacingDiff"), false,
                              QStringLiteral("相对间距差")),
                QStringLiteral("评分")),
        grouped(makeEnumParam(QStringLiteral("spacingDiffRelativeCurve"), 0,
                              {QStringLiteral("递增"), QStringLiteral("递减")},
                              QStringLiteral("左曲线类型")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffRelativeX0"), -1.0, -100000.0, 100000.0,
                                QStringLiteral("起点 X0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffRelativeX1"), -1.0, -100000.0, 100000.0,
                                QStringLiteral("X中点 X1")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffRelativeXC"), 0.0, -100000.0, 100000.0,
                                QStringLiteral("终点 XC")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffRelativeY0"), 0.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffRelativeY1"), 100.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y1")), QStringLiteral("评分")),
        grouped(makeEnumParam(QStringLiteral("spacingDiffRelativeCurveH"), 1,
                              {QStringLiteral("递增"), QStringLiteral("递减")},
                              QStringLiteral("右曲线类型")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffRelativeX0H"), 0.0, -100000.0, 100000.0,
                                QStringLiteral("右起点 X0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffRelativeX1H"), 2.0, -100000.0, 100000.0,
                                QStringLiteral("右X中点 X1")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffRelativeXCH"), 2.0, -100000.0, 100000.0,
                                QStringLiteral("右终点 XC")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffRelativeY0H"), 0.0, 0.0, 100.0,
                                QStringLiteral("右Y计分 y0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("spacingDiffRelativeY1H"), 100.0, 0.0, 100.0,
                                QStringLiteral("右Y计分 y1")), QStringLiteral("评分")),

        // 对比度（单边缘）与边缘对对比度共用同一组曲线（官方 XML 只有一组 Contrast*）
        grouped(makeBoolParam(QStringLiteral("scoreContrast"), true,
                              QStringLiteral("对比度")),
                QStringLiteral("评分")),
        grouped(makeBoolParam(QStringLiteral("scorePairContrast"), false,
                              QStringLiteral("边缘对对比度")),
                QStringLiteral("评分")),
        grouped(makeEnumParam(QStringLiteral("contrastCurve"), 0,
                              {QStringLiteral("递增"), QStringLiteral("递减")},
                              QStringLiteral("曲线类型")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("contrastX0"), 0.0, -100000.0, 100000.0,
                                QStringLiteral("起点 X0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("contrastX1"), 0.0, -100000.0, 100000.0,
                                QStringLiteral("X中点 X1")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("contrastXC"), 255.0, -100000.0, 100000.0,
                                QStringLiteral("终点 XC")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("contrastY0"), 0.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("contrastY1"), 100.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y1")), QStringLiteral("评分")),

        grouped(makeBoolParam(QStringLiteral("scorePosition"), false,
                              QStringLiteral("位置")),
                QStringLiteral("评分")),
        grouped(makeEnumParam(QStringLiteral("positionCurve"), 1,
                              {QStringLiteral("递增"), QStringLiteral("递减")},
                              QStringLiteral("曲线类型")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionX0"), 0.0, -100000.0, 100000.0,
                                QStringLiteral("起点 X0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionX1"), 100.0, -100000.0, 100000.0,
                                QStringLiteral("X中点 X1")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionXC"), 10000.0, -100000.0, 100000.0,
                                QStringLiteral("终点 XC")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionY0"), 0.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionY1"), 100.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y1")), QStringLiteral("评分")),

        grouped(makeBoolParam(QStringLiteral("scoreRelativePosition"), false,
                              QStringLiteral("相对位置")),
                QStringLiteral("评分")),
        grouped(makeEnumParam(QStringLiteral("positionRelativeCurve"), 1,
                              {QStringLiteral("递增"), QStringLiteral("递减")},
                              QStringLiteral("曲线类型")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionRelativeX0"), -100.0, -100000.0, 100000.0,
                                QStringLiteral("起点 X0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionRelativeX1"), 100.0, -100000.0, 100000.0,
                                QStringLiteral("X中点 X1")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionRelativeXC"), 10000.0, -100000.0, 100000.0,
                                QStringLiteral("终点 XC")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionRelativeY0"), 0.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionRelativeY1"), 100.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y1")), QStringLiteral("评分")),

        grouped(makeBoolParam(QStringLiteral("scoreNormPosition"), false,
                              QStringLiteral("归一位置")),
                QStringLiteral("评分")),
        grouped(makeEnumParam(QStringLiteral("positionNormCurve"), 1,
                              {QStringLiteral("递增"), QStringLiteral("递减")},
                              QStringLiteral("曲线类型")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionNormX0"), 0.0, -100000.0, 100000.0,
                                QStringLiteral("起点 X0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionNormX1"), 5.0, -100000.0, 100000.0,
                                QStringLiteral("X中点 X1")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionNormXC"), 5.0, -100000.0, 100000.0,
                                QStringLiteral("终点 XC")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionNormY0"), 0.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionNormY1"), 100.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y1")), QStringLiteral("评分")),

        grouped(makeBoolParam(QStringLiteral("scoreNormRelativePosition"), false,
                              QStringLiteral("归一相对位置")),
                QStringLiteral("评分")),
        grouped(makeEnumParam(QStringLiteral("positionNormRelativeCurve"), 1,
                              {QStringLiteral("递增"), QStringLiteral("递减")},
                              QStringLiteral("曲线类型")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionNormRelativeX0"), -5.0, -100000.0, 100000.0,
                                QStringLiteral("起点 X0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionNormRelativeX1"), 5.0, -100000.0, 100000.0,
                                QStringLiteral("X中点 X1")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionNormRelativeXC"), 5.0, -100000.0, 100000.0,
                                QStringLiteral("终点 XC")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionNormRelativeY0"), 0.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("positionNormRelativeY1"), 100.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y1")), QStringLiteral("评分")),

        grouped(makeBoolParam(QStringLiteral("scoreGray"), false,
                              QStringLiteral("灰度")),
                QStringLiteral("评分")),
        grouped(makeEnumParam(QStringLiteral("grayCurve"), 0,
                              {QStringLiteral("递增"), QStringLiteral("递减")},
                              QStringLiteral("曲线类型")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("grayX0"), 0.0, -100000.0, 100000.0,
                                QStringLiteral("起点 X0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("grayX1"), 0.0, -100000.0, 100000.0,
                                QStringLiteral("X中点 X1")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("grayXC"), 255.0, -100000.0, 100000.0,
                                QStringLiteral("终点 XC")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("grayY0"), 0.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y0")), QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("grayY1"), 100.0, 0.0, 100.0,
                                QStringLiteral("Y计分 y1")), QStringLiteral("评分")),
        // VM「边缘阈值」：模块 XML 默认 ContrastTH=15（官方范围 0~255）
        grouped(makeDoubleParam(QStringLiteral("threshold"), 15.0, 0.0, 255.0,
                                QStringLiteral("边缘梯度阈值")),
                QStringLiteral("边缘")),
        // VM「滤波尺寸」：XML 参数名 HalfKernelSize（半核尺寸，范围 1~50，默认 2）
        // ⇒ 实际核宽 = 2*half+1（默认 5，与帮助图 4 的「滤波核尺寸=5」一致）。
        grouped(makeIntParam(QStringLiteral("filterSize"), 2, 1, 50,
                             QStringLiteral("滤波尺寸（半核尺寸，核宽=2×值+1）")),
                QStringLiteral("边缘")),
        // VM「边缘0/1极性」：官方 UI 顺序「从黑到白 / 从白到黑 / 任意极性」。
        // 内部索引与 UI 顺序一致：0=从黑到白（上升沿）、1=从白到黑（下降沿）、2=任意极性。
        grouped(makeEnumParam(QStringLiteral("polarity"), 2,
                              {QStringLiteral("从黑到白"), QStringLiteral("从白到黑"),
                               QStringLiteral("任意极性")},
                              QStringLiteral("边缘0极性")),
                QStringLiteral("边缘")),
        // VM「边缘1极性」：仅边缘对模式出现，约束配对中的第二条边
        grouped(makeEnumParam(QStringLiteral("edge1Polarity"), 2,
                              {QStringLiteral("从黑到白"), QStringLiteral("从白到黑"),
                               QStringLiteral("任意极性")},
                              QStringLiteral("边缘1极性")),
                QStringLiteral("边缘")),
        grouped(makeEnumParam(QStringLiteral("projectionType"), 0,
                              {QStringLiteral("均值"), QStringLiteral("最大值"),
                               QStringLiteral("最小值")},
                              QStringLiteral("剖面投影")),
                QStringLiteral("边缘")),
        grouped(makeIntParam(QStringLiteral("profileWidth"), 1, 1, 101,
                             QStringLiteral("投影宽度（沿卡尺方向，1=逐像素）"),
                             QStringLiteral("px")),
                QStringLiteral("边缘")),
        grouped(makeBoolParam(QStringLiteral("subpixel"), true,
                              QStringLiteral("亚像素插值")),
                QStringLiteral("边缘")),
        // ---- 结果筛选 ----
        grouped(makeIntParam(QStringLiteral("maxCount"), 1, 1, 1000,
                             QStringLiteral("最大结果数")),
                QStringLiteral("结果")),
        // VM「排序方式」四选一。0/1 保持老语义（方向正向 / 分数降序），
        // 2=分数升序、3=方向逆向为新增。
        grouped(makeEnumParam(QStringLiteral("sortType"), 0,
                              {QStringLiteral("方向正向"), QStringLiteral("分数降序"),
                               QStringLiteral("分数升序"), QStringLiteral("方向逆向")},
                              QStringLiteral("结果排序")),
                QStringLiteral("结果")),
        grouped(makeBoolParam(QStringLiteral("showCaliperEdge"), true,
                              QStringLiteral("显示边缘点")),
                QStringLiteral("结果")),
        grouped(makeBoolParam(QStringLiteral("showCaliperPair"), true,
                              QStringLiteral("显示边对连线")),
                QStringLiteral("结果")),
        // ---- VM「结果判断」----
        // 官方 XML：数量判断 / 宽度判断 / 边缘点0X/Y、边缘点1X/Y 判断，各带 Enable+Low+High。
        // 任一启用且不满足 → 模块状态 NG，原因写 judgeNote（*Note 口径，随失败轮进结果面板）。
        grouped(makeBoolParam(QStringLiteral("numLimitEnable"), false,
                              QStringLiteral("数量判断")),
                QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("numLimitLow"), 0, 0, 99999,
                             QStringLiteral("数量范围下限")), QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("numLimitHigh"), 99999, 0, 99999,
                             QStringLiteral("数量范围上限")), QStringLiteral("结果判断")),
        // 宽度判断与边缘点1判断只在边缘对模式有意义
        grouped(makeBoolParam(QStringLiteral("edgeWidthLimitEnable"), false,
                              QStringLiteral("宽度判断")),
                QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("edgeWidthLimitLow"), 0, 0, 99999,
                             QStringLiteral("宽度范围下限")), QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("edgeWidthLimitHigh"), 99999, 0, 99999,
                             QStringLiteral("宽度范围上限")), QStringLiteral("结果判断")),
        grouped(makeBoolParam(QStringLiteral("edge0PointXLimitEnable"), false,
                              QStringLiteral("边缘点0X判断")),
                QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("edge0PointXLimitLow"), -99999, -99999, 99999,
                             QStringLiteral("边缘点0X下限")), QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("edge0PointXLimitHigh"), 99999, -99999, 99999,
                             QStringLiteral("边缘点0X上限")), QStringLiteral("结果判断")),
        grouped(makeBoolParam(QStringLiteral("edge0PointYLimitEnable"), false,
                              QStringLiteral("边缘点0Y判断")),
                QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("edge0PointYLimitLow"), -99999, -99999, 99999,
                             QStringLiteral("边缘点0Y下限")), QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("edge0PointYLimitHigh"), 99999, -99999, 99999,
                             QStringLiteral("边缘点0Y上限")), QStringLiteral("结果判断")),
        grouped(makeBoolParam(QStringLiteral("edge1PointXLimitEnable"), false,
                              QStringLiteral("边缘点1X判断")),
                QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("edge1PointXLimitLow"), -99999, -99999, 99999,
                             QStringLiteral("边缘点1X下限")), QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("edge1PointXLimitHigh"), 99999, -99999, 99999,
                             QStringLiteral("边缘点1X上限")), QStringLiteral("结果判断")),
        grouped(makeBoolParam(QStringLiteral("edge1PointYLimitEnable"), false,
                              QStringLiteral("边缘点1Y判断")),
                QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("edge1PointYLimitLow"), -99999, -99999, 99999,
                             QStringLiteral("边缘点1Y下限")), QStringLiteral("结果判断")),
        grouped(makeIntParam(QStringLiteral("edge1PointYLimitHigh"), 99999, -99999, 99999,
                             QStringLiteral("边缘点1Y上限")), QStringLiteral("结果判断")),
    });

    setParamDirect(QStringLiteral("edgeCount"), 0);
    setParamDirect(QStringLiteral("rawEdgeCount"), 0);
    setParamDirect(QStringLiteral("pairCount"), 0);
    setParamDirect(QStringLiteral("avgWidth"), 0.0);
    setParamDirect(QStringLiteral("minWidth"), 0.0);
    setParamDirect(QStringLiteral("maxWidth"), 0.0);
    setParamDirect(QStringLiteral("avgContrast"), 0.0);
    setParamDirect(QStringLiteral("maxContrast"), 0.0);
    setParamDirect(QStringLiteral("edgeScores"), QString());
    setParamDirect(QStringLiteral("avgScore"), 0.0);
    setParamDirect(QStringLiteral("elapsedMs"), 0.0);
    setParamDirect(QStringLiteral("edgePolarities"), QString());
    setParamDirect(QStringLiteral("edgePositions"), QString());
    setParamDirect(QStringLiteral("edgeGrays"), QString());
    // 「当前结果」展示用（结果字段，不上面板）：逐结果的边缘点中心与边对直线
    setParamDirect(QStringLiteral("edgePoints"), QString());
    setParamDirect(QStringLiteral("pairLines"), QString());
    // VM「模块结果 → 检测区域」：ROI 中心/宽/高/角度
    setParamDirect(QStringLiteral("roiCenterX"), 0.0);
    setParamDirect(QStringLiteral("roiCenterY"), 0.0);
    setParamDirect(QStringLiteral("roiWidth"), 0.0);
    setParamDirect(QStringLiteral("roiHeight"), 0.0);
    setParamDirect(QStringLiteral("roiAngle"), 0.0);
}

void OpencvCaliperNode::run(bool /*autoSwitch*/)
{
    try {
        QElapsedTimer runTimer;          // 官方模块结果「耗时（ms）」
        runTimer.start();
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat gray = OpencvUtil::himageToMat(input);
        if (gray.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        // 非 8 位输入的显式拒绝（2026-10-25 复核 P2）：桥接修好后 16 位图会如实返回 CV_16U，
        // 而这里的阈值 0-255 / at<uchar> / 外部 8 位入口都按 8 位写死——静默错值比判红更糟。
        {
            QString depthWhy;
            if (!OpencvUtil::requireGray8(gray, &depthWhy))
                throw std::runtime_error(depthWhy.toStdString());
        }
        if (gray.channels() == 3) {
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
        }

        const double r1 = m_params.value(QStringLiteral("row1"), 100.0).toDouble();
        const double c1 = m_params.value(QStringLiteral("col1"), 100.0).toDouble();
        const double r2 = m_params.value(QStringLiteral("row2"), 200.0).toDouble();
        const double c2 = m_params.value(QStringLiteral("col2"), 100.0).toDouble();
        const int half = m_params.value(QStringLiteral("halfWidth"), 10).toInt();
        const double step = m_params.value(QStringLiteral("step"), 5.0).toDouble();
        const double thr = m_params.value(QStringLiteral("threshold"), 10.0).toDouble();
        // VM「模糊边缘」：官方说明是「增强候选边缘点集的提取能力，获取更多候选点集数」，
        // 未给具体算法。这里把极值门槛减半以纳入更弱的候选（默认关 = 老行为不变）。
        const bool blurEdge = m_params.value(QStringLiteral("blurEdge"), false).toBool();
        const double effThr = blurEdge ? (thr * 0.5) : thr;
        // VM 极性：0=从黑到白（上升沿）、1=从白到黑（下降沿）、2=任意极性（默认=老行为）
        const int polarity = m_params.value(QStringLiteral("polarity"), 2).toInt();
        const int edge1Polarity = m_params.value(QStringLiteral("edge1Polarity"), 2).toInt();
        // VM「滤波尺寸」= 半核尺寸（XML HalfKernelSize，1~50，默认 2）：核宽 = 2*half+1。
        const int filterHalf = qBound(1, m_params.value(QStringLiteral("filterSize"), 2).toInt(), 50);
        const int edgeMode = m_params.value(QStringLiteral("edgeMode"), 0).toInt();
        const int projectionType = m_params.value(QStringLiteral("projectionType"), 0).toInt();
        const int profileWidth = m_params.value(QStringLiteral("profileWidth"), 1).toInt();
        const bool useSubpixel = m_params.value(QStringLiteral("subpixel"), true).toBool();
        const int maxCount = m_params.value(QStringLiteral("maxCount"), 1).toInt();
        const int sortType = m_params.value(QStringLiteral("sortType"), 0).toInt();
        // VM 极值点评分（默认 = 模块 XML：单边缘对比度开、边缘对对比度关、间距差开）
        const double expectedW =
            m_params.value(QStringLiteral("expectedPairWidth"), 10.0).toDouble();
        const bool scoreSpacing = m_params.value(QStringLiteral("scoreSpacing"), false).toBool();
        const bool scoreSpacingDiff =
            m_params.value(QStringLiteral("scoreSpacingDiff"), true).toBool();
        const bool scoreRelativeSpacingDiff =
            m_params.value(QStringLiteral("scoreRelativeSpacingDiff"), false).toBool();
        const bool scoreContrast =
            m_params.value(QStringLiteral("scoreContrast"), true).toBool();
        const bool scorePairContrast =
            m_params.value(QStringLiteral("scorePairContrast"), false).toBool();
        const bool scorePosition =
            m_params.value(QStringLiteral("scorePosition"), false).toBool();
        const bool scoreRelativePosition =
            m_params.value(QStringLiteral("scoreRelativePosition"), false).toBool();
        const bool scoreNormPosition =
            m_params.value(QStringLiteral("scoreNormPosition"), false).toBool();
        const bool scoreNormRelativePosition =
            m_params.value(QStringLiteral("scoreNormRelativePosition"), false).toBool();
        const bool scoreGray =
            m_params.value(QStringLiteral("scoreGray"), false).toBool();
        const bool anyScore = scoreSpacing || scoreSpacingDiff
                              || scoreRelativeSpacingDiff || scoreContrast
                              || scorePairContrast
                              || scorePosition || scoreRelativePosition
                              || scoreNormPosition || scoreNormRelativePosition
                              || scoreGray;
        // 每个计分项各带一组独立曲线（官方 XML：*Drop/*X0/*X1/*XC/*Y0/*Y1）。
        // 双边项（相对间距差）右半曲线用 *H 后缀。
        struct CurveParams {
            int type = 1;
            double x0 = 0.0;
            double x1 = 1.0;
            double xc = 1.0;
            double y0 = 0.0;
            double y1 = 100.0;
        };
        auto readCurve = [this](const QString &prefix, const QString &suffix,
                                int defType, double dx0, double dx1, double dxc,
                                double dy0, double dy1) -> CurveParams {
            CurveParams c;
            c.type = m_params.value(prefix + QStringLiteral("Curve") + suffix, defType).toInt();
            c.x0 = m_params.value(prefix + QStringLiteral("X0") + suffix, dx0).toDouble();
            c.x1 = m_params.value(prefix + QStringLiteral("X1") + suffix, dx1).toDouble();
            c.xc = m_params.value(prefix + QStringLiteral("XC") + suffix, dxc).toDouble();
            c.y0 = m_params.value(prefix + QStringLiteral("Y0") + suffix, dy0).toDouble();
            c.y1 = m_params.value(prefix + QStringLiteral("Y1") + suffix, dy1).toDouble();
            return c;
        };
        const CurveParams cSpacing = readCurve(QStringLiteral("spacing"), QString(),
                                               0, 0, 0, 5, 0, 100);
        const CurveParams cSpacingDiff = readCurve(QStringLiteral("spacingDiff"), QString(),
                                                   1, 0, 1, 1, 0, 100);
        const CurveParams cAsymLeft = readCurve(QStringLiteral("spacingDiffRelative"), QString(),
                                                0, -1, -1, 0, 0, 100);
        const CurveParams cAsymRight = readCurve(QStringLiteral("spacingDiffRelative"),
                                                 QStringLiteral("H"), 1, 0, 2, 2, 0, 100);
        const CurveParams cContrast = readCurve(QStringLiteral("contrast"), QString(),
                                                0, 0, 0, 255, 0, 100);
        const CurveParams cPosition = readCurve(QStringLiteral("position"), QString(),
                                                1, 0, 100, 10000, 0, 100);
        const CurveParams cPositionRel = readCurve(QStringLiteral("positionRelative"), QString(),
                                                   1, -100, 100, 10000, 0, 100);
        const CurveParams cPositionNorm = readCurve(QStringLiteral("positionNorm"), QString(),
                                                    1, 0, 5, 5, 0, 100);
        const CurveParams cPositionNormRel =
            readCurve(QStringLiteral("positionNormRelative"), QString(), 1, -5, 5, 5, 0, 100);
        const CurveParams cGray = readCurve(QStringLiteral("gray"), QString(), 0, 0, 0, 255, 0, 100);

        const double dx = c2 - c1;
        const double dy = r2 - r1;
        const double L = std::hypot(dx, dy);
        if (L < 1.0) {
            // 显式失败分支也要有可读原因（2026-10-26 复核）：原来只判红，面板上看不到为什么
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("卡尺两点重合（长度为 0），无法定义搜索方向");
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            return;
        }
        const double ux = dx / L;   // 直线方向单位向量（卡尺宽度方向）
        const double uy = dy / L;
        const double nx = -uy;      // 法线方向单位向量（边缘搜索方向）
        const double ny = ux;

        const int K = std::max(1, static_cast<int>(L / step));
        const int M = half * 2 + 1; // 法线方向采样点数
        const int projHalf = std::max(0, (profileWidth - 1) / 2);   // 总采样数 = 2*projHalf+1 ≤ profileWidth

        const int projectionDirection =
            m_params.value(QStringLiteral("projectionDirection"), 0).toInt();
        const bool longitudinal = (projectionDirection == 1);
        // 横向投影（老行为）：一维信号沿法线 n，K+1 个剖面沿卡尺线 u 排列；
        // 纵向投影：          一维信号沿卡尺线 u，M 个剖面沿法线 n 排列。
        // 两种方向覆盖同一个矩形 ROI（卡尺线 × ±half），只是交换「信号轴」与「剖面轴」。
        const int nSig = longitudinal ? (K + 1) : M;
        const int nProf = longitudinal ? M : (K + 1);
        const double sigHalf = longitudinal ? (K * 0.5) : half;
        const double sigSpacing = longitudinal ? (L / std::max(1, K)) : 1.0;
        const double sx = longitudinal ? ux : nx;   // 一维信号方向
        const double sy = longitudinal ? uy : ny;
        const double qx = longitudinal ? nx : ux;   // 投影聚合方向（= 剖面排列方向）
        const double qy = longitudinal ? ny : uy;
        const double midCol = (c1 + c2) * 0.5;      // 投影区域中心
        const double midRow = (r1 + r2) * 0.5;

        // 双线性采样。VM：投影区域超出图像边界时「填充其他灰度值」；
        // 旧实现越界返回 0，在亮背景上会凭空造出一条 亮→黑 的伪边缘，改用边界复制。
        auto sampleGray = [&gray](double px, double py) -> double {
            const double cx = std::min(std::max(px, 0.0), double(gray.cols - 1));
            const double cy = std::min(std::max(py, 0.0), double(gray.rows - 1));
            const int x0 = static_cast<int>(cx);
            const int y0 = static_cast<int>(cy);
            const int x1 = std::min(x0 + 1, gray.cols - 1);
            const int y1 = std::min(y0 + 1, gray.rows - 1);
            const double fx = cx - x0;
            const double fy = cy - y0;
            return gray.at<uchar>(y0, x0) * (1 - fx) * (1 - fy)
                   + gray.at<uchar>(y0, x1) * fx * (1 - fy)
                   + gray.at<uchar>(y1, x0) * (1 - fx) * fy
                   + gray.at<uchar>(y1, x1) * fx * fy;
        };

        std::vector<EdgeHit> hits;    // 单边模式：保留的边缘
        std::vector<PairHit> pairs;   // 边对模式：保留的边对
        std::vector<std::vector<EdgeHit>> pairProfiles;   // 边对模式：逐剖面候选边缘（配对延后到评分就绪）
        int rawEdgeCount = 0;         // 极性过滤后的候选边缘总数（含未输出的）

        for (int pIdx = 0; pIdx < nProf; ++pIdx) {
            double baseX = 0.0, baseY = 0.0;
            double profOffset = 0.0;   // 本剖面相对投影区域中心沿法线的位移
            if (longitudinal) {
                profOffset = pIdx - half;
                baseX = midCol + nx * profOffset;
                baseY = midRow + ny * profOffset;
            } else {
                const double t = (K == 0) ? 0.0 : static_cast<double>(pIdx) / K;
                baseX = c1 + dx * t;      // 与老实现逐位一致
                baseY = r1 + dy * t;
            }

            // 一维投影剖面：沿信号轴 sig 取 nSig 个点，沿聚合轴 q 按 projectionType
            // 聚合 profileWidth 个采样点（横向投影时 q=卡尺线方向，与老实现一致）。
            std::vector<float> profile(nSig);
            for (int m = 0; m < nSig; ++m) {
                const double s = (m - sigHalf) * sigSpacing;
                const double px = baseX + sx * s;
                const double py = baseY + sy * s;
                if (projHalf == 0) {
                    profile[m] = static_cast<float>(sampleGray(px, py));
                    continue;
                }
                double sum = 0.0;
                double maxV = -1e9;
                double minV = 1e9;
                int samples = 0;
                for (int j = -projHalf; j <= projHalf; ++j) {
                    const double v = sampleGray(px + qx * j, py + qy * j);
                    sum += v;
                    maxV = std::max(maxV, v);
                    minV = std::min(minV, v);
                    ++samples;
                }
                if (projectionType == 1) {
                    profile[m] = static_cast<float>(maxV);
                } else if (projectionType == 2) {
                    profile[m] = static_cast<float>(minV);
                } else {
                    profile[m] = static_cast<float>(sum / samples);
                }
            }

            // VM 步骤3「一维差分滤波」：以 m+0.5 为中心，右半核均值 − 左半核均值。
            // 注意必须是**两侧各自求均值再相减**；若写成「先盒式平滑再差分」，
            // 代数上会约掉中间项、只剩两个端点，噪声完全没被平均掉（实测 k=5 反而
            // 比 k=1 报出更多伪边缘）。半核尺寸=1 时每侧 1 点，逐位等于老的一阶差分；
            // 半核尺寸=h 时每侧 h 点、核宽 2h+1（VM 默认 h=2 ⇒ 核宽 5）。
            const int side = filterHalf;
            auto clampIdx = [&](int i) { return std::min(nSig - 1, std::max(0, i)); };
            std::vector<float> grad(std::max(0, nSig - 1));
            for (int m = 0; m < nSig - 1; ++m) {
                double right = 0.0, left = 0.0;
                for (int j = 0; j < side; ++j) {
                    right += profile[clampIdx(m + 1 + j)];
                    left += profile[clampIdx(m - j)];
                }
                grad[m] = static_cast<float>((right - left) / side);
            }

            // 收集本剖面所有满足阈值的跨零极值点（一个窗口可输出多个边缘，对应 measure_select='all'）
            std::vector<EdgeHit> profileEdges;
            for (int m = 1; m < nSig - 2; ++m) {
                const float gp = grad[m - 1];
                const float gc = grad[m];
                const float gn = grad[m + 1];
                const double a0 = std::fabs(static_cast<double>(gp));
                const double a1 = std::fabs(static_cast<double>(gc));
                const double a2 = std::fabs(static_cast<double>(gn));
                // 局部极值（非极大值抑制）：幅值不小于左侧、严格大于右侧 —— 平顶只在末端记一次。
                // 旧逻辑要求「梯度符号跨零」，只对一阶差分下阶跃两侧恰为 0 成立；
                // 滤波核放大后梯度峰两侧同号，真实边缘会被全部漏检（实测 k=5 → 0 条）。
                if (!(a1 >= a0 && a1 > a2))
                    continue;
                if (a1 < effThr)
                    continue;

                const bool rising = gc > 0.0f;   // 沿法线方向变亮

                // 极性过滤：单边模式逐条过滤；边对模式留给配对时判首边（否则先删掉一条就配不成对）
                if (edgeMode == 0) {
                    if (polarity == 0 && !rising) continue;   // 从黑到白 = 上升沿
                    if (polarity == 1 && rising) continue;    // 从白到黑 = 下降沿
                }

                // 亚像素：抛物线插值（用 m-1, m, m+1 的梯度幅值）。
                // grad[m] 是 profile[m+1]−profile[m]（或左右均值差），其物理位置在
                // m 与 m+1 之间，故基准取 m+0.5；旧实现取 m 会让边缘位置系统性
                // 偏半个采样间隔（实测 step=5 的纵向投影把 y=100 的边缘报成 95）。
                double sub = static_cast<double>(m) + 0.5;
                if (useSubpixel) {
                    const double denom = a0 - 2 * a1 + a2;
                    if (std::fabs(denom) > 1e-9)
                        sub += 0.5 * (a0 - a2) / denom;
                }

                const double offDist = (sub - sigHalf) * sigSpacing;
                EdgeHit hit;
                hit.col = baseX + sx * offDist;
                hit.row = baseY + sy * offDist;
                hit.contrast = a1;
                // 位置评分输入恒为「相对投影区域中心沿法线的位移」：
                // 横向投影时信号就在法线上（offDist）；纵向投影时来自剖面位置（profOffset）。
                hit.offset = longitudinal ? profOffset : offDist;
                hit.polarity = rising ? 1 : 2;   // 官方结果：1=从黑到白，2=从白到黑
                // 边缘处灰度：在亚像素位置对投影剖面做线性插值（灰度评分输入）
                {
                    const double base = std::floor(sub);
                    const int i0 = std::min(nSig - 1, std::max(0, static_cast<int>(base)));
                    const int i1 = std::min(nSig - 1, i0 + 1);
                    const double f = sub - base;
                    hit.gray = profile[i0] * (1.0 - f) + profile[i1] * f;
                }
                hit.rising = rising;
                hit.profile = pIdx;
                profileEdges.push_back(hit);
            }
            rawEdgeCount += static_cast<int>(profileEdges.size());

            if (edgeMode == 1) {
                // 配对留到「极值点评分」之后统一做：官方按得分输出边缘对，
                // 需要先用评分函数给候选对打分（旧实现按原始序列相邻配对，噪声边会拆散真实边对）。
                pairProfiles.push_back(std::move(profileEdges));
            } else {
                hits.insert(hits.end(), profileEdges.begin(), profileEdges.end());
            }
        }

        // ---- VM 步骤6「极值点评分」----
        // 官方帮助图 6（递增）/ 图 7（递减）给出确切形状（分段直线，不是平滑 S 曲线）：
        //   递增：x < x0 → y0；x0~x1 → 平于 y0；x1~xc → 线性升到 y1；x > xc → 平于 y1
        //   递减：x < x0 → y1；x0~x1 → 线性由 y1 降到 y0；x > x1 → 平于 y0
        // 横轴命名以模块 XML 为准：X0=起点、X1=X中点、XC=终点；纵轴 Y计分范围 0~100。
        // 官方「总分 = 各计分项相乘后开 N 次方」（帮助页公式图）：
        //   OverallScore = (Score1 × Score2 × … × ScoreN)^(1/N)
        auto oneSided = [](double x, const CurveParams &c) -> double {
            if (c.type == 0) {   // 递增（图 6）：X0~X1 平于 y0，X1~XC 升到 y1
                if (x <= c.x1)
                    return c.y0;
                if (c.xc <= c.x1 || x >= c.xc)
                    return c.y1;   // XC<=X1 时退化为台阶
                return c.y0 + (c.y1 - c.y0) * (x - c.x1) / (c.xc - c.x1);
            }
            // 递减（图 7）：X0~X1 由 y1 降到 y0，之后平于 y0
            if (x <= c.x0)
                return c.y1;
            if (c.x1 <= c.x0 || x >= c.x1)
                return c.y0;   // X1<=X0 时退化为台阶
            return c.y1 + (c.y0 - c.y1) * (x - c.x0) / (c.x1 - c.x0);
        };
        // 官方公式：总分 = 各计分项相乘后开 N 次方（帮助页公式图），再除以 100 归一化到 0~1。
        // 各计分项的横轴输入用物理量（官方 XML 默认值就是按物理量给的）：
        //   对比度/灰度 0~255；位置为像素；归一位置为「像素 / 边缘对宽度」；间距为「距离 / 边缘对宽度」。
        auto computeScore = [&](double contrast, double width,
                                double offset, double gray, bool asPair) -> double {
            if (!anyScore)
                return qBound(0.0, contrast / 255.0, 1.0);   // 全关时分数 = 对比度归一化
            double product = 1.0;
            int count = 0;
            auto add = [&](double score) { product *= score; ++count; };
            const bool usable = (width > 0.0 && expectedW > 0.0);
            if (scoreSpacing && usable)                        // 官方：边对距离/边缘对宽度
                add(oneSided(width / expectedW, cSpacing));
            if (scoreSpacingDiff && usable)                    // 官方：|(距离−宽度)/宽度|
                add(oneSided(std::fabs((width - expectedW) / expectedW), cSpacingDiff));
            if (scoreRelativeSpacingDiff && usable) {          // 官方：带符号，双边
                const double dev = (width - expectedW) / expectedW;
                // 负侧走左曲线、正侧走右曲线（图 8/图 9）
                add(oneSided(dev, (dev < 0.0) ? cAsymLeft : cAsymRight));
            }
            // 官方 XML 把单边缘对比度与边缘对对比度拆成两个开关，按模式只取其一
            if (asPair ? scorePairContrast : scoreContrast)
                add(oneSided(contrast, cContrast));
            if (scorePosition)                                 // 绝对位置差（像素）
                add(oneSided(std::fabs(offset), cPosition));
            if (scoreRelativePosition)                         // 位置差（带符号，像素）
                add(oneSided(offset, cPositionRel));
            if (scoreNormPosition && expectedW > 0.0)          // 绝对归一化距离
                add(oneSided(std::fabs(offset) / expectedW, cPositionNorm));
            if (scoreNormRelativePosition && expectedW > 0.0)  // 归一化位置差（带符号）
                add(oneSided(offset / expectedW, cPositionNormRel));
            if (scoreGray)                                     // 官方：边缘灰度（边对取均值）
                add(oneSided(gray, cGray));
            if (count == 0)
                return qBound(0.0, contrast / 255.0, 1.0);
            // N 次方根：乘积可能很大（每项 ≤100），用 pow 而非连乘开方
            const double overall = std::pow(std::max(0.0, product),
                                            1.0 / static_cast<double>(count));
            return qBound(0.0, overall / 100.0, 1.0);   // 0~100 → 0~1
        };
        for (EdgeHit &e : hits)
            e.score = computeScore(e.contrast, 0.0, e.offset, e.gray, false);

        // ---- VM 步骤5+6「边缘对配对 + 按得分选优」----
        // 官方只说明最终输出「符合得分要求的亚像素极值点/边缘对」，未给配对算法。
        // 工程口径（替代旧「原始序列相邻两条边」的强制配对）：
        //   · 同一剖面内，任意两条极性相反的边缘都是一个候选对
        //     （同极性噪声边夹在中间不影响真实边对成为候选；官方没有「边对宽度」筛选）
        //   · 候选对先用同一套评分函数打分，再按分数降序贪心接受；
        //   · 每条边最多属于一个边对（非重叠），冲突时分数高的优先。
        if (edgeMode == 1) {
            struct CandidatePair {
                PairHit pair;
                size_t profile = 0;
                size_t first = 0;
                size_t second = 0;
            };
            std::vector<CandidatePair> candidates;
            // 防御：没有官方宽度上限后，高噪声图时候选对会按平方增长；单剖面封顶，
            // 避免内存/排序时间失控（正常参数下远达不到该上限）。
            constexpr size_t kMaxCandidatesPerProfile = 4096;
            for (size_t pi = 0; pi < pairProfiles.size(); ++pi) {
                const std::vector<EdgeHit> &edges = pairProfiles[pi];
                size_t profileCandidates = 0;
                for (size_t i = 0; i < edges.size(); ++i) {
                    for (size_t j = i + 1; j < edges.size(); ++j) {
                        if (edges.at(j).rising == edges.at(i).rising)
                            continue;   // 同极性不配对
                        const EdgeHit &e1 = edges.at(i);
                        const EdgeHit &e2 = edges.at(j);
                        const double gap = std::hypot(e2.col - e1.col, e2.row - e1.row);
                        if (gap <= 0.0)
                            continue;
                        if (polarity == 0 && !e1.rising) continue;    // 边缘0极性：从黑到白
                        if (polarity == 1 && e1.rising) continue;     // 边缘0极性：从白到黑
                        // VM「边缘1极性」：只在边缘对模式出现，约束配对中的第二条边
                        if (edge1Polarity == 0 && !e2.rising) continue;   // 从黑到白
                        if (edge1Polarity == 1 && e2.rising) continue;    // 从白到黑

                        CandidatePair cand;
                        cand.pair.col1 = e1.col;
                        cand.pair.row1 = e1.row;
                        cand.pair.col2 = e2.col;
                        cand.pair.row2 = e2.row;
                        cand.pair.width = gap;
                        // 官方「如果是边缘对查找模式，则使用两个边缘的平均对比度作为评分因子」
                        cand.pair.contrast = (e1.contrast + e2.contrast) * 0.5;
                        cand.pair.offset = (e1.offset + e2.offset) * 0.5;
                        cand.pair.gray = (e1.gray + e2.gray) * 0.5;
                        cand.pair.polarity = e1.rising ? 1 : 2;
                        cand.pair.profile = e1.profile;
                        cand.pair.score = computeScore(cand.pair.contrast, cand.pair.width,
                                                       cand.pair.offset, cand.pair.gray, true);
                        cand.profile = pi;
                        cand.first = i;
                        cand.second = j;
                        candidates.push_back(cand);
                        ++profileCandidates;
                        if (profileCandidates >= kMaxCandidatesPerProfile)
                            break;   // 单剖面候选对封顶
                    }
                    if (profileCandidates >= kMaxCandidatesPerProfile)
                        break;
                }
            }
            // 分数降序；同分保持生成顺序（剖面顺序 / 沿法线顺序）
            std::stable_sort(candidates.begin(), candidates.end(),
                             [](const CandidatePair &a, const CandidatePair &b) {
                                 return a.pair.score > b.pair.score;
                             });
            std::vector<std::vector<bool>> used;
            used.reserve(pairProfiles.size());
            for (const auto &edges : pairProfiles)
                used.emplace_back(edges.size(), false);
            for (const CandidatePair &cand : candidates) {
                if (used[cand.profile][cand.first] || used[cand.profile][cand.second])
                    continue;   // 一条边最多属于一个边对
                used[cand.profile][cand.first] = true;
                used[cand.profile][cand.second] = true;
                pairs.push_back(cand.pair);
            }
            // 恢复采集顺序：方向正向、未启用计分时的截断都按它（分数排序留给 sortType）
            std::stable_sort(pairs.begin(), pairs.end(),
                             [](const PairHit &a, const PairHit &b) {
                                 if (a.profile != b.profile)
                                     return a.profile < b.profile;
                                 return a.offset < b.offset;
                             });
        }

        // ① 官方「最大结果数」：启用计分时按分数由高到低保留 N 个（官方原文）；
        //    未启用计分时按采集顺序截断（老行为）。
        if (maxCount > 0) {
            if (anyScore) {
                if (edgeMode == 1) {
                    std::stable_sort(pairs.begin(), pairs.end(),
                                     [](const PairHit &a, const PairHit &b) {
                                         return a.score > b.score;
                                     });
                    if (static_cast<int>(pairs.size()) > maxCount)
                        pairs.resize(static_cast<size_t>(maxCount));
                } else {
                    std::stable_sort(hits.begin(), hits.end(),
                                     [](const EdgeHit &a, const EdgeHit &b) {
                                         return a.score > b.score;
                                     });
                    if (static_cast<int>(hits.size()) > maxCount)
                        hits.resize(static_cast<size_t>(maxCount));
                }
            } else {
                if (edgeMode == 1 && static_cast<int>(pairs.size()) > maxCount)
                    pairs.resize(static_cast<size_t>(maxCount));
                if (edgeMode == 0 && static_cast<int>(hits.size()) > maxCount)
                    hits.resize(static_cast<size_t>(maxCount));
            }
        }

        // ② VM「排序方式」：决定最终输出顺序（0=方向正向、1=分数降序、2=分数升序、3=方向逆向）。
        //    边缘对的上一步是「按分数贪心选优」，所以方向正向必须显式恢复采集顺序；
        //    旧实现漏了这一步，启用 maxCount+计分时输出会残留分数顺序。
        if (edgeMode == 1) {
            auto byAcquisition = [](const PairHit &a, const PairHit &b) {
                if (a.profile != b.profile)
                    return a.profile < b.profile;
                return a.offset < b.offset;
            };
            if (sortType == 1 || sortType == 2) {
                const bool desc = (sortType == 1);
                std::stable_sort(pairs.begin(), pairs.end(),
                                 [desc](const PairHit &a, const PairHit &b) {
                                     return desc ? a.score > b.score : a.score < b.score;
                                 });
            } else {
                std::stable_sort(pairs.begin(), pairs.end(), byAcquisition);
                if (sortType == 3)
                    std::reverse(pairs.begin(), pairs.end());
            }
        } else {
            if (sortType == 1 || sortType == 2) {
                const bool desc = (sortType == 1);
                std::stable_sort(hits.begin(), hits.end(),
                                 [desc](const EdgeHit &a, const EdgeHit &b) {
                                     return desc ? a.score > b.score : a.score < b.score;
                                 });
            } else if (sortType == 3) {
                std::reverse(hits.begin(), hits.end());
            }
        }

        // ---- 结果字段 ----
        const int resultCount = edgeMode == 1 ? static_cast<int>(pairs.size())
                                              : static_cast<int>(hits.size());
        double totalContrast = 0.0;
        double maxContrast = 0.0;
        for (const EdgeHit &e : hits) {
            totalContrast += e.contrast;
            maxContrast = std::max(maxContrast, e.contrast);
        }
        for (const PairHit &p : pairs) {
            totalContrast += p.contrast;
            maxContrast = std::max(maxContrast, p.contrast);
        }
        const double avgContrast = resultCount > 0 ? totalContrast / resultCount : 0.0;

        double minWidth = 0.0;
        double maxWidth = 0.0;
        double totalWidth = 0.0;
        for (const PairHit &p : pairs) {
            totalWidth += p.width;
            if (minWidth <= 0.0 || p.width < minWidth) minWidth = p.width;
            if (maxWidth <= 0.0 || p.width > maxWidth) maxWidth = p.width;
        }
        const double avgWidth = !pairs.empty()
                                    ? totalWidth / static_cast<double>(pairs.size())
                                    : 0.0;

        m_params[QStringLiteral("edgeCount")] = resultCount;
        m_params[QStringLiteral("rawEdgeCount")] = rawEdgeCount;
        m_params[QStringLiteral("pairCount")] = static_cast<int>(pairs.size());
        m_params[QStringLiteral("avgWidth")] = avgWidth;
        m_params[QStringLiteral("minWidth")] = minWidth;
        m_params[QStringLiteral("maxWidth")] = maxWidth;
        m_params[QStringLiteral("avgContrast")] = avgContrast;
        m_params[QStringLiteral("maxContrast")] = maxContrast;
        // 官方「模块结果」逐结果字段：分数 / 边缘*极性 / 边缘*位置 / 灰度。
        // 顺序与输出点一致（端口 1 的 extraValues）。
        QStringList scoreTexts, polarityTexts, positionTexts, grayTexts;
        double totalScore = 0.0;
        for (const EdgeHit &e : hits) {
            scoreTexts << QString::number(e.score, 'f', 4);
            polarityTexts << QString::number(e.polarity);
            positionTexts << QString::number(e.offset, 'f', 2);
            grayTexts << QString::number(e.gray, 'f', 1);
            totalScore += e.score;
        }
        for (const PairHit &p : pairs) {
            scoreTexts << QString::number(p.score, 'f', 4);
            polarityTexts << QString::number(p.polarity);
            positionTexts << QString::number(p.offset, 'f', 2);
            grayTexts << QString::number(p.gray, 'f', 1);
            totalScore += p.score;
        }
        m_params[QStringLiteral("elapsedMs")] = static_cast<double>(runTimer.elapsed());
        m_params[QStringLiteral("edgeScores")] = scoreTexts.join(QLatin1Char(','));
        m_params[QStringLiteral("edgePolarities")] = polarityTexts.join(QLatin1Char(','));
        m_params[QStringLiteral("edgePositions")] = positionTexts.join(QLatin1Char(','));
        m_params[QStringLiteral("edgeGrays")] = grayTexts.join(QLatin1Char(','));
        m_params[QStringLiteral("avgScore")] =
            resultCount > 0 ? totalScore / resultCount : 0.0;
        // ---- VM「结果判断」----
        // 官方 XML：数量判断 / 宽度判断 / 边缘点0X/Y、边缘点1X/Y 判断（各 Enable+Low+High）。
        // 任一项不满足 → moduleStatus=false，原因写 judgeNote（*Note 属判红原因口径）。
        bool judgedOk = true;
        QString judgeReason;
        auto boolParam = [this](const QString &k) {
            return m_params.value(k, false).toBool();
        };
        auto intParam = [this](const QString &k, int def) {
            return m_params.value(k, def).toInt();
        };
        if (boolParam(QStringLiteral("numLimitEnable"))) {
            const int lo = intParam(QStringLiteral("numLimitLow"), 0);
            const int hi = intParam(QStringLiteral("numLimitHigh"), 99999);
            if (resultCount < lo || resultCount > hi) {
                judgedOk = false;
                judgeReason = QStringLiteral("数量判断失败：结果数 %1 不在 [%2, %3]")
                                  .arg(resultCount).arg(lo).arg(hi);
            }
        }
        if (judgedOk && edgeMode == 1 && boolParam(QStringLiteral("edgeWidthLimitEnable"))) {
            const int lo = intParam(QStringLiteral("edgeWidthLimitLow"), 0);
            const int hi = intParam(QStringLiteral("edgeWidthLimitHigh"), 99999);
            for (int i = 0; i < static_cast<int>(pairs.size()); ++i) {
                const double w = pairs[static_cast<size_t>(i)].width;
                if (w < lo || w > hi) {
                    judgedOk = false;
                    judgeReason = QStringLiteral("宽度判断失败：边对%1 宽度 %2 不在 [%3, %4]")
                                      .arg(i).arg(w, 0, 'f', 2).arg(lo).arg(hi);
                    break;
                }
            }
        }
        // 边缘点判断取第一个结果：边对模式是边的两端，单边缘模式是第一个边缘点
        const bool hasResult = (resultCount > 0);
        const double e0x = hasResult ? (edgeMode == 1 ? pairs.front().col1 : hits.front().col)
                                     : 0.0;
        const double e0y = hasResult ? (edgeMode == 1 ? pairs.front().row1 : hits.front().row)
                                     : 0.0;
        const double e1x = (hasResult && edgeMode == 1) ? pairs.front().col2 : 0.0;
        const double e1y = (hasResult && edgeMode == 1) ? pairs.front().row2 : 0.0;
        auto pointJudge = [&](bool on, const QString &label, bool available, double v,
                              const QString &lowKey, const QString &highKey) {
            if (!on || !judgedOk)
                return;
            if (!available) {
                judgedOk = false;
                judgeReason = label + QStringLiteral("失败：无结果可判断");
                return;
            }
            const int lo = intParam(lowKey, -99999);
            const int hi = intParam(highKey, 99999);
            if (v < lo || v > hi) {
                judgedOk = false;
                judgeReason = QStringLiteral("%1失败：%2 不在 [%3, %4]")
                                  .arg(label).arg(v, 0, 'f', 2).arg(lo).arg(hi);
            }
        };
        pointJudge(boolParam(QStringLiteral("edge0PointXLimitEnable")),
                   QStringLiteral("边缘点0X判断"), hasResult, e0x,
                   QStringLiteral("edge0PointXLimitLow"), QStringLiteral("edge0PointXLimitHigh"));
        pointJudge(boolParam(QStringLiteral("edge0PointYLimitEnable")),
                   QStringLiteral("边缘点0Y判断"), hasResult, e0y,
                   QStringLiteral("edge0PointYLimitLow"), QStringLiteral("edge0PointYLimitHigh"));
        pointJudge(boolParam(QStringLiteral("edge1PointXLimitEnable")),
                   QStringLiteral("边缘点1X判断"), hasResult && edgeMode == 1, e1x,
                   QStringLiteral("edge1PointXLimitLow"), QStringLiteral("edge1PointXLimitHigh"));
        pointJudge(boolParam(QStringLiteral("edge1PointYLimitEnable")),
                   QStringLiteral("边缘点1Y判断"), hasResult && edgeMode == 1, e1y,
                   QStringLiteral("edge1PointYLimitLow"), QStringLiteral("edge1PointYLimitHigh"));
        if (judgedOk) {
            m_params["moduleStatus"] = true;
            setResultNg(false);
        } else {
            m_params["moduleStatus"] = false;
            m_params[QStringLiteral("judgeNote")] = judgeReason;
            // 判 NG 但结果有效：保留输出端口，画布/结果面板显示红色结果
            setResultNg(true);
        }
        m_outputImage = m_inputImage;

        // 输出边缘点/边对中点（Measure：value=结果数，extraValues=[col0,row0,col1,row1,...]）
        QVector<double> points;
        if (edgeMode == 1) {
            for (const PairHit &p : pairs) {
                points.append((p.col1 + p.col2) * 0.5);
                points.append((p.row1 + p.row2) * 0.5);
            }
        } else {
            for (const EdgeHit &e : hits) {
                points.append(e.col);
                points.append(e.row);
            }
        }

        // 「当前结果」逐结果字段：edgePoints="col,row;…"（官方 边缘点*X/Y），
        // pairLines="c1,r1,c2,r2,w;…"（官方 直线*起点/终点）。
        QStringList pointTexts;
        if (edgeMode == 1) {
            for (const PairHit &p : pairs) {
                pointTexts << QStringLiteral("%1,%2")
                                  .arg((p.col1 + p.col2) * 0.5, 0, 'f', 2)
                                  .arg((p.row1 + p.row2) * 0.5, 0, 'f', 2);
            }
        } else {
            for (const EdgeHit &e : hits)
                pointTexts << QStringLiteral("%1,%2").arg(e.col, 0, 'f', 2).arg(e.row, 0, 'f', 2);
        }
        m_params[QStringLiteral("edgePoints")] = pointTexts.join(QLatin1Char(';'));

        QStringList lineTexts;
        for (const PairHit &p : pairs) {
            lineTexts << QStringLiteral("%1,%2,%3,%4,%5")
                             .arg(p.col1, 0, 'f', 2)
                             .arg(p.row1, 0, 'f', 2)
                             .arg(p.col2, 0, 'f', 2)
                             .arg(p.row2, 0, 'f', 2)
                             .arg(p.width, 0, 'f', 2);
        }
        m_params[QStringLiteral("pairLines")] = lineTexts.join(QLatin1Char(';'));

        // VM「模块结果 → 检测区域」：中心点/宽/高/角度。
        // 角度按图像坐标（y 向下）计算，顺时针为正，与官方「检测区域角度」定义一致。
        m_params[QStringLiteral("roiCenterX")] = (c1 + c2) * 0.5;
        m_params[QStringLiteral("roiCenterY")] = (r1 + r2) * 0.5;
        m_params[QStringLiteral("roiWidth")] = L;
        m_params[QStringLiteral("roiHeight")] = 2.0 * static_cast<double>(half);
        m_params[QStringLiteral("roiAngle")] =
            std::atan2(dy, dx) * (180.0 / 3.14159265358979323846);
        MeasureResult res;
        res.type = QStringLiteral("caliper");
        res.valueName = edgeMode == 1 ? QStringLiteral("边对") : QStringLiteral("边缘点");
        res.valid = resultCount > 0;
        res.value = static_cast<double>(resultCount);
        res.extraValues = points;
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        auto numObj = QSharedPointer<DataObject>::create();
        numObj->setValue(double(resultCount));
        setOutputData(2, numObj);

        // 边对明细（type="caliperPair"：extraValues=[col1,row1,col2,row2,width,...]）
        if (edgeMode == 1 && !pairs.empty()) {
            MeasureResult pairRes;
            pairRes.type = QStringLiteral("caliperPair");
            pairRes.valueName = QStringLiteral("边对宽度");
            pairRes.valid = true;
            pairRes.value = avgWidth;
            pairRes.point1 = QPointF((pairs.front().col1 + pairs.front().col2) * 0.5,
                                     (pairs.front().row1 + pairs.front().row2) * 0.5);
            QVector<double> pairExtra;
            for (const PairHit &p : pairs) {
                pairExtra.append(p.col1);
                pairExtra.append(p.row1);
                pairExtra.append(p.col2);
                pairExtra.append(p.row2);
                pairExtra.append(p.width);
            }
            pairRes.extraValues = pairExtra;
            auto pairObj = QSharedPointer<DataObject>::create();
            pairObj->setMeasureResult(pairRes);
            setOutputData(3, pairObj);
        } else {
            setOutputData(3, QSharedPointer<DataObject>());
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvCaliperNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());   // 原因到面板（2026-10-26）
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

/// VM 卡尺工具的条件显隐。官方「参数配置」原文中反复出现的条款：
///   「边缘模式设置为边缘对时，才有该参数」——适用于 边缘1极性 / 边缘对宽度 /
///    归一位置 / 归一相对位置 / 间距 / 间距差 / 相对间距差；
///   「边缘模式设置为单边缘时，该参数为对比度；设置为边缘对时，为边缘对对比度」。
/// 计分曲线参数则只在至少一个计分项启用时出现（官方把曲线配置挂在计分项下面）。
/// 实现沿用 OpencvBlobNode::applyParamVisibility 的先例：自动面板给控件设的
/// objectName 就是参数名，用 QFormLayout::setRowVisible 连标签一起隐藏。
void OpencvCaliperNode::applyParamVisibility(QWidget *panel)
{
    if (!panel)
        return;

    auto enumIndex = [panel, this](const QString &name) -> int {
        if (auto *c = panel->findChild<QComboBox *>(name))
            return c->currentIndex();
        return m_params.value(name, 0).toInt();
    };
    auto boolValue = [panel, this](const QString &name) -> bool {
        if (auto *c = panel->findChild<QCheckBox *>(name))
            return c->isChecked();
        return m_params.value(name, false).toBool();
    };
    const bool pairMode = (enumIndex(QStringLiteral("edgeMode")) == 1);

    static const QSet<QString> pairOnly {
        QStringLiteral("edge1Polarity"),
        QStringLiteral("expectedPairWidth"),
        QStringLiteral("scoreSpacing"), QStringLiteral("scoreSpacingDiff"),
        QStringLiteral("scoreRelativeSpacingDiff"), QStringLiteral("scorePairContrast"),
        QStringLiteral("scoreNormPosition"), QStringLiteral("scoreNormRelativePosition"),
        // 结果判断：宽度判断与边缘点1判断只在边缘对模式有意义
        QStringLiteral("edgeWidthLimitEnable"), QStringLiteral("edgeWidthLimitLow"),
        QStringLiteral("edgeWidthLimitHigh"),
        QStringLiteral("edge1PointXLimitEnable"), QStringLiteral("edge1PointXLimitLow"),
        QStringLiteral("edge1PointXLimitHigh"),
        QStringLiteral("edge1PointYLimitEnable"), QStringLiteral("edge1PointYLimitLow"),
        QStringLiteral("edge1PointYLimitHigh")
    };
    // 每个计分项各有一组曲线参数：只在该计分项启用时显示（官方 XML 的分组方式）。
    auto curveKeyNames = [](const QString &prefix, const QString &suffix = QString()) {
        return QStringList{prefix + QStringLiteral("Curve") + suffix,
                           prefix + QStringLiteral("X0") + suffix,
                           prefix + QStringLiteral("X1") + suffix,
                           prefix + QStringLiteral("XC") + suffix,
                           prefix + QStringLiteral("Y0") + suffix,
                           prefix + QStringLiteral("Y1") + suffix};
    };
    QHash<QString, bool> curveVisible;
    auto gate = [&](const QString &prefix, const QString &suffix, bool on) {
        for (const QString &k : curveKeyNames(prefix, suffix))
            curveVisible.insert(k, on);
    };
    gate(QStringLiteral("spacing"), QString(),
         pairMode && boolValue(QStringLiteral("scoreSpacing")));
    gate(QStringLiteral("spacingDiff"), QString(),
         pairMode && boolValue(QStringLiteral("scoreSpacingDiff")));
    gate(QStringLiteral("spacingDiffRelative"), QString(),
         pairMode && boolValue(QStringLiteral("scoreRelativeSpacingDiff")));
    gate(QStringLiteral("spacingDiffRelative"), QStringLiteral("H"),
         pairMode && boolValue(QStringLiteral("scoreRelativeSpacingDiff")));
    // 对比度/边缘对对比度共用一组曲线：按当前模式只认可见的那个开关
    gate(QStringLiteral("contrast"), QString(),
         pairMode ? boolValue(QStringLiteral("scorePairContrast"))
                  : boolValue(QStringLiteral("scoreContrast")));
    gate(QStringLiteral("position"), QString(), boolValue(QStringLiteral("scorePosition")));
    gate(QStringLiteral("positionRelative"), QString(),
         boolValue(QStringLiteral("scoreRelativePosition")));
    gate(QStringLiteral("positionNorm"), QString(),
         pairMode && boolValue(QStringLiteral("scoreNormPosition")));
    gate(QStringLiteral("positionNormRelative"), QString(),
         pairMode && boolValue(QStringLiteral("scoreNormRelativePosition")));
    gate(QStringLiteral("gray"), QString(), boolValue(QStringLiteral("scoreGray")));

    // 遍历所有分组的 form（参数分散在「卡尺/边缘/评分/结果」多个 group）
    for (QFormLayout *form : panel->findChildren<QFormLayout *>()) {
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem *fieldItem = form->itemAt(row, QFormLayout::FieldRole);
            QWidget *field = fieldItem ? fieldItem->widget() : nullptr;
            const QString name = field ? field->objectName() : QString();
            if (name.isEmpty())
                continue;
            bool show = true;
            if (pairOnly.contains(name))
                show = pairMode;
            else if (name == QStringLiteral("scoreContrast"))
                show = !pairMode;   // 单边缘专属；边缘对用 scorePairContrast
            else if (curveVisible.contains(name))
                show = curveVisible.value(name);
            form->setRowVisible(row, show);
        }
    }
}

QWidget *OpencvCaliperNode::createParamPanel()
{
    QWidget *panel = createAutoParamPanel();
    // 边缘模式切换、任一计分开关切换都要即时刷新显隐
    if (auto *modeCombo = panel->findChild<QComboBox *>(QStringLiteral("edgeMode"))) {
        connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, panel](int) { applyParamVisibility(panel); });
    }
    static const QStringList scoreWidgets {
        QStringLiteral("scoreSpacing"), QStringLiteral("scoreSpacingDiff"),
        QStringLiteral("scoreRelativeSpacingDiff"), QStringLiteral("scoreContrast"),
        QStringLiteral("scorePairContrast"),
        QStringLiteral("scorePosition"), QStringLiteral("scoreRelativePosition"),
        QStringLiteral("scoreNormPosition"), QStringLiteral("scoreNormRelativePosition"),
        QStringLiteral("scoreGray")
    };
    for (const QString &k : scoreWidgets) {
        if (auto *cb = panel->findChild<QCheckBox *>(k)) {
            connect(cb, &QCheckBox::toggled, this,
                    [this, panel](bool) { applyParamVisibility(panel); });
        }
    }
    applyParamVisibility(panel);
    return panel;
}

void OpencvCaliperNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
    applyParamVisibility(panel);
}

QVector<NodeResultField> OpencvCaliperNode::resultDisplayFields() const
{
    // 官方「模块结果」字段表（GUID-59D1E6A2「模块结果」节）逐项照抄，
    // 顺序：耗时 → 模块状态 → 边缘个数 → 分数 → 边缘*极性 → 边缘*位置 →
    //       边缘点*X/Y → 测量宽度 → 直线*起点/终点 → 检测区域。
    // 「当前结果」面板按本表渲染，配置参数不再混进结果表。
    const bool pairMode = m_params.value(QStringLiteral("edgeMode"), 0).toInt() == 1;
    const QString group = pairMode ? QStringLiteral("边对%1") : QStringLiteral("边缘%1");

    QVector<NodeResultField> fields;
    fields << resultValue(QStringLiteral("elapsedMs"), QStringLiteral("耗时"),
                          QStringLiteral("ms"), 0)
           << resultStatus(QStringLiteral("moduleStatus"), QStringLiteral("模块状态"))
           << resultValue(pairMode ? QStringLiteral("pairCount") : QStringLiteral("edgeCount"),
                          pairMode ? QStringLiteral("边缘对个数") : QStringLiteral("边缘个数"),
                          QString(), 0);
    if (pairMode) {
        fields << resultValue(QStringLiteral("avgWidth"), QStringLiteral("测量宽度"),
                              QStringLiteral("px"), 2)
               << resultValue(QStringLiteral("minWidth"), QStringLiteral("最小宽度"),
                              QStringLiteral("px"), 2)
               << resultValue(QStringLiteral("maxWidth"), QStringLiteral("最大宽度"),
                              QStringLiteral("px"), 2);
    }
    fields << resultList(QStringLiteral("edgeScores"), group, QStringLiteral("分数"),
                         QString(), 4)
           << resultPolarity(QStringLiteral("edgePolarities"), group, QStringLiteral("极性"))
           << resultList(QStringLiteral("edgePositions"), group, QStringLiteral("位置"),
                         QStringLiteral("px"), 2)
           << resultPoint(QStringLiteral("edgePoints"), group,
                          pairMode ? QStringLiteral("中心点") : QStringLiteral("边缘点"), 2)
           << resultList(QStringLiteral("edgeGrays"), group, QStringLiteral("灰度"),
                         QString(), 1);
    if (pairMode) {
        fields << resultPairLine(QStringLiteral("pairLines"), QStringLiteral("直线%1"),
                                 QStringLiteral("起点/终点"), 2);
    }
    fields << resultValue(QStringLiteral("roiCenterX"), VmSharedText::roiCenterXLabel(),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("roiCenterY"), VmSharedText::roiCenterYLabel(),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("roiWidth"), QStringLiteral("检测区域宽度"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("roiHeight"), QStringLiteral("检测区域高度"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("roiAngle"), QStringLiteral("检测区域角度"),
                          QStringLiteral("°"), 3);
    return fields;
}

RoiShape OpencvCaliperNode::geometryRoi() const
{
    RoiShape s;
    s.type = RoiType::Line;
    s.p1 = QPointF(m_params.value(QStringLiteral("col1"), 100.0).toDouble(),
                   m_params.value(QStringLiteral("row1"), 100.0).toDouble());
    s.p2 = QPointF(m_params.value(QStringLiteral("col2"), 100.0).toDouble(),
                   m_params.value(QStringLiteral("row2"), 200.0).toDouble());
    return s;
}

void OpencvCaliperNode::applyGeometryRoi(const RoiShape &shape)
{
    if (shape.type != RoiType::Line)
        return;
    setParam(QStringLiteral("col1"), shape.p1.x());
    setParam(QStringLiteral("row1"), shape.p1.y());
    setParam(QStringLiteral("col2"), shape.p2.x());
    setParam(QStringLiteral("row2"), shape.p2.y());
}
