#include "CommentGraphicsItem.h"
#include "FlowScene.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QStyle>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>

CommentGraphicsItem::CommentGraphicsItem(const QString &text, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_text(text.isEmpty() ? QStringLiteral("注释") : text)
{
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    setZValue(-1);
}

QRectF CommentGraphicsItem::boundingRect() const
{
    return m_rect.adjusted(-2, -2, 2, 2);
}

void CommentGraphicsItem::setText(const QString &text)
{
    m_text = text;
    update();
}

void CommentGraphicsItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(QColor(0x3d, 0x3a, 0x22));
    painter->setPen(QPen(QColor(0xd4, 0xc4, 0x6a), 1));
    painter->drawRoundedRect(m_rect, 4, 4);

    QFont font = painter->font();
    font.setFamilies({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI")});
    font.setPointSizeF(9.5);
    painter->setFont(font);
    painter->setPen(QColor(0xf2, 0xe8, 0xb0));
    painter->drawText(m_rect.adjusted(8, 6, -8, -6),
                      Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_text);

    if (option && (option->state & QStyle::State_Selected)) {
        painter->setPen(QPen(QColor(0x40, 0xa9, 0xff), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(m_rect.adjusted(-1, -1, 1, 1), 4, 4);
    }
}

QJsonObject CommentGraphicsItem::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("x")] = pos().x();
    o[QStringLiteral("y")] = pos().y();
    o[QStringLiteral("w")] = m_rect.width();
    o[QStringLiteral("h")] = m_rect.height();
    o[QStringLiteral("text")] = m_text;
    return o;
}

CommentGraphicsItem *CommentGraphicsItem::fromJson(const QJsonObject &json)
{
    auto *item = new CommentGraphicsItem(json.value(QStringLiteral("text")).toString());
    item->setPos(json.value(QStringLiteral("x")).toDouble(),
                 json.value(QStringLiteral("y")).toDouble());
    const double w = json.value(QStringLiteral("w")).toDouble(180);
    const double h = json.value(QStringLiteral("h")).toDouble(72);
    if (w > 20 && h > 20)
        item->m_rect = QRectF(0, 0, w, h);
    return item;
}

void CommentGraphicsItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsItem::mouseDoubleClickEvent(event);
    if (auto *scene = dynamic_cast<FlowScene *>(this->scene())) {
        if (scene->isEditLocked())
            return;
        scene->recordUndo();
    }
    bool ok = false;
    const QString t = QInputDialog::getMultiLineText(
        nullptr, QStringLiteral("编辑注释"), QStringLiteral("内容:"), m_text, &ok);
    if (ok)
        setText(t);
}

void CommentGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    m_pressPos = pos();
    QGraphicsItem::mousePressEvent(event);
}

void CommentGraphicsItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsItem::mouseReleaseEvent(event);
    if ((pos() - m_pressPos).manhattanLength() > 1.0) {
        if (auto *scene = dynamic_cast<FlowScene *>(this->scene()))
            scene->recordUndo();
    }
}

void CommentGraphicsItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    auto *scene = dynamic_cast<FlowScene *>(this->scene());
    if (!scene)
        return;
    QMenu menu;
    QAction *edit = menu.addAction(QStringLiteral("编辑文字…"));
    QAction *del = menu.addAction(QStringLiteral("删除注释"));
    edit->setEnabled(!scene->isEditLocked());
    del->setEnabled(!scene->isEditLocked());
    QAction *sel = menu.exec(event->screenPos());
    if (sel == edit && !scene->isEditLocked()) {
        scene->recordUndo();
        bool ok = false;
        const QString t = QInputDialog::getMultiLineText(
            nullptr, QStringLiteral("编辑注释"), QStringLiteral("内容:"), m_text, &ok);
        if (ok)
            setText(t);
    } else if (sel == del && !scene->isEditLocked()) {
        scene->removeComment(this);
    }
}
