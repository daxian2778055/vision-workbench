#pragma once

#include <QGraphicsItem>
#include <QString>
#include <QJsonObject>

/// 画布注释（文本框），随 .vfp 流程保存
class CommentGraphicsItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 21 };
    int type() const override { return Type; }

    explicit CommentGraphicsItem(const QString &text = QString(), QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    QString text() const { return m_text; }
    void setText(const QString &text);

    QJsonObject toJson() const;
    static CommentGraphicsItem *fromJson(const QJsonObject &json);

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;

private:
    QString m_text;
    QRectF m_rect {0, 0, 180, 72};
    QPointF m_pressPos;
};
