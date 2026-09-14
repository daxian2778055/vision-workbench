#pragma once

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(vfpCat)
Q_DECLARE_LOGGING_CATEGORY(vfpExecCat)

/// 可调级别：运行时 QT_LOGGING_RULES="visionflow.platform.debug=false" 关闭冗余日志
#define VFP_DEBUG qCDebug(vfpCat)

/// 流程执行器逐节点/拓扑类日志（大图易刷屏）；可单独关闭：
/// QT_LOGGING_RULES="visionflow.platform.executor.debug=false" 或对两者同时设为 false
#define VFP_EXEC_DEBUG qCDebug(vfpExecCat)

/// 运行期统计摘要（轮次/耗时/失败/句柄/内存）：现场长跑诊断用，默认每分钟一条，
/// 可用 FlowExecutor::setStatsLogIntervalMs(0) 关闭；
/// 静默：QT_LOGGING_RULES="visionflow.platform.executor.info=false"
#define VFP_RUNTIME_INFO qCInfo(vfpExecCat)
