#pragma once

#include <QString>

/// 连线语义类型，需与运行时 DataObject::DataType 一致
enum class PortDataType : qint8 {
    Any      = -1, //!< 不设限制（占位/过渡期）
    Unknown  = -2, //!< 未指定，连线规则与 Any 类似
    Image    = 0,
    Region,
    XLD,
    Number,
    String,
    Array,
    Point,      //!< 二维点
    Measure,    //!< 测量结果
    Matrix,     //!< 齐次变换矩阵
    Bool,       //!< 布尔
};

QString portDataTypeLabel(PortDataType t);
