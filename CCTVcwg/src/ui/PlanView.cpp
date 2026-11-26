#include "PlanView.h"
#include "../geometry/VisibilityCalculator.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace
{
constexpr double radiansPerDegree = 0.017453292519943295;

// Переводит координаты плана в координаты сцены.
QPointF toScenePoint(const PlanPoint& point)
{
    return {point.xMeters, -point.yMeters};
}

// Переводит координаты сцены в координаты плана.
PlanPoint toPlanPoint(const QPointF& point)
{
    return {point.x(), -point.y()};
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

// Формирует основание одной части здания с дворами.
QPainterPath footprintPath(const BuildingFootprint& part)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    appendRing(path, part.outerRing);

    for (const auto& hole : part.innerRings)
        appendRing(path, hole);

    return path;
}

// Строит видимую область на плоскости заданной высоты.
QPainterPath coveragePath(const CameraPlacement& camera, const Project& project, double z)
{
    const double width = camera.model.horizontalFovDegrees;
    const double start = camera.pose.azimuthDegrees - width / 2.0;

    std::vector<double> angles{0.0, width};

    for (double angle = 1.0; angle < width; angle += 1.0)
        angles.push_back(angle);

    // Дополнительные направления проходят через вершины препятствий.
    const auto addVertex = [&](PlanPoint point) {
        const auto bearing = VisibilityCalculator::bearing(camera.pose.position, point);

        if (!bearing)
            return;

        const double angle = std::fmod(*bearing - start + 720.0, 360.0);

        if (angle > 0.0 && angle < width)
            angles.push_back(angle);
    };

    QPainterPath solid;

    for (const auto& building : project.buildings())
    {
        for (const auto& part : building.footprints)
        {
            for (const auto& point : part.outerRing)
                addVertex(point);

            for (const auto& hole : part.innerRings)
            {
                for (const auto& point : hole)
                    addVertex(point);
            }

            if (!building.heightMeters || z <= *building.heightMeters)
                solid = solid.united(footprintPath(part));
        }
    }

    for (const auto& fence : project.fences())
    {
        addVertex(fence.segment.start);
        addVertex(fence.segment.end);
    }

    std::sort(angles.begin(), angles.end());

    const QPointF center = toScenePoint(camera.pose.position);

    const auto screenPoint = [&](double angle, double radius) {
        return center + QPointF(
                   std::sin(angle * radiansPerDegree) * radius,
                   -std::cos(angle * radiansPerDegree) * radius);
    };

    const auto spans = [&](double angle) {
        return VisibilityCalculator::visibleRay(
            camera, angle, z, project.buildings(), project.fences());
    };

    QPainterPath path;
    path.setFillRule(Qt::WindingFill);

    for (std::size_t i = 1; i < angles.size(); ++i)
    {
        const double a = start + angles[i - 1];
        const double b = start + angles[i];

        if (b - a < 1e-8)
            continue;

        const double inset = (b - a) * 1e-6;
        auto left = spans(a + inset);
        auto right = spans(b - inset);

        if (left.size() != right.size())
        {
            left = spans((a + b) / 2.0);
            right = left;
        }

        for (std::size_t j = 0; j < left.size(); ++j)
        {
            path.moveTo(screenPoint(a, left[j].nearMeters));
            path.lineTo(screenPoint(a, left[j].farMeters));
            path.lineTo(screenPoint(b, right[j].farMeters));
            path.lineTo(screenPoint(b, right[j].nearMeters));
            path.closeSubpath();
        }
    }

    return path.subtracted(solid);
}
}

// Настраивает графическую сцену и масштаб.
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

// Отображает контуры частей здания.
void PlanView::drawBuilding(const Building& building)
{
    QPen pen(QColor(64, 72, 82));
    pen.setCosmetic(true);

    for (const auto& part : building.footprints)
        scene()->addPath(footprintPath(part), pen, QBrush(QColor(185, 194, 204)));
}

