#pragma once

#include <QString>
#include <QVariant>
#include <QPointF>
#include <QVector>
#include <QPair>
#include <QSharedPointer>
#include <QMutex>
#include <QMutexLocker>
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
    // 公开 setter 也必须走读写锁：执行线程算子（如 propagateData 打来源戳、节点复用输出对象）
    // 会在发布后仍写 m_type/m_data，而界面线程同时在读——无锁即为数据竞争（S8 旁路）。
    void setType(DataType t) { QMutexLocker lock(&m_mutex); m_type = t; }

    // 通用值设置方法（用于设置bool、int、double等简单类型）
    template<typename T>
    void setValue(const T &value) {
        QMutexLocker lock(&m_mutex);
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

    // ---- 统一输出（B1）：同一对象可同时携带图像 + 测量结果 + 其它工具输出 ----
    /// 附加一路测量结果（不改变主类型，供"图像 + 结果"同端口输出用；可多路）
    void attachMeasureResult(const MeasureResult &r);
    /// 全部附加测量结果（按附加顺序；老对象无附加时由主值派生 0/1 项）
    QVector<MeasureResult> measureResults() const;
    /// 是否带测量结果（不看主类型）
    bool hasMeasureResult() const;
    /// 附加一路具名子结果（原多输出工具的第 2..N 个端口：二值图/XLD/区域/数值…）
    void attachResult(const QString &name, const QSharedPointer<DataObject> &object);
    QVector<QPair<QString, QSharedPointer<DataObject>>> attachedResults() const;
    QSharedPointer<DataObject> attachedResult(const QString &name) const;
    /// 主值：Number/String/Bool/Array/Point/Matrix 直接取 m_data；Measure 取 value
    QVariant primaryValue() const;
    /// 主数值（数据类算子取数统一入口；Measure 走 value，无效测量 ok=false）
    double primaryNumber(bool *ok = nullptr) const;
    /// 主结果在原端口清单里的名字（聚合输出后运行界面按名取值的依据）
    void setPrimaryName(const QString &name);
    QString primaryName() const;

    // 目标检测结果
    DetectionResult getDetectionResult() const;
    void setDetectionResult(const DetectionResult &r);

    // 点
    QPointF getPoint() const;
    void setPoint(const QPointF &p);

    QString getTypeString() const;

private:
    /// 内部读写锁：DataObject 发布到输出端口**之后**仍可能被写
    /// （如 FlowExecutor::propagateData 打来源戳、节点复用输出对象），
    /// 而界面线程同时在读（结果面板/变量面板/预览）——无锁即为数据竞争。
    /// 用递归锁：setHRegion / setHXLDCont 内部复用 setHObject。
    mutable QRecursiveMutex m_mutex;

    DataType m_type;
    QVariant m_data;
    HalconCpp::HImage m_hImage; // 专门存储HImage对象
    HalconCpp::HObject m_hObject; // 通用 HObject（Region/XLD 等）
    QString m_sourceInfo; // 数据来源信息
    /// 附加测量结果（统一输出：主类型仍是图像/数值时结果不丢）
    QVector<MeasureResult> m_measureResults;
    /// 附加子结果（原多输出工具的第 2..N 个端口，按端口名索引）
    QVector<QPair<QString, QSharedPointer<DataObject>>> m_attachedResults;
    /// 主结果名（"值"/"匹配结果"/"输出"…）
    QString m_primaryName;

public:
    QString sourceInfo() const;
    void setSourceInfo(const QString &sourceInfo);
};

using DataObjectPtr = QSharedPointer<DataObject>;
