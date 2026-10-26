#pragma once

#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QWidget>

/// 破坏性操作的确认框（2026-10-26）：**默认键显式落"取消"**，回车不会误删。
/// 集中一处的原因：两个对话框此前各抄了一份完全相同的实现（中文重复闸也不允许复制）。
/// 注：`QMessageBox::question` 的静态重载**本来就有** defaultButton 形参（默认 NoButton），
/// 原调用只是没传——早先注释写成"静态不能指定默认键"是错的，这里保留正确说法。
inline bool confirmDestructive(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(title);
    box.setText(text);
    QPushButton *yes = box.addButton(QObject::tr("确定"), QMessageBox::YesRole);
    QPushButton *no = box.addButton(QObject::tr("取消"), QMessageBox::NoRole);
    box.setDefaultButton(no);
    box.exec();
    return box.clickedButton() == yes;
}
