#include "FlowCanvasView.h"

#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolButton>
#include <QWheelEvent>
#include <cmath>

namespace {

/// 鹰眼/适应窗口共用的"整图范围"：优先取图元包围盒，空场景回落到 sceneRect。
QRectF flowBounds(QGraphicsScene *scene)
{
    if (!scene)
        return QRectF();
    const QRectF itemsRect = scene->itemsBoundingRect();
    if (!itemsRect.isNull() && itemsRect.width() > 0.0 && itemsRect.height() > 0.0)
        return itemsRect;
    return scene->sceneRect();
}

} // namespace

/// 鹰眼缩略图：把整张流程缩略绘制，并用橙色框标出主视图当前可见区域。
/// 点击/拖动即把主视图中心移到对应场景位置（只做视图定位，不改场景内容）。
class FlowEagleEye : public QWidget
{
public:
    explicit FlowEagleEye(FlowCanvasView *view)
        : QWidget(view), m_view(view)
    {
        setFixedSize(180, 120);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("鹰眼：点击或拖动定位主视图"));
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, 200));

        QGraphicsScene *scene = m_view->scene();
        const QRectF bounds = flowBounds(scene);
        if (!scene || bounds.isNull() || bounds.width() <= 0.0 || bounds.height() <= 0.0) {
            painter.setPen(QColor(0xb0, 0xb0, 0xb0));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("鹰眼"));
            return;
        }

        const QRectF target = QRectF(rect()).adjusted(2, 2, -2, -2);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        scene->render(&painter, target, bounds, Qt::KeepAspectRatio);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

        // 当前视口在缩略图中的位置（按 KeepAspectRatio 的等比缩放计算）
        const double scale = qMin(target.width() / bounds.width(), target.height() / bounds.height());
        const double offsetX = target.left() + (target.width() - bounds.width() * scale) * 0.5;
        const double offsetY = target.top() + (target.height() - bounds.height() * scale) * 0.5;
        const QRectF visible = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
        QRectF visibleRect(offsetX + (visible.left() - bounds.left()) * scale,
                           offsetY + (visible.top() - bounds.top()) * scale,
                           visible.width() * scale, visible.height() * scale);
        visibleRect = visibleRect.intersected(target);
        painter.setPen(QPen(QColor(0xff, 0xa0, 0x2a), 1.5));
        painter.drawRect(visibleRect);

        painter.setPen(QPen(QColor(0x4a, 0x6a, 0x9c), 1));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        centerOnWidgetPos(event->pos());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton)
            centerOnWidgetPos(event->pos());
        event->accept();
    }

private:
    void centerOnWidgetPos(const QPoint &pos)
    {
        QGraphicsScene *scene = m_view->scene();
        const QRectF bounds = flowBounds(scene);
        if (!scene || bounds.isNull() || bounds.width() <= 0.0 || bounds.height() <= 0.0)
            return;
        const QRectF target = QRectF(rect()).adjusted(2, 2, -2, -2);
        const double scale = qMin(target.width() / bounds.width(), target.height() / bounds.height());
        if (scale <= 0.0)
            return;
        const double offsetX = target.left() + (target.width() - bounds.width() * scale) * 0.5;
        const double offsetY = target.top() + (target.height() - bounds.height() * scale) * 0.5;
        const QPointF scenePos(bounds.left() + (pos.x() - offsetX) / scale,
                               bounds.top() + (pos.y() - offsetY) / scale);
        m_view->centerOn(scenePos);
    }

    FlowCanvasView *m_view = nullptr;
};

FlowCanvasView::FlowCanvasView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::RubberBandDrag);
    setRubberBandSelectionMode(Qt::IntersectsItemShape);
    setFocusPolicy(Qt::StrongFocus);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);

    buildControls();

    m_eagle = new FlowEagleEye(this);
    m_eagle->hide();
    // VM 4.4 默认就显示右下角鹰眼缩略图（空场景也保留一块缩略图区域）；
    // 用户仍可通过「鹰眼」按钮隐藏。
    m_eagleBtn->setChecked(true);
    setEagleEyeVisible(true);

    // 视图滚动/缩放后鹰眼方框跟随
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        if (m_eagle)
            m_eagle->update();
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        if (m_eagle)
            m_eagle->update();
    });
}

