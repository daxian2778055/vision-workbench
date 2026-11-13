#pragma once

#include <QPushButton>
#include <QColor>
#include <QPainter>

/// VM4.4 的小滑块开关（通信设备列表、内联参数、创建弹窗共用，2026-11-13）。
///
/// 故意仍是 checkable QPushButton：既有回归用 qobject_cast<QPushButton*> 定位
/// cellWidget(row, 2) 并做 QTest::mouseClick，换成自定义 widget 会打断那条真实点击路径。
/// header-only 且不带 Q_OBJECT —— AUTOMOC 不需要为它生成 moc 文件。
class VmSwitch : public QPushButton
{
public:
    explicit VmSwitch(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setCheckable(true);
        setFixedSize(44, 24);
        setText(QString());
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const bool on = isChecked();
        p.setPen(Qt::NoPen);
        p.setBrush(on ? QColor(QStringLiteral("#f26b21"))
                      : QColor(QStringLiteral("#d8d8d8")));
        p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 11, 11);
        p.setBrush(Qt::white);
        const int d = 18;
        const int x = on ? width() - d - 3 : 3;
        p.drawEllipse(x, (height() - d) / 2, d, d);
    }
};
