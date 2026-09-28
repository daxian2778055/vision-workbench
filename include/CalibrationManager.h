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

    /// 保存命名载荷。同一张表里住着两种载荷，项数就是它们的种类标识：
    /// 6 元＝仿射/齐次（m11 m12 m13 m21 m22 m23），9 元＝OpenCV 内参（fx fy cx cy k1 k2 p1 p2 rms）。
    /// 返回 false 的两种情形：名字为空；该键上已有一种载荷、而新载荷**项数与它不同**
    /// （不同项数＝另一种载荷，同名覆盖会把那一条整条销毁，R-5）。同项数才允许覆盖。
    bool setHomography(const QString &name, const QVector<double> &hom);
    QVector<double> homography(const QString &name) const;
    bool hasHomography(const QString &name) const;
    QStringList names() const;
    void remove(const QString &name);
    void clear();

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);

    /// 6 元齐次作用于点；项数不是 6（过短或过长）一律不作用于点，原样透传
    static QPointF applyHomography(const QVector<double> &hom, double x, double y);

private:
    CalibrationManager() = default;
    QMap<QString, QVector<double>> m_homographies;
};
