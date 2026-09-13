#include "DataObject.h"

DataObject::DataObject()
    : m_type(DataType::Unknown)
{
}

DataObject::DataObject(DataType type, const QVariant &data)
    : m_type(type)
    , m_data(data)
{
}

DataObject::~DataObject()
{
    // 清理HImage对象
    if (m_hImage.IsInitialized()) {
        m_hImage.Clear();
    }
    if (m_hObject.IsInitialized()) {
        m_hObject.Clear();
    }
}

DataObject::DataType DataObject::getType() const
{
    return m_type;
}

QVariant DataObject::getData() const
{
    return m_data;
}

void DataObject::setData(const QVariant &data)
{
    m_data = data;
}

// HImage相关方法
HalconCpp::HImage DataObject::getHImage() const
{
    return m_hImage;
}

void DataObject::setHImage(const HalconCpp::HImage &image)
{
    // 清理旧的HImage对象
    if (m_hImage.IsInitialized()) {
        m_hImage.Clear();
    }
    // 复制新的HImage对象
    m_hImage = image;
    m_type = DataType::Image;
    // 同时更新m_data成员变量
    m_data = QVariant::fromValue(image);
}

HalconCpp::HObject DataObject::getHObject() const
{
    return m_hObject;
}

void DataObject::setHObject(const HalconCpp::HObject &obj)
{
    if (m_hObject.IsInitialized()) {
        m_hObject.Clear();
    }
    m_hObject = obj;
    // 类型推断
    if (obj.IsInitialized()) {
        // Region 与 XLD 均可用 HObject 承载，交由 getType() 用户判断
        m_type = DataType::Region;
    }
}

HalconCpp::HRegion DataObject::getHRegion() const
{
    if (m_hObject.IsInitialized())
        return HalconCpp::HRegion(m_hObject);
    return HalconCpp::HRegion();
}

void DataObject::setHRegion(const HalconCpp::HRegion &region)
{
    setHObject(HalconCpp::HObject(region));
    m_type = DataType::Region;
}

HalconCpp::HXLDCont DataObject::getHXLDCont() const
{
    if (m_hObject.IsInitialized())
        return HalconCpp::HXLDCont(m_hObject);
    return HalconCpp::HXLDCont();
}

void DataObject::setHXLDCont(const HalconCpp::HXLDCont &xld)
{
    setHObject(HalconCpp::HObject(xld));
    m_type = DataType::XLD;
}

MeasureResult DataObject::getMeasureResult() const
{
    if (m_data.canConvert<MeasureResult>())
        return m_data.value<MeasureResult>();
    return MeasureResult();
}

void DataObject::setMeasureResult(const MeasureResult &r)
{
    m_data = QVariant::fromValue(r);
    m_type = DataType::Measure;
}

QPointF DataObject::getPoint() const
{
    return m_data.toPointF();
}

void DataObject::setPoint(const QPointF &p)
{
    m_data = p;
    m_type = DataType::Point;
}

QString DataObject::getTypeString() const
{
    switch (m_type) {
        case DataType::Image:
            return "Image";
        case DataType::Region:
            return "Region";
        case DataType::XLD:
            return "XLD";
        case DataType::Number:
            return "Number";
        case DataType::String:
            return "String";
        case DataType::Array:
            return "Array";
        case DataType::Point:
            return "Point";
        case DataType::Measure:
            return "Measure";
        case DataType::Matrix:
            return "Matrix";
        case DataType::Bool:
            return "Bool";
        default:
            return "Unknown";
    }
}

QString DataObject::sourceInfo() const
{
    return m_sourceInfo;
}

void DataObject::setSourceInfo(const QString &sourceInfo)
{
    m_sourceInfo = sourceInfo;
}
