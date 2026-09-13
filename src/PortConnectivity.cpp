#include "PortConnectivity.h"
#include "Port.h"
#include "NodeBase.h"
#include "DataObject.h"
#include <QObject>

PortDataType portDataTypeFromDataObject(int dataTypeEnumOrd)
{
    using DT = DataObject::DataType;
    const auto dt = static_cast<DT>(dataTypeEnumOrd);
    switch (dt) {
    case DT::Image:
        return PortDataType::Image;
    case DT::Region:
        return PortDataType::Region;
    case DT::XLD:
        return PortDataType::XLD;
    case DT::Number:
        return PortDataType::Number;
    case DT::String:
        return PortDataType::String;
    case DT::Array:
        return PortDataType::Array;
    case DT::Point:
        return PortDataType::Point;
    case DT::Measure:
        return PortDataType::Measure;
    case DT::Matrix:
        return PortDataType::Matrix;
    case DT::Bool:
        return PortDataType::Bool;
    case DT::Unknown:
    default:
        return PortDataType::Unknown;
    }
}

QString PortConnectivity::formatTypeMismatchHint(PortDataType src, PortDataType dst)
{
    return QObject::tr("类型不匹配：输出为 [%1]，输入需要 [%2]")
        .arg(portDataTypeLabel(src))
        .arg(portDataTypeLabel(dst));
}

bool PortConnectivity::canConnectPorts(const Port *sourceOut, const Port *targetIn,
                                       QString *rejectReasonOut, bool checkSemanticTypes)
{
    if (!sourceOut || !targetIn) {
        if (rejectReasonOut)
            *rejectReasonOut = QObject::tr("端口无效");
        return false;
    }
    if (sourceOut->type() != Port::OUTPUT || targetIn->type() != Port::INPUT) {
        if (rejectReasonOut)
            *rejectReasonOut = QObject::tr("请从输出端口连接到输入端口");
        return false;
    }
    NodeBase *srcNode = sourceOut->node();
    NodeBase *dstNode = targetIn->node();
    if (!srcNode || !dstNode || srcNode == dstNode) {
        if (rejectReasonOut)
            *rejectReasonOut = QObject::tr("不可连接同一算子的输出与输入");
        return false;
    }
    if (targetIn->isConnected()) {
        if (rejectReasonOut)
            *rejectReasonOut = QObject::tr("该输入端口已连接，请先断开");
        return false;
    }
    if (!checkSemanticTypes)
        return true;

    const PortDataType srcT = sourceOut->dataType();
    const PortDataType dstT = targetIn->dataType();
    if (!portTypesWireCompatible(srcT, dstT)) {
        if (rejectReasonOut)
            *rejectReasonOut = PortConnectivity::formatTypeMismatchHint(srcT, dstT);
        return false;
    }
    return true;
}
