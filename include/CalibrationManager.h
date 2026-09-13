#pragma once

#include <QString>
#include <QMap>
#include <QVector>
#include <QStringList>
#include <QJsonObject>
#include <QPointF>

/// 标定数据管理单例：集中存储命名齐次变换矩阵（N点标定/手眼标定结果），
/// 对标 VisionMaster 的标定模块，供坐标系换算算子复用。
class CalibrationManager
{
public:
    static CalibrationManager *instance();

    /// 保存命名齐次矩阵（6 元素: m11 m12 m13 m21 m22 m23）
    void setHomography(const QString &name, const QVector<double> &hom);
    QVector<double> homography(const QString &name) const;
    bool hasHomography(const QString &name) const;
    QStringList names() const;
    void remove(const QString &name);
    void clear();

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);

    /// 6 元齐次作用于点
    static QPointF applyHomography(const QVector<double> &hom, double x, double y);

private:
    CalibrationManager() = default;
    QMap<QString, QVector<double>> m_homographies;
};
