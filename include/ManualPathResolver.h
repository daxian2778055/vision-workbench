#pragma once

#include <QStringList>

/// 手册路径解析器（词条切片 B 手册解析族首抽，从 MainWindow::openManualDialog 抽出）：
/// 「候选链四式恒序 + 首个存在且可读者胜出 + 嵌入资源兜底」两步走。
///
/// 契约钉（与原体逐行同语义）：
///  · 候选链纯装配不触文件系统；优先级序是判定本体（exe 同目录 docs/ 现场可替换 >
///    仓库 docs/（相对进程工作目录）> exe 同目录根 > 嵌入资源 qrc 兜底）；
///  · 首读者胜出：沿链依序，首个「存在且可打开（只读文本形）」者取 UTF-8 全文即回，
///    后位不再试（原体 break 同形）；全者不可读返回空串（空判据与提示弹框留窗侧）；
///  · 目录形与不可打开形照样跳（原体 exists 且 open 两判同瞬）。
class ManualPathResolver final {
public:
    /// 候选链装配（优先级序恒定；字符串拼接纯函数，无文件系统触碰）。
    static QStringList candidatePaths(const QString &exeDir);

    /// 沿链找首个可读者取全文；全空返回空串。资源路径（qrc）同径处理（QFile 对 qrc 亦可）。
    static QString resolveMarkdown(const QStringList &paths);
};