// Отображает цветной маркер цели.
void PlanView::drawObservationPoint(const PlanPoint& position, bool covered)
{
    const QColor color = covered ? QColor(32, 147, 80) : QColor(196, 45, 50);

    auto* marker = scene()->addEllipse(
        -4, -4, 8, 8, QPen(Qt::NoPen), QBrush(color));

    marker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    marker->setPos(toScenePoint(position));
    marker->setZValue(1.0);
}

// Отображает камеру и видимую область выбранного уровня.
void PlanView::drawCamera(const CameraPlacement& camera, bool selected,
                          const Project& project, double z)
{
    if (selected)
    {
        auto* coverage = scene()->addPath(
            coveragePath(camera, project, z), QPen(Qt::NoPen),
            QBrush(QColor(38, 112, 210, 70)));

        coverage->setZValue(0.5);
    }

    auto* marker = scene()->addRect(
        -4, -4, 8, 8, QPen(Qt::NoPen), QBrush(QColor(42, 98, 180)));

    marker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    marker->setPos(toScenePoint(camera.pose.position));
    marker->setZValue(2.0);
}

// Обновляет предварительный сектор при повороте камеры.
void PlanView::previewCamera(const CameraPlacement& camera, const Project& project, double z)
{
    if (!cameraPreview_)
    {
        cameraPreview_ = scene()->addPath(
            QPainterPath(), QPen(Qt::NoPen), QBrush(QColor(64, 150, 230, 90)));

        cameraPreview_->setAcceptedMouseButtons(Qt::NoButton);
        cameraPreview_->setZValue(0.75);
    }

    cameraPreview_->setPath(coveragePath(camera, project, z));
    cameraPreview_->show();
}

// Отображает отрезок ограждения.
void PlanView::drawFence(const Fence& fence)
{
    QPen pen(QColor(129, 84, 40));
    pen.setCosmetic(true);
    pen.setWidth(3);

    scene()->addLine(
        QLineF(toScenePoint(fence.segment.start), toScenePoint(fence.segment.end)),
        pen);
}

// Обновляет прямоугольник строящегося здания.
void PlanView::showBuildingPreview(const PlanPoint& first, const PlanPoint& second)
{
    if (!buildingPreview_)
    {
        QPen pen(QColor(35, 105, 210));
        pen.setCosmetic(true);

        buildingPreview_ = scene()->addRect(
            QRectF(), pen, QBrush(QColor(35, 105, 210, 55)));

        buildingPreview_->setAcceptedMouseButtons(Qt::NoButton);
        buildingPreview_->setZValue(2.0);
    }

    const QRectF rectangle(toScenePoint(first), toScenePoint(second));
    buildingPreview_->setRect(rectangle.normalized());
    buildingPreview_->show();
}

// Скрывает элементы предварительного построения.
void PlanView::hidePreview()
{
    if (buildingPreview_)
        buildingPreview_->hide();

    if (cameraPreview_)
        cameraPreview_->hide();
}

// Очищает сцену и сбрасывает указатели предпросмотра.
void PlanView::clearPlan()
{
    cameraPreview_ = nullptr;
    buildingPreview_ = nullptr;
    scene()->clear();
}

// Передаёт точку нажатия левой кнопки.
void PlanView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    pressPosition_ = event->position().toPoint();
    emit pointSelected(toPlanPoint(mapToScene(pressPosition_)));
    event->accept();
}

// Передаёт координаты курсора.
void PlanView::mouseMoveEvent(QMouseEvent* event)
{
    emit cursorMoved(toPlanPoint(mapToScene(event->position().toPoint())));
    QGraphicsView::mouseMoveEvent(event);
}

// Завершает установку по перетаскиванию.
void PlanView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    const QPoint point = event->position().toPoint();

    if ((point - pressPosition_).manhattanLength() >= QApplication::startDragDistance())
        emit dragFinished(toPlanPoint(mapToScene(point)));

    event->accept();
}