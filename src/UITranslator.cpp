#include "UITranslator.h"

// UI 文本表应用器（词条切片 B 就地链首抽）：
//  · 应用序即注册序——序偶性是行为保真的观测位（原两分支逐行同序）；
//  · 语言分派一跳（原 if/else 两分支同目标，分支只剩文本两套）；
//  · 空回调跳过（目标死活的 guard 留在调用方的回调体内，与原逐行 if 同瞬）。

void UITranslator::add(Setter setter, const QString &zhText, const QString &enText)
{
    m_entries.append(Entry(std::move(setter), zhText, enText));
}

void UITranslator::clear()
{
    m_entries.clear();
}

void UITranslator::apply(Lang lang) const
{
    for (const Entry &entry : m_entries) {
        if (!entry.apply)
            continue;
        entry.apply(lang == Lang::Zh ? entry.zh : entry.en);
    }
}
