#pragma once

#include <QString>

/// CSV 字段统一转义：列结构（逗号/引号/换行/回车）＋公式注入（= + - @ 与制表/回车开头）。
/// 中文 Windows 的 Excel/WPS 打开导出的 CSV 会把以 = + - @ 开头的单元格当公式执行
/// （DDE / 外部链接风险）；算子名与流程名都是用户可改的现场输入，必须先置成文本。
/// 先前置单引号，再做标准双引号包裹，两处导出（性能面板/报表）共用同一实现。
inline QString csvField(const QString &s)
{
    QString v = s;
    if (!v.isEmpty()) {
        const QChar first = v.at(0);
        if (first == QLatin1Char('=') || first == QLatin1Char('+')
            || first == QLatin1Char('-') || first == QLatin1Char('@')
            || first == QLatin1Char('\t') || first == QLatin1Char('\r')) {
            v.prepend(QLatin1Char('\''));
        }
    }
    if (v.contains(QLatin1Char('"')) || v.contains(QLatin1Char(','))
        || v.contains(QLatin1Char('\n')) || v.contains(QLatin1Char('\r'))) {
        v.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        v = QLatin1Char('"') + v + QLatin1Char('"');
    }
    return v;
}
