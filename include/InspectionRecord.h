#pragma once

#include <QString>
#include <QDateTime>

/// 检测结果记录（"某流程的某算子跑出一轮结果"的最小描述）。
///
/// 为什么单独放一个**瘦**头文件：
/// FlowExecutor 需要按值持有本结构（每轮缓冲 m_pendingResults、轮末整批落库），
/// 而它原先必须 #include "AppDatabase.h" 才能拿到完整类型——那会把 <QSqlDatabase> 等
/// QtSql 依赖带进执行器的头文件，于是"改任何一个核心头"都会触发大规模重编
/// （本地实测 369s、runner 上要二十多分钟）。本文件只依赖 QString/QDateTime，
/// 数据库侧与执行器侧各自包含即可。
struct InspectionRecord {
    int id = 0;
    QString flowName;
    QString nodeName;
    bool passed = false;
    QString value;
    QDateTime timestamp;
};
