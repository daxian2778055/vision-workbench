#pragma once

#include <QVector>
#include <functional>
#include <QString>

/// UI 文本表应用器（词条切片 B 就地链首抽，从 MainWindow::retranslateUi 抽出）：
/// 双语文本成对注册、按语言一跳取文应用——表里只存「设文回调 + 两文」，判定只有语言分派一处。
///
/// 契约钉（与原 retranslateUi 两分支的逐行 setText 同序等瞬）：
///  · 应用序 = 注册序（原中文/英文两分支同目标同序，序偶性即行为保真的观测位）；
///  · 设文回调内的目标指针在应用时才 deref（与原逐行 if 直达同瞬），GUI 不进本面；
///  · 两文同文允许（别名对）、空回调跳过（原 guard 形在回调体内，本面不管目标死活）。
class UITranslator final {
public:
    enum class Lang { Zh, En };
    using Setter = std::function<void(const QString &)>;

    struct Entry {
        Setter apply;
        QString zh;
        QString en;
        Entry(Setter setter, QString zhText, QString enText)
            : apply(std::move(setter)), zh(std::move(zhText)), en(std::move(enText))
        {
        }
    };

    /// 追加一对文本（顺序敏感）。
    void add(Setter setter, const QString &zhText, const QString &enText);

    /// 清空重填（原实现每次调用即全场重设，本面同拍）。
    void clear();

    /// 按语言一跳取文并依注册序逐条应用；空回调跳过，空表无事。
    void apply(Lang lang) const;

private:
    QVector<Entry> m_entries;
};
