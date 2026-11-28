#include "PlanView.h"
#include "../geometry/VisibilityCalculator.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHelpEvent>
#include <QLabel>
#include <QLineF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QScrollBar>
#include <QToolTip>
#include <QVariant>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace
{
constexpr int objectIdRole = 0;
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

// Формирует основание части здания с учётом дворов.
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

    // Добавляет направление через вершину препятствия.
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

    // Возвращает точку сцены по направлению и дальности.
    const auto screenPoint = [&](double angle, double radius) {
        return center + QPointF(std::sin(angle * radiansPerDegree) * radius,
                                -std::cos(angle * radiansPerDegree) * radius);
    };

    // Вычисляет интервалы видимости для заданного направления.
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

// Настраивает графическую сцену, масштаб и подпись размеров.
PlanView::PlanView(QWidget* parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    viewport()->setMouseTracking(true);
    setSceneRect(-100000.0, -100000.0, 200000.0, 200000.0);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setBackgroundBrush(QColor(248, 249, 250));
    setRenderHint(QPainter::Antialiasing);

    dimensions_ = new QLabel(viewport());
    dimensions_->setAttribute(Qt::WA_TransparentForMouseEvents);
    dimensions_->setStyleSheet(QStringLiteral(
        "QLabel { background: #fbfcfd; color: #273444; padding: 3px 6px; "
        "border: 1px solid #d7dce2; border-radius: 4px; }"));
    dimensions_->hide();

    scale(12.0, 12.0);
    centerOn(10.0, -10.0);
}

// Рисует серую сетку с шагом, зависящим от масштаба.
void PlanView::drawBackground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawBackground(painter, rect);

    const double pixelsPerMeter = transform().m11();
    const double step = pixelsPerMeter >= 8.0 ? 1.0 :
                            pixelsPerMeter >= 1.6 ? 5.0 : 10.0;

    QPen minor(QColor(100, 110, 120, 23));
    QPen major(QColor(100, 110, 120, 44));
    minor.setCosmetic(true);
    major.setCosmetic(true);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);

    for (auto index = static_cast<long long>(std::ceil(rect.left() / step));
         index * step <= rect.right(); ++index)
    {
        painter->setPen(index % 5 == 0 ? major : minor);
        const double x = index * step;
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    }

    for (auto index = static_cast<long long>(std::ceil(rect.top() / step));
         index * step <= rect.bottom(); ++index)
    {
        painter->setPen(index % 5 == 0 ? major : minor);
        const double y = index * step;
        painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
    }

    painter->restore();
}

// Отображает части здания и выделяет выбранный контур.
void PlanView::drawBuilding(const Building& building, bool selected)
{
    QPen pen(selected ? QColor(30, 100, 210) : QColor(64, 72, 82));
    pen.setCosmetic(true);
    pen.setWidth(selected ? 2 : 1);

    const QString tooltip = building.heightMeters
                                ? tr("Высота %1 м").arg(QLocale().toString(*building.heightMeters, 'f', 2))
                                : tr("Высота не задана");

    for (const auto& part : building.footprints)
    {
        auto* item = scene()->addPath(
            footprintPath(part), pen, QBrush(QColor(185, 194, 204)));

        item->setData(objectIdRole, QVariant::fromValue<qulonglong>(building.id));
        item->setToolTip(tooltip);
    }
}

// Отображает цветной маркер цели.
void PlanView::drawObservationPoint(const PlanPoint& position, bool covered, PlanObjectId id)
{
    const QColor color = covered ? QColor(32, 147, 80) : QColor(196, 45, 50);

    auto* marker = scene()->addEllipse(
        -4, -4, 8, 8, QPen(Qt::NoPen), QBrush(color));

    marker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    marker->setPos(toScenePoint(position));
    marker->setZValue(1.0);
    marker->setData(objectIdRole, QVariant::fromValue<qulonglong>(id));
}

// Отображает камеру и покрытие выбранной камеры.
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
    marker->setData(objectIdRole, QVariant::fromValue<qulonglong>(camera.id));
}

// Обновляет предварительную область обзора.
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

// Отображает отрезок ограждения и его высоту.
void PlanView::drawFence(const Fence& fence)
{
    QPen pen(QColor(129, 84, 40));
    pen.setWidth(0);
    pen.setCosmetic(true);

    auto* item = scene()->addLine(
        QLineF(toScenePoint(fence.segment.start), toScenePoint(fence.segment.end)), pen);

    item->setZValue(0.8);
    item->setData(objectIdRole, QVariant::fromValue<qulonglong>(fence.id));
    item->setToolTip(tr("Высота %1 м").arg(QLocale().toString(fence.heightMeters, 'f', 2)));
}

