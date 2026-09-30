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
    /// 从方案 JSON 装表（先 clear 整表）。项数口径＝**只认 {6,9}**（U-40 收窄；旧口径"≥6、不猜长度"
    /// 由推进计划 §3.19 拍定，被 §3.44 表 1 的消费面实测取代：这张表的读取者只有恰好 6 项的仿射两个
    /// （坐标系换算／位置修正）与恰好 9 项的内参一个（畸变校正键侧），其余项数进表谁都取不到，
    /// 还占住那个键让 setHomography 的覆盖冲突闸挡住同名重标定）。收窄只作用于**读侧**：写侧在空表上
    /// 仍不设长度闸（HALCON 两个站点直写，长度本机仍未证到，由 §3.19 的前提探针钉住）。
    /// 同时**逐元素要求是有限数值**：元素不是数值／不是有限值 ⇒ 整条条目不进表。理由是 toDouble()
    /// 对字符串、null、bool 一律给 0.0，坏载荷会伪装成"看着合法的全 0 内参"进表，下游照判绿（§3.32）。
    /// rejected 非空时逐条追加 "键 :: 原因"，供加载方把丢弃留痕（不影响装表结果）。
    void fromJson(const QJsonObject &json, QStringList *rejected = nullptr);

    /// 6 元齐次作用于点；项数不是 6（过短或过长）一律不作用于点，原样透传
    static QPointF applyHomography(const QVector<double> &hom, double x, double y);

    /// U-28：两个消费端（坐标系换算／位置修正）**共用**这一份"取不到 6 元仿射"的原因拼装。
    /// 只有 what（哪个算子）与 tail（不许回退成哪组手填参数）是节点侧的，判据与其余文案只写这一遍。
    /// 改前是两处各抄一份（推进计划 §3.34 登记的取证：只改一份时，红只落在读那一侧的腿上半边）。
    static QString affineLookupMissReason(const QString &what, const QString &tail,
                                          const QString &fixtureName, bool hasScene,
                                          bool fixtureNamed, bool fixtureHasHom, int homSize);

private:
    CalibrationManager() = default;
    QMap<QString, QVector<double>> m_homographies;
};
