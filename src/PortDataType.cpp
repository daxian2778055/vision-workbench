#include "PortDataType.h"

QString portDataTypeLabel(PortDataType t)
{
    switch (t) {
    case PortDataType::Any:
        return QStringLiteral("任意");
    case PortDataType::Unknown:
        return QStringLiteral("未标注");
    case PortDataType::Image:
        return QStringLiteral("图像");
    case PortDataType::Region:
        return QStringLiteral("区域");
    case PortDataType::XLD:
        return QStringLiteral("XLD/轮廓");
    case PortDataType::Number:
        return QStringLiteral("数值");
    case PortDataType::String:
        return QStringLiteral("字符串");
    case PortDataType::Array:
        return QStringLiteral("数组");
    case PortDataType::Point:
        return QStringLiteral("点");
    case PortDataType::Measure:
        return QStringLiteral("测量结果");
    case PortDataType::Matrix:
        return QStringLiteral("变换矩阵");
    case PortDataType::Bool:
        return QStringLiteral("布尔");
    }
    return QStringLiteral("?");
}