// Возвращает идентификатор верхнего объекта возле точки.
PlanObjectId PlanView::objectAt(PlanPoint point) const
{
    const QPoint at = mapFromScene(toScenePoint(point));
    const QRect area(at - QPoint(3, 3), QSize(7, 7));

    for (const auto* item : items(area))
    {
        const auto id = item->data(objectIdRole).toULongLong();

        if (id != 0)
            return id;
    }

    return 0;
}

// Показывает размеры рядом с текущей точкой построения.
void PlanView::showDimensions(const QString& text, PlanPoint position)
{
    QToolTip::hideText();
    dimensions_->setText(text);
    dimensions_->adjustSize();

    const QPoint point = mapFromScene(toScenePoint(position));
    const int maxX = std::max(0, viewport()->width() - dimensions_->width());
    const int maxY = std::max(0, viewport()->height() - dimensions_->height());

    dimensions_->move(std::clamp(point.x() + 14, 0, maxX),
                      std::clamp(point.y() + 18, 0, maxY));
    dimensions_->show();
}

// Показывает предварительный прямоугольник и его размеры.
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

    const QRectF bounds = QRectF(toScenePoint(first), toScenePoint(second)).normalized();
    buildingPreview_->setRect(bounds);
    buildingPreview_->show();

    showDimensions(tr("%1 × %2 м")
                       .arg(QLocale().toString(bounds.width(), 'f', 2))
                       .arg(QLocale().toString(bounds.height(), 'f', 2)),
                   second);
}

// Показывает строящееся ограждение и отмечает конфликт красным.
void PlanView::showFencePreview(const PlanSegment& segment, bool allowed)
{
    const QLineF line(toScenePoint(segment.start), toScenePoint(segment.end));
    QPen pen(allowed ? QColor(129, 84, 40) : QColor(196, 45, 50));
    pen.setStyle(Qt::DashLine);
    pen.setCosmetic(true);
    pen.setWidth(2);

    if (!fencePreview_)
    {
        fencePreview_ = scene()->addLine(line, pen);
        fencePreview_->setZValue(2.5);
        fencePreview_->setAcceptedMouseButtons(Qt::NoButton);
    }

    fencePreview_->setLine(line);
    fencePreview_->setPen(pen);
    fencePreview_->show();

    showDimensions(tr("Длина %1 м").arg(QLocale().toString(line.length(), 'f', 2)),
                   segment.end);
}

// Скрывает элементы предварительного построения.
void PlanView::hidePreview()
{
    QToolTip::hideText();
    dimensions_->hide();

    if (roadPreview_)
        roadPreview_->hide();
    if (fencePreview_)
        fencePreview_->hide();

    if (buildingPreview_)
        buildingPreview_->hide();

    if (cameraPreview_)
        cameraPreview_->hide();
}

// Очищает сцену и сбрасывает указатели предпросмотра.
void PlanView::clearPlan()
{
    hidePreview();
    roadPreview_ = nullptr;
    fencePreview_ = nullptr;
    cameraPreview_ = nullptr;
    buildingPreview_ = nullptr;
    scene()->clear();
}

// Сохраняет точку под курсором при изменении масштаба.
void PlanView::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();

    if (delta == 0)
    {
        event->ignore();
        return;
    }

    const QPoint position = event->position().toPoint();
    const QPointF anchor = mapToScene(position);
    const double current = transform().m11();
    const double next = std::clamp(current * std::pow(1.15, delta / 120.0), 0.8, 96.0);

    const double factor = next / current;
    scale(factor, factor);

    const QPoint offset = mapFromScene(anchor) - position;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + offset.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + offset.y());

    QToolTip::hideText();
    emit cursorMoved(toPlanPoint(mapToScene(position)));
    event->accept();
}

// Показывает высоту здания или ограждения возле курсора.
bool PlanView::viewportEvent(QEvent* event)
{
    if (event->type() != QEvent::ToolTip)
        return QGraphicsView::viewportEvent(event);

    auto* help = static_cast<QHelpEvent*>(event);

    if (dimensions_ && !dimensions_->isVisible() && !panning_)
    {
        const QRect area(help->pos() - QPoint(3, 3), QSize(7, 7));

        for (const auto* item : items(area))
        {
            if (!item->toolTip().isEmpty())
            {
                QToolTip::showText(help->globalPos(), item->toolTip(), viewport(), area);
                return true;
            }
        }
    }

    QToolTip::hideText();
    event->ignore();
    return true;
}

