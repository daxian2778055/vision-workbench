#pragma once

// 本地绘制图标的设备像素比画布（AE2）。
// 仓库曾修过 BrandHeader 的高分屏 logo 模糊：根因是直接按逻辑像素建 QPixmap，
// DPR=2 时 16px 图标只有 16 个物理像素，放大后必然糊。这里把同一做法收口，
// 让所有 QPainter 本地绘制的图标先按 DPR 放大物理画布，再 scale 回逻辑坐标，
// 于是原有所有 QRectF/QPen 宽度等几何代码无需改动，QIcon 的逻辑尺寸也保持不变。

#include <QCoreApplication>
#include <QGuiApplication>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <cmath>

namespace DprIcon {

class Canvas
{
public:
    explicit Canvas(const QSize &logical)
    {
        qreal dpr = 1.0;
        // instance() 的静态返回类型是 QCoreApplication*，需显式转成 QGuiApplication*
        QGuiApplication *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
        if (gui)
            dpr = gui->devicePixelRatio();
        if (!(dpr > 0.0) || !std::isfinite(dpr))
            dpr = 1.0;
        m_dpr = dpr;
        m_pixmap = QPixmap(qMax(1, qRound(logical.width() * m_dpr)),
                           qMax(1, qRound(logical.height() * m_dpr)));
        m_pixmap.fill(Qt::transparent);
        m_painter = new QPainter(&m_pixmap);
        m_painter->scale(m_dpr, m_dpr);
        m_painter->setRenderHint(QPainter::Antialiasing, true);
    }

    ~Canvas() { delete m_painter; }

    QPainter &painter() { return *m_painter; }

    QPixmap finish()
    {
        m_painter->end();
        m_pixmap.setDevicePixelRatio(m_dpr);
        return m_pixmap;
    }

private:
    qreal m_dpr = 1.0;
    QPixmap m_pixmap;
    QPainter *m_painter = nullptr;
};

} // namespace DprIcon
