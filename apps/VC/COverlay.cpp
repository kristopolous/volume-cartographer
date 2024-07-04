// COverlay.hpp
// Philip Allgaier 2024 May
#include "COverlay.hpp"

#include <QPainter>

#include <iostream>

using namespace ChaoVis;

COverlayGraphicsItem::COverlayGraphicsItem(QGraphicsView* graphicsView, OverlaySliceData points, QRect sceneRect, QWidget* parent) : 
    QGraphicsItem(), view(graphicsView), points(points), sceneRect(sceneRect)
{
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);

    prepareGeometryChange();
    intBoundingRect.setTopLeft(QPoint(0, 0));
    intBoundingRect.setBottomRight(QPoint(sceneRect.width(), sceneRect.height()));
}

void COverlayGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    const int pointWidth = 1;

    painter->setPen(pen);
    painter->setBrush(brush);

    for (auto point : points) {

        QPoint center(point.x - sceneRect.x(), point.y - sceneRect.y());
        painter->drawEllipse(center, pointWidth, pointWidth);
    }
}

auto COverlayGraphicsItem::boundingRect() const -> QRectF
{
    return intBoundingRect;
}