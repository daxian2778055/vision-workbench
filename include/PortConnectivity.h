#pragma once

#include "PortDataType.h"

class Port;

/// DataObject::DataType 与 PortDataType 对应（供执行阶段扩展使用）
PortDataType portDataTypeFromDataObject(int dataTypeEnumOrd);

inline bool portTypesWireCompatible(PortDataType src, PortDataType dst)
{
    if (src == PortDataType::Any || dst == PortDataType::Any)
        return true;
    if (src == PortDataType::Unknown || dst == PortDataType::Unknown)
        return true;
    return src == dst;
}

class PortConnectivity
{
public:
    /**
     * @param checkSemanticTypes 为 false 时跳过类型兼容性校验（工程等反序列化用）
     */
    static bool canConnectPorts(const Port *sourceOut, const Port *targetIn,
                                QString *rejectReasonOut = nullptr,
                                bool checkSemanticTypes = true);

    static QString formatTypeMismatchHint(PortDataType src, PortDataType dst);
};
