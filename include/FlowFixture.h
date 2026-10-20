#pragma once

#include <QString>
#include <QVector>

/// 命名夹具：6 元齐次 + 可选匹配位姿
/// （切片 B 夹具族首抽自场景头件拆出——轻头件，族面与消费侧共用同一类型定义）
struct FlowFixture {
    QString name;
    QVector<double> hom; /// m11 m12 m13 m21 m22 m23
    double poseRow = 0.0;
    double poseCol = 0.0;
    double poseAngle = 0.0;
    double poseScale = 1.0;
    bool hasHom = false;
    bool hasPose = false;
};
