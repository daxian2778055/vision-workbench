#pragma once

#include <QFile>
#include <QMessageBox>
#include <QString>
#include <QTextStream>
#include <QWidget>

namespace ExportWriteCheck {

/// 打开导出文件；打不开就地弹警告。集中一处的原因：这条提示此前散落多处、口径不一，
/// 仓库的中文重复闸也不允许复制粘贴（2026-10-26）。
inline bool openForExport(QWidget *parent, QFile &file, const QString &path)
{
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
        return true;
    QMessageBox::warning(parent, QObject::tr("导出失败"),
                         QObject::tr("无法写入 %1：%2").arg(path, file.errorString()));
    return false;
}

/// 写完收尾：flush + 查状态 + 关闭；失败弹警告并返回 false。
/// 为什么必须查：open() 成功不代表写成功——磁盘满/网络盘断开在 flush 或 close 时才暴露，
/// 报表/记录用于追溯，静默截断比报错更糟。
inline bool finishTextExport(QWidget *parent, QFile &file, QTextStream &ts, const QString &path)
{
    ts.flush();
    const bool streamOk = ts.status() == QTextStream::Ok;
    const bool flushOk = file.flush();
    file.close();
    // errorString 必须**在** flush/close 之后取（2026-10-26 复核）：磁盘满/网络盘断开时
    // 错误码是在 flush/close 那一刻才置上的，提前取会拿到旧值或空串。
    const QString err = file.errorString();
    if (streamOk && flushOk)
        return true;
    QMessageBox::warning(parent, QObject::tr("导出失败"),
                         QObject::tr("写入未完成 %1：%2").arg(path, err));
    return false;
}

} // namespace ExportWriteCheck
