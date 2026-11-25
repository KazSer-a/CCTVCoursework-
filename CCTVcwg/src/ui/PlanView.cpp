#include "PlanView.h"

#include <QBrush>
#include <QColor>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include <cstddef>
#include <vector>

namespace
{
// Переводит координаты плана в координаты графической сцены.
QPointF toScenePoint(const PlanPoint& point)
{
    return {point.xMeters, -point.yMeters};
}

// Добавляет замкнутый контур к графическому пути.
void appendRing(QPainterPath& path, const std::vector<PlanPoint>& ring)
{
    if (ring.size() < 3)
        return;

    path.moveTo(toScenePoint(ring.front()));

    for (std::size_t i = 1; i < ring.size(); ++i)
        path.lineTo(toScenePoint(ring[i]));

    path.closeSubpath();
}
}

// Настраивает сцену, отслеживание курсора и начальный масштаб.
PlanView::PlanView(QWidget* parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    viewport()->setMouseTracking(true);

    setSceneRect(-10.0, -30.0, 40.0, 40.0);
    setBackgroundBrush(QColor(248, 249, 250));
    setRenderHint(QPainter::Antialiasing);

    scale(12.0, 12.0);
    centerOn(10.0, -10.0);
}

// Отображает каждую часть здания с учётом внутренних контуров.
void PlanView::drawBuilding(const Building& building)
{
    QPen outline(QColor(64, 72, 82));
    outline.setCosmetic(true);

    const QBrush fill(QColor(185, 194, 204));

    for (const BuildingFootprint& footprint : building.footprints)
    {
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);

        appendRing(path, footprint.outerRing);

        for (const auto& innerRing : footprint.innerRings)
            appendRing(path, innerRing);

        scene()->addPath(path, outline, fill);
    }
}

// Размещает маркер цели поверх контуров зданий.
void PlanView::drawObservationPoint(const PlanPoint& position)
{
    constexpr double radiusPixels = 4.0;

    auto* marker = scene()->addEllipse(
        -radiusPixels, -radiusPixels,
        2.0 * radiusPixels, 2.0 * radiusPixels,
        QPen(Qt::NoPen), QBrush(QColor(196, 45, 50)));

    marker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    marker->setPos(toScenePoint(position));
    marker->setZValue(1.0);
}

// Обновляет положение и размеры предварительного контура.
void PlanView::showBuildingPreview(const PlanPoint& first, const PlanPoint& second)
{
    if (!buildingPreview_)
    {
        QPen outline(QColor(35, 105, 210));
        outline.setCosmetic(true);

        const QBrush fill(QColor(35, 105, 210, 55));

        buildingPreview_ = scene()->addRect(QRectF(), outline, fill);
        buildingPreview_->setZValue(2.0);
        buildingPreview_->setAcceptedMouseButtons(Qt::NoButton);
    }

    const QRectF rectangle(toScenePoint(first), toScenePoint(second));
    buildingPreview_->setRect(rectangle.normalized());
    buildingPreview_->show();
}

// Скрывает предварительный контур до следующего построения.
void PlanView::hideBuildingPreview()
{
    if (buildingPreview_)
        buildingPreview_->hide();
}

// Очищает сцену и сбрасывает указатель на предварительный контур.
void PlanView::clearPlan()
{
    buildingPreview_ = nullptr;
    scene()->clear();
}

// Передаёт координаты выбранной точки плана.
void PlanView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    const QPointF point = mapToScene(event->position().toPoint());
    emit pointSelected(PlanPoint{point.x(), -point.y()});
    event->accept();
}

// Передаёт текущие координаты курсора на плане.
void PlanView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF point = mapToScene(event->position().toPoint());
    emit cursorMoved(PlanPoint{point.x(), -point.y()});

    QGraphicsView::mouseMoveEvent(event);
}