// Начинает перемещение плана или передаёт точку построения.
void PlanView::mousePressEvent(QMouseEvent* event)
{
    const QPoint position = event->position().toPoint();

    if (event->button() == Qt::MiddleButton)
    {
        panning_ = true;
        leftPressed_ = false;
        panPosition_ = position;
        viewport()->setCursor(Qt::ClosedHandCursor);
        QToolTip::hideText();
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton)
    {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    if (!panning_)
    {
        leftPressed_ = true;
        pressPosition_ = position;
        emit pointSelected(toPlanPoint(mapToScene(position)));
    }

    event->accept();
}

// Перемещает план и передаёт координаты курсора.
void PlanView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint point = event->position().toPoint();

    if (panning_)
    {
        const QPoint delta = point - panPosition_;
        panPosition_ = point;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());

        emit cursorMoved(toPlanPoint(mapToScene(point)));
        event->accept();
        return;
    }

    emit cursorMoved(toPlanPoint(mapToScene(point)));
    QGraphicsView::mouseMoveEvent(event);
}

// Завершает перемещение плана или установку камеры перетаскиванием.
void PlanView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && panning_)
    {
        panning_ = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton)
    {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    const QPoint point = event->position().toPoint();
    const bool dragged = leftPressed_ && !panning_ &&
                         (point - pressPosition_).manhattanLength() >= QApplication::startDragDistance();

    leftPressed_ = false;

    if (dragged)
        emit dragFinished(toPlanPoint(mapToScene(point)));

    event->accept();
}

// Отображает линию наблюдения либо красный запрет на фасаде.
void PlanView::drawSegment(const PlanSegment& segment, PlanObjectId id,
                           bool forbidden, bool selected)
{
    QPen pen(forbidden ? QColor(195, 55, 65) : QColor(35, 110, 165));
    pen.setWidth(selected ? 4 : 2);
    pen.setCosmetic(true);
    if (forbidden)
        pen.setStyle(Qt::DashLine);

    auto* item = scene()->addLine(
        QLineF(toScenePoint(segment.start), toScenePoint(segment.end)), pen);
    item->setZValue(1.3);
    item->setData(objectIdRole, QVariant::fromValue<qulonglong>(id));
}

// Помечает опору на плане и показывает высоту крепления.
void PlanView::drawPole(const MountPole& pole, bool selected)
{
    const QColor color = selected ? QColor(30, 100, 210) : QColor(121, 73, 153);
    auto* item = scene()->addRect(-5, -5, 10, 10, QPen(color, 2), QBrush(Qt::white));
    item->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    item->setPos(toScenePoint(pole.position));
    item->setZValue(1.5);
    item->setData(objectIdRole, QVariant::fromValue<qulonglong>(pole.id));
    item->setToolTip(tr("Высота крепления %1 м").arg(pole.heightMeters));
}

// Переводит допуск в восемь экранных пикселей в метры плана.
double PlanView::facadePickTolerance() const
{
    return 8.0 / transform().m11();
}


// Отображает полосу дороги в реальном масштабе.
void PlanView::drawRoad(const Road& road, bool selected)
{
    const auto part = Project::roadFootprint(road);
    if (!part)
        return;

    QPen outline(selected ? QColor(30, 100, 210) : QColor(103, 112, 122));
    outline.setCosmetic(true);
    outline.setWidth(selected ? 2 : 1);
    auto* item = scene()->addPath(footprintPath(*part), outline,
                                  QBrush(QColor(189, 195, 201)));
    item->setZValue(-1.0);
    item->setData(objectIdRole, QVariant::fromValue<qulonglong>(road.id));
    item->setToolTip(tr("Ширина %1 м").arg(road.widthMeters));
}

// Показывает полосу и отмечает конфликт красным цветом.
void PlanView::previewRoad(const Road& road, bool allowed)
{
    const auto part = Project::roadFootprint(road);
    if (!part)
    {
        if (roadPreview_)
            roadPreview_->hide();
        return;
    }

    if (!roadPreview_)
    {
        roadPreview_ = scene()->addPath(QPainterPath());
        roadPreview_->setAcceptedMouseButtons(Qt::NoButton);
        roadPreview_->setZValue(2.5);
    }

    const QColor color = allowed ? QColor(72, 114, 154, 110) : QColor(195, 55, 65, 120);
    roadPreview_->setPen(QPen(Qt::NoPen));
    roadPreview_->setBrush(QBrush(color));
    roadPreview_->setPath(footprintPath(*part));
    roadPreview_->show();
    const auto a = toScenePoint(road.centerline.front());
    const auto b = toScenePoint(road.centerline.back());
    showDimensions(tr("%1 м × %2 м").arg(QLineF(a, b).length(), 0, 'f', 2)
                       .arg(road.widthMeters, 0, 'f', 2),
                   road.centerline.back());
}

