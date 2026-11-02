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
    QMutexLocker locker(&m_mutex);
    return m_type;
}

QVariant DataObject::getData() const
{
    QMutexLocker locker(&m_mutex);
    return m_data;
}

void DataObject::setData(const QVariant &data)
{
    QMutexLocker locker(&m_mutex);
    m_data = data;
}

// HImage相关方法
HalconCpp::HImage DataObject::getHImage() const
{
    QMutexLocker locker(&m_mutex);
    return m_hImage;
}

void DataObject::setHImage(const HalconCpp::HImage &image)
{
    QMutexLocker locker(&m_mutex);
    // 清理旧的HImage对象
    if (m_hImage.IsInitialized()) {
        m_hImage.Clear();
    }
    // 复制新的HImage对象
    m_hImage = image;
    m_type = DataType::Image;
    // 这里**不再**额外写一份 m_data = QVariant::fromValue(image)：
    // 全仓读图像一律走 getHImage()，没有任何 getData().value<HImage>() 的读取者
    // （已用 grep 核实），那份 QVariant 只是每次赋值多一次元类型拷贝与堆分配。
    // m_data 仍继续承担数值/字符串/Bool/MeasureResult/Point 等非图像类型。
}

HalconCpp::HObject DataObject::getHObject() const
{
    QMutexLocker locker(&m_mutex);
    return m_hObject;
}

void DataObject::setHObject(const HalconCpp::HObject &obj)
{
    QMutexLocker locker(&m_mutex);
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
    QMutexLocker locker(&m_mutex);
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
    QMutexLocker locker(&m_mutex);
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
    QMutexLocker locker(&m_mutex);
    if (!m_measureResults.isEmpty())
        return m_measureResults.first();
    if (m_data.canConvert<MeasureResult>())
        return m_data.value<MeasureResult>();
    return MeasureResult();
}

void DataObject::setMeasureResult(const MeasureResult &r)
{
    QMutexLocker locker(&m_mutex);
    m_measureResults.clear();
    m_measureResults.append(r);
    m_data = QVariant::fromValue(r);
    m_type = DataType::Measure;
}

void DataObject::attachMeasureResult(const MeasureResult &r)
{
    QMutexLocker locker(&m_mutex);
    m_measureResults.append(r);
    // 主值不是数据类时，把测量结果也作为老读取路径（getData()）的兜底：
    // 统一输出端口上"图像 + 结果"共用一个对象，老的 `getData().value<MeasureResult>()`
    // 调用方（若有）仍能拿到结果，不至于因为类型变成 Image 就丢失。
    if (m_type == DataType::Unknown || m_type == DataType::Image)
        m_data = QVariant::fromValue(r);
}

QVector<MeasureResult> DataObject::measureResults() const
{
    QMutexLocker locker(&m_mutex);
    if (!m_measureResults.isEmpty())
        return m_measureResults;
    if (m_data.canConvert<MeasureResult>())
        return {m_data.value<MeasureResult>()};
    return {};
}

bool DataObject::hasMeasureResult() const
{
    QMutexLocker locker(&m_mutex);
    return !m_measureResults.isEmpty() || m_data.canConvert<MeasureResult>();
}

void DataObject::attachResult(const QString &name, const QSharedPointer<DataObject> &object)
{
    if (!object)
        return;
    QMutexLocker locker(&m_mutex);
    m_attachedResults.append(qMakePair(name, object));
}

QVector<QPair<QString, QSharedPointer<DataObject>>> DataObject::attachedResults() const
{
    QMutexLocker locker(&m_mutex);
    return m_attachedResults;
}

QSharedPointer<DataObject> DataObject::attachedResult(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    for (const auto &item : m_attachedResults) {
        if (item.first == name)
            return item.second;
    }
    return {};
}

QVariant DataObject::primaryValue() const
{
    QMutexLocker locker(&m_mutex);
    if (m_type == DataType::Measure) {
        if (!m_measureResults.isEmpty())
            return m_measureResults.first().valid ? QVariant(m_measureResults.first().value)
                                                  : QVariant();
        if (m_data.canConvert<MeasureResult>()) {
            const MeasureResult r = m_data.value<MeasureResult>();
            return r.valid ? QVariant(r.value) : QVariant();
        }
        return {};
    }
    // 主类型是图像时，m_data 里若挂了测量结果（历史 attach 兜底）不冒充主值
    if (m_type == DataType::Image)
        return {};
    return m_data;
}

void DataObject::setPrimaryName(const QString &name)
{
    QMutexLocker locker(&m_mutex);
    m_primaryName = name;
}

QString DataObject::primaryName() const
{
    QMutexLocker locker(&m_mutex);
    return m_primaryName;
}

double DataObject::primaryNumber(bool *ok) const
{
    if (ok)
        *ok = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_type == DataType::Measure) {
            MeasureResult r;
            if (!m_measureResults.isEmpty())
                r = m_measureResults.first();
            else if (m_data.canConvert<MeasureResult>())
                r = m_data.value<MeasureResult>();
            if (r.valid) {
                if (ok)
                    *ok = true;
                return r.value;
            }
            return 0.0;
        }
        if (m_type != DataType::Image) {
            bool conv = false;
            const double v = m_data.toDouble(&conv);
            if (conv) {
                if (ok)
                    *ok = true;
                return v;
            }
        }
        // 图像对象也允许带附加测量结果（统一输出）：数值侧回落到结果
        if (!m_measureResults.isEmpty() && m_measureResults.first().valid) {
            if (ok)
                *ok = true;
            return m_measureResults.first().value;
        }
    }
    return 0.0;
}

DetectionResult DataObject::getDetectionResult() const
{
    QMutexLocker locker(&m_mutex);
    if (m_data.canConvert<DetectionResult>())
        return m_data.value<DetectionResult>();
    return DetectionResult();
}

void DataObject::setDetectionResult(const DetectionResult &r)
{
    QMutexLocker locker(&m_mutex);
    m_data = QVariant::fromValue(r);
    m_type = DataType::Detections;
}

QPointF DataObject::getPoint() const
{
    QMutexLocker locker(&m_mutex);
    return m_data.toPointF();
}

void DataObject::setPoint(const QPointF &p)
{
    QMutexLocker locker(&m_mutex);
    m_data = p;
    m_type = DataType::Point;
}

QString DataObject::getTypeString() const
{
    QMutexLocker locker(&m_mutex);
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
        case DataType::Detections:
            return "Detections";
        default:
            return "Unknown";
    }
}

QString DataObject::sourceInfo() const
{
    QMutexLocker locker(&m_mutex);
    return m_sourceInfo;
}

void DataObject::setSourceInfo(const QString &sourceInfo)
{
    QMutexLocker locker(&m_mutex);
    m_sourceInfo = sourceInfo;
}