void FlowCanvasView::buildControls()
{
    m_controls = new QWidget(this);
    m_controls->setObjectName(QStringLiteral("canvasZoomControls"));
    m_controls->setStyleSheet(QStringLiteral(
        "QWidget#canvasZoomControls { background-color: rgba(30,30,33,210);"
        " border: 1px solid #4a6a9c; border-radius: 4px; }"
        "QToolButton { color: #e0e0e0; border: none; padding: 2px 6px; min-width: 20px; }"
        "QToolButton:hover { background-color: #3a3a55; }"
        "QToolButton:checked { color: #ffa02a; }"
        "QLabel { color: #c0c0c0; }"));

    auto *layout = new QHBoxLayout(m_controls);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);

    QToolButton *zoomOutBtn = new QToolButton(m_controls);
    zoomOutBtn->setText(QStringLiteral("−"));
    zoomOutBtn->setToolTip(QStringLiteral("缩小"));
    layout->addWidget(zoomOutBtn);
    connect(zoomOutBtn, &QToolButton::clicked, this, [this]() { zoomBy(0.8); });

    m_zoomLabel = new QLabel(QStringLiteral("100%"), m_controls);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(44);
    layout->addWidget(m_zoomLabel);

    QToolButton *zoomInBtn = new QToolButton(m_controls);
    zoomInBtn->setText(QStringLiteral("＋"));
    zoomInBtn->setToolTip(QStringLiteral("放大"));
    layout->addWidget(zoomInBtn);
    connect(zoomInBtn, &QToolButton::clicked, this, [this]() { zoomBy(1.25); });

    QToolButton *fitBtn = new QToolButton(m_controls);
    fitBtn->setText(QStringLiteral("适应"));
    fitBtn->setToolTip(QStringLiteral("适应窗口"));
    layout->addWidget(fitBtn);
    connect(fitBtn, &QToolButton::clicked, this, [this]() { fitToFlow(); });

    m_eagleBtn = new QToolButton(m_controls);
    m_eagleBtn->setText(QStringLiteral("鹰眼"));
    m_eagleBtn->setToolTip(QStringLiteral("显示/隐藏鹰眼缩略图"));
    m_eagleBtn->setCheckable(true);
    layout->addWidget(m_eagleBtn);
    connect(m_eagleBtn, &QToolButton::toggled, this, [this](bool on) {
        setEagleEyeVisible(on);
    });

    m_controls->adjustSize();

    // P2-4：耗时角标（固定在右下角缩放条上方）
    m_timingLabel = new QLabel(this);
    m_timingLabel->setObjectName(QStringLiteral("canvasTimingLabel"));
    m_timingLabel->setStyleSheet(QStringLiteral(
        "QLabel#canvasTimingLabel { background-color: rgba(30,30,33,200);"
        " color: #d8dee9; border: 1px solid #3a4354; border-radius: 4px;"
        " padding: 3px 8px; font-size: 12px; }"));
    m_timingLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    refreshTimingLabel();
    updateControlsPosition();
}

void FlowCanvasView::refreshTimingLabel()
{
    if (!m_timingLabel)
        return;
    QString text = m_flowElapsedMs >= 0
                       ? QStringLiteral("流程耗时 %1 ms").arg(m_flowElapsedMs)
                       : QStringLiteral("流程耗时 --");
    if (!m_selectedNodeName.isEmpty() && m_selectedNodeElapsedMs >= 0) {
        text += QStringLiteral("\n%1  %2 ms").arg(m_selectedNodeName).arg(m_selectedNodeElapsedMs);
    } else {
        text += QStringLiteral("\n选中模块 --");
    }
    m_timingLabel->setText(text);
    m_timingLabel->adjustSize();
}

