#pragma once

#include <QMetaObject>
#include <QObject>

#include <functional>

#include "ProjectManager.h"

/// U-34：把「载入期降级／被拒留痕」接到可见面的那一句接线，只写这一遍。
///
/// 改前的形状（推进计划 §3.32 登记未修第 2 条 · §3.37 表 4 第 2 条，本轮实测读数
/// [U34-PRE] ownSignals=0 debugLines=2 hits=2）：那几段留痕只进 VFP_DEBUG（qCDebug），
/// 而进程装的消息处理器把 QtDebugMsg 只写崩溃日志文件、连 stderr 都不转发
/// （src/main.cpp:96）⇒ 操作员看到的是"方案打开后标定结果／夹具的矩阵不见了"，没有下一句解释。
///
/// 为什么单列一处：MainWindow 不能在 CI 里实例化，接线若写在窗口代码里就没有任何运行期腿盯得住它
/// （U-18 的口径谓词、W-1 的取实参是同族）。这里把接线做成一个函数，产线与门禁腿调**同一份**：
/// 撤掉下面的 connect，腿立刻红。
namespace ProjectLoadNotes {

using Sink = std::function<void(const QString &)>;

/// 逐句投递给 show（MainWindow 传的是 logMessage）。context 取 pm：方案管理器析构即断开。
inline QMetaObject::Connection attachTo(ProjectManager *pm, const Sink &show)
{
    return QObject::connect(pm, &ProjectManager::loadNote, pm,
                            [show](const QString &note) { show(note); });
}

}   // namespace ProjectLoadNotes
