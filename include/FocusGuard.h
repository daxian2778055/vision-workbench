#pragma once

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

/// 键盘快捷键的焦点守卫（2026-11-08 复核）。
///
/// 主窗口的 Delete 快捷键用的是 QShortcut 默认的 WindowShortcut 上下文——只要窗口
/// 是激活的，任何子控件收到 Delete 都会触发它。于是用户在参数面板的输入框里按
/// Delete 想删一个字符，画布上选中的节点却被删掉了。
///
/// 这类"快捷键劫持文本编辑"的判定集中在这里，header-only 便于单测（不需要构造
/// 整个 MainWindow）。
namespace FocusGuard {

/// 给定控件是否属于「Delete 是文本操作」的那类（输入框 / 文本域 / 数值框 / 下拉框）。
/// 纯判定、不读焦点，便于单测（离屏环境下 setFocus() 不生效）。
inline bool isTextEditingWidget(QWidget *w)
{
    return w
           && (qobject_cast<QLineEdit *>(w) || qobject_cast<QTextEdit *>(w)
               || qobject_cast<QPlainTextEdit *>(w)
               || qobject_cast<QAbstractSpinBox *>(w)
               || qobject_cast<QComboBox *>(w));
}

/// 当前焦点是否落在可编辑控件里。
inline bool isTextEditingFocus()
{
    return isTextEditingWidget(QApplication::focusWidget());
}

}   // namespace FocusGuard
