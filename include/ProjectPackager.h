#pragma once

#include <QString>
#include <QStringList>

class FlowScene;

/// 把方案的"外部依赖"（模型/类别表等文件路径参数）收进方案目录（2026-10-28）。
///
/// 为什么需要：YOLO 的 onnx 与 classes.txt 默认放在方案目录之外，换台机器把方案拷过去，
/// 节点就找不到模型——现场最常见的"方案带不动"。
///
/// 为什么做成**显式动作**而不是保存时自动搬：改用户参数是侵入式行为，保存这种高频操作
/// 不该顺带挪文件。这里只在用户点"打包依赖"时执行，并在结果里如实报告搬了什么。
class ProjectPackager
{
public:
    struct Result {
        bool ok = false;
        QString error;
        QStringList copied;     /// 已复制进方案的依赖（相对路径）
        QStringList skipped;    /// 已在方案内/不是文件，无需处理
    };

    /// schemeFilePath：方案的 .vfp 文件路径（用它的目录作为方案根）；
    /// 依赖统一放进 <schemeDir>/assets/，同名不同源自动加 _2/_3 后缀。
    static Result pack(FlowScene *scene, const QString &schemeFilePath);
};
