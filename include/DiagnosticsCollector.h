#pragma once

#include <QString>

/// 异常信息收集器的输入（全部由调用方采集，纯数据面便于单测，不依赖 HALCON / 主窗口）
struct DiagnosticsInput {
    QString appVersion;      ///< 程序版本整串（含提交短哈希与构建日期）
    QString osVersion;       ///< 操作系统版本（QSysInfo）
    QString qtVersion;       ///< Qt 运行时版本（qVersion()）
    QString halconVersion;   ///< HALCON 版本（GetSystem("version")；空 = 未检测到）
    QString cpuArchitecture; ///< CPU 架构（QSysInfo::currentCpuArchitecture）
    QString appDir;          ///< 程序所在目录
    QString schemePath;      ///< 当前方案路径（空 = 未命名方案）
    QString logTail;         ///< 日志末尾（调用方按 tailLines 裁剪后传入）
    int cpuPercent = -1;     ///< 当前 CPU 占用（-1 = 未获取）
    int memoryPercent = -1;  ///< 当前内存占用（-1 = 未获取）
    int diskPercent = -1;    ///< 当前方案卷占用（-1 = 未获取）
};

/// 异常信息收集器：把版本 / 运行环境 / 资源占用 / 最近日志整理为纯文本报告，
/// 供用户导出后发给技术支持（不包含图像、模型或方案内容）。
class DiagnosticsCollector final {
public:
    /// 日志裁剪：只保留末尾 maxLines 行，且总字符不超过 maxChars（超长从头部截断）。
    /// 统一把 CRLF / CR 归一为 LF；maxLines/maxChars 小于 1 时按 1 处理。
    static QString tailLines(const QString &text, int maxLines, int maxChars);

    /// 生成可读的纯文本报告（含生成时间；缺失字段标为「未获取」）
    static QString buildReport(const DiagnosticsInput &input);

    /// 保存报告文本（UTF-8；无扩展名自动补 .txt）。
    /// 失败返回 false 且 error 含具体上下文（空路径 / 报告为空 / 目录不存在 / 写入失败）。
    static bool writeReport(const QString &filePath, const QString &report,
                            QString *error = nullptr);
};
