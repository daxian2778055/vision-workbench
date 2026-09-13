#pragma once

#include <QString>
#include <QVariant>
#include <QList>
#include <QStringList>

/// 参数类型
enum class ParamType {
    Int,        /// 整数 (QSpinBox)
    Double,     /// 浮点 (QDoubleSpinBox)
    Bool,       /// 布尔 (QCheckBox)
    String,     /// 字符串 (QLineEdit)
    Enum,       /// 枚举 (QComboBox)，取值存为 int 索引
    FilePath,   /// 文件路径 (QLineEdit + 浏览按钮)
    Point,      /// 二维点 (x,y)
    Rect,       /// 矩形 (x,y,w,h)
    MultiLine,  /// 多行文本 (QPlainTextEdit)
};

/// 参数描述：声明式定义算子的参数，用于自动生成参数面板
struct ParamSpec {
    QString name;             /// 参数名（驼峰，序列化键）
    ParamType type = ParamType::Double;
    QVariant defaultValue;    /// 默认值
    QVariant minValue;        /// 范围下界（Int/Double 用）
    QVariant maxValue;        /// 范围上界
    bool hasRange = false;
    QStringList enumValues;   /// Enum 的候选（显示文本）
    QString unit;             /// 单位（如 ms、px）
    QString label;            /// 显示标签（中文，缺省用 name）
    QString group;            /// 参数分组（面板内分组标题）
    QString tooltip;
};

using ParamSpecList = QList<ParamSpec>;

inline ParamSpec makeIntParam(const QString &name, int def, int minV, int maxV,
                              const QString &label, const QString &unit = QString())
{
    ParamSpec s;
    s.name = name; s.type = ParamType::Int; s.defaultValue = def;
    s.minValue = minV; s.maxValue = maxV; s.hasRange = true;
    s.label = label; s.unit = unit;
    return s;
}

inline ParamSpec makeDoubleParam(const QString &name, double def, double minV, double maxV,
                                 const QString &label, const QString &unit = QString())
{
    ParamSpec s;
    s.name = name; s.type = ParamType::Double; s.defaultValue = def;
    s.minValue = minV; s.maxValue = maxV; s.hasRange = true;
    s.label = label; s.unit = unit;
    return s;
}

inline ParamSpec makeBoolParam(const QString &name, bool def,
                               const QString &label)
{
    ParamSpec s;
    s.name = name; s.type = ParamType::Bool; s.defaultValue = def;
    s.label = label;
    return s;
}

inline ParamSpec makeStringParam(const QString &name, const QString &def,
                                 const QString &label)
{
    ParamSpec s;
    s.name = name; s.type = ParamType::String; s.defaultValue = def;
    s.label = label;
    return s;
}

/// 文件路径参数（自动面板生成 QLineEdit + 浏览按钮）
inline ParamSpec makeFilePathParam(const QString &name, const QString &def,
                                   const QString &label)
{
    ParamSpec s;
    s.name = name; s.type = ParamType::FilePath; s.defaultValue = def;
    s.label = label;
    return s;
}

inline ParamSpec makeEnumParam(const QString &name, int def,
                               const QStringList &values,
                               const QString &label)
{
    ParamSpec s;
    s.name = name; s.type = ParamType::Enum; s.defaultValue = def;
    s.enumValues = values; s.label = label;
    return s;
}
