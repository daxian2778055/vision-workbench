#include "CalibrationWizard.h"

namespace CalibrationWizard {

QVector<CalibrationWizardSpec> allSpecs()
{
    QVector<CalibrationWizardSpec> specs;

    // 官方「静态标定」子页 GUID-5F52CE25-A6F2-42DD-B717-8BF0244F04B6
    CalibrationWizardSpec s;
    s.type = CalibrationWizardType::Static;
    s.name = QStringLiteral("静态标定");
    s.description = QStringLiteral(
        "通过静止的单相机和标定板确定图像坐标系和物理坐标系之间的转换关系。");
    s.targetModule = QStringLiteral("标定板标定");
    s.steps = {
        QStringLiteral("配置触发信号：设置控制流程执行的字符串信息"),
        QStringLiteral("配置相机管理：添加并配置全局相机"),
        QStringLiteral("配置图像源模块：设置本地图像 / 相机 / SDK 输入"),
        QStringLiteral("选择标定方式（当前仅支持标定板标定）"),
        QStringLiteral("配置标定板标定模块：标定板类型与物理尺寸"),
        QStringLiteral("查看模块搭建及配置情况"),
        QStringLiteral("进入结果查看：执行标定并生成标定文件与标定矩阵")
    };
    // 本实现已有算子链：读取图像(ImageReadNode) → 相机标定(OpencvCalibNode)
    s.flowNodes = { QStringLiteral("读取图像"), QStringLiteral("相机标定") };
    s.flowNodeIds = { QStringLiteral("ImageReadNode"), QStringLiteral("OpencvCalibNode") };
    s.supported = true;
    specs.append(s);

    // 官方主文档只给动态/映射的释义与落地模块名；子页步骤依赖本实现未注册的算子，
    // 因此不编造步骤、不生成流程，直接给出缺模块的说明。
    CalibrationWizardSpec d;
    d.type = CalibrationWizardType::Dynamic;
    d.name = QStringLiteral("动态标定");
    d.description = QStringLiteral(
        "通过特征点的移动确定图像坐标系和物理坐标系之间的转换关系以及旋转中心。");
    d.targetModule = QStringLiteral("N图像标定 / N点标定");
    d.supported = false;
    d.unsupportedReason = QStringLiteral(
        "官方动态标定依赖 N 图像标定 / N 点标定模块；本实现仅有 N 点标定算子，"
        "尚不具备完整引导链，暂不生成流程。");
    specs.append(d);

    CalibrationWizardSpec m;
    m.type = CalibrationWizardType::Mapping;
    m.name = QStringLiteral("映射标定");
    m.description = QStringLiteral(
        "通过特征点确定两个相机之间图像坐标系的转换关系。");
    m.targetModule = QStringLiteral("映射标定");
    m.supported = false;
    m.unsupportedReason = QStringLiteral(
        "官方映射标定依赖映射标定模块；本实现未注册该算子，暂不生成流程。");
    specs.append(m);

    return specs;
}

CalibrationWizardSpec specFor(CalibrationWizardType type)
{
    const QVector<CalibrationWizardSpec> specs = allSpecs();
    for (const CalibrationWizardSpec &s : specs) {
        if (s.type == type)
            return s;
    }
    return CalibrationWizardSpec();
}

}   // namespace CalibrationWizard
