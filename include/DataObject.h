#pragma once

#include <QString>
#include <QVariant>
#include <QPointF>
#include <QVector>
#include <QSharedPointer>
#include <HalconCpp.h>

/// 测量结果结构（供卡尺、距离、角度等测量算子输出）
struct MeasureResult {
    bool valid = false;           /// 是否测量成功
    double value = 0.0;           /// 主测量值
    QString valueName;            /// 测量值名称（如 "距离"、"角度"）
    QPointF point1;               /// 相关点 1
    QPointF point2;               /// 相关点 2
    QVector<double> extraValues;  /// 附加数值（如多条边）
    QString type;                 /// 结果类型描述
};
Q_DECLARE_METATYPE(MeasureResult)

/// 单个目标检测框（原图像素坐标：左上角 x,y + 宽高 w,h）
struct DetectionBox {
    int classId = -1;            /// 类别索引（-1 表示未分类）
    QString className;           /// 类别名（由 classes.txt 解析）
    double confidence = 0.0;     /// 置信度 [0,1]
    double x = 0.0, y = 0.0;     /// 左上角坐标
    double w = 0.0, h = 0.0;     /// 宽高
};
Q_DECLARE_METATYPE(DetectionBox)

/// 目标检测结果：若干检测框 + 原图尺寸
struct DetectionResult {
    QVector<DetectionBox> boxes;
    int imageWidth = 0;
    int imageHeight = 0;
};
Q_DECLARE_METATYPE(DetectionResult)

class DataObject
{
public:
    enum class DataType {
        Image,
        Region,
        XLD,
        Number,
        String,
        Array,
        Point,      /// 二维点（QPointF）
        Measure,    /// 测量结果（MeasureResult）
        Matrix,     /// 齐次变换矩阵（HOMat2D / HTuple 6 元组）
        Bool,       /// 布尔
        Detections, /// 目标检测结果（DetectionResult）
        Unknown
    };

    DataObject();
    DataObject(DataType type, const QVariant &data);
    ~DataObject();

    DataType getType() const;
    QVariant getData() const;
    void setData(const QVariant &data);
    void setType(DataType t) { m_type = t; }
    
    // 通用值设置方法（用于设置bool、int、double等简单类型）
    template<typename T>
    void setValue(const T &value) {
        m_data = QVariant::fromValue(value);
        m_type = DataType::Number;
    }

    // HImage相关方法
    HalconCpp::HImage getHImage() const;
    void setHImage(const HalconCpp::HImage &image);

    // 通用 HObject（Region / XLD 等）
    HalconCpp::HObject getHObject() const;
    void setHObject(const HalconCpp::HObject &obj);

    // Region 便捷访问
    HalconCpp::HRegion getHRegion() const;
    void setHRegion(const HalconCpp::HRegion &region);

    // XLD 便捷访问
    HalconCpp::HXLDCont getHXLDCont() const;
    void setHXLDCont(const HalconCpp::HXLDCont &xld);

    // 测量结果
    MeasureResult getMeasureResult() const;
    void setMeasureResult(const MeasureResult &r);

    // 目标检测结果
    DetectionResult getDetectionResult() const;
    void setDetectionResult(const DetectionResult &r);

    // 点
    QPointF getPoint() const;
    void setPoint(const QPointF &p);

    QString getTypeString() const;

private:
    DataType m_type;
    QVariant m_data;
    HalconCpp::HImage m_hImage; // 专门存储HImage对象
    HalconCpp::HObject m_hObject; // 通用 HObject（Region/XLD 等）
    QString m_sourceInfo; // 数据来源信息

public:
    QString sourceInfo() const;
    void setSourceInfo(const QString &sourceInfo);
};

using DataObjectPtr = QSharedPointer<DataObject>;