void FlowCanvasView::setFlowElapsedMs(qint64 elapsedMs)
{
    m_flowElapsedMs = elapsedMs;
    refreshTimingLabel();
    updateControlsPosition();
}

void FlowCanvasView::setSelectedNodeElapsed(const QString &nodeName, qint64 elapsedMs)
{
    m_selectedNodeName = nodeName;
    m_selectedNodeElapsedMs = elapsedMs;
    refreshTimingLabel();
    updateControlsPosition();
}

void FlowCanvasView::clearSelectedNodeElapsed()
{
    m_selectedNodeName.clear();
    m_selectedNodeElapsedMs = -1;
    refreshTimingLabel();
    updateControlsPosition();
}

void FlowCanvasView::updateControlsPosition()
{
    if (!m_controls)
        return;
    m_controls->adjustSize();
    const int margin = 12;
    const int x = width() - m_controls->width() - margin;
    const int y = height() - m_controls->height() - margin;
    m_controls->move(qMax(0, x), qMax(0, y));

    int aboveY = y;
    if (m_timingLabel) {
        m_timingLabel->adjustSize();
        aboveY = y - m_timingLabel->height() - 6;
        m_timingLabel->move(qMax(0, width() - m_timingLabel->width() - margin),
                            qMax(0, aboveY));
    }

    if (m_eagle) {
        m_eagle->move(qMax(0, width() - m_eagle->width() - margin),
                      qMax(0, aboveY - m_eagle->height() - 6));
    }
}

void FlowCanvasView::refreshZoomLabel()
{
    const int percent = zoomPercent();
    if (m_zoomLabel)
        m_zoomLabel->setText(QStringLiteral("%1%").arg(percent));
    emit zoomPercentChanged(percent);
}

int FlowCanvasView::zoomPercent() const
{
    return qRound(transform().m11() * 100.0);
}

void FlowCanvasView::setZoomPercent(int percent)
{
    const int clamped = qBound(10, percent, 800);
    const double current = transform().m11();
    if (current <= 0.0)
        return;
    const double factor = (clamped / 100.0) / current;
    if (qAbs(factor - 1.0) < 0.001)
        return;
    scale(factor, factor);
    refreshZoomLabel();
    if (m_eagle)
        m_eagle->update();
}

void FlowCanvasView::zoomBy(double factor)
{
    if (factor <= 0.0)
        return;
    const int target = qRound(zoomPercent() * factor);
    setZoomPercent(target);
}

void FlowCanvasView::fitToFlow()
{
    QGraphicsScene *sc = scene();
    const QRectF bounds = flowBounds(sc);
    if (bounds.isNull())
        return;
    QRectF padded = bounds.adjusted(-40.0, -40.0, 40.0, 40.0);
    if (padded.width() < 1.0 || padded.height() < 1.0)
        return;
    fitInView(padded, Qt::KeepAspectRatio);
    const int percent = zoomPercent();
    if (percent > 800)
        setZoomPercent(800);
    refreshZoomLabel();
    if (m_eagle)
        m_eagle->update();
}

void FlowCanvasView::setEagleEyeVisible(bool on)
{
    if (!m_eagle)
        return;
    m_eagle->setVisible(on);
    if (m_eagleBtn && m_eagleBtn->isChecked() != on) {
        QSignalBlocker blocker(m_eagleBtn);
        m_eagleBtn->setChecked(on);
    }
    if (on)
        m_eagle->update();
    updateControlsPosition();
}

bool FlowCanvasView::isEagleEyeVisible() const
{
    return m_eagle && m_eagle->isVisible();
}

void FlowCanvasView::wheelEvent(QWheelEvent *event)
{
    // Ctrl + 滚轮 = 缩放（VM 画布习惯）；未按 Ctrl 时保留原生滚动。
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta != 0)
            zoomBy(delta > 0 ? 1.15 : 1.0 / 1.15);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void FlowCanvasView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    updateControlsPosition();
    if (m_eagle)
        m_eagle->update();
}

void FlowCanvasView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    if (m_eagle)
        m_eagle->update();
}
