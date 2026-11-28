#pragma once

#include "../model/Project.h"

#include <QGraphicsView>
#include <QPoint>

class QGraphicsLineItem;
class QGraphicsPathItem;
class QGraphicsRectItem;
class QLabel;
class QMouseEvent;
class QWheelEvent;

// Отображает план и обрабатывает управление сценой.
class PlanView : public QGraphicsView
{
    Q_OBJECT

public:
    // Создаёт сцену плана.
    explicit PlanView(QWidget* parent = nullptr);

    // Отображает здание и выделяет его контур при выборе.
    void drawBuilding(const Building& building, bool selected = false);

    // Отображает точечную цель.
    void drawObservationPoint(const PlanPoint& position, bool covered = false,
                              PlanObjectId id = 0);

    // Отображает камеру и покрытие на выбранной высоте.
    void drawCamera(const CameraPlacement& camera, bool selected,
                    const Project& project, double z);

    // Обновляет предварительную область обзора.
    void previewCamera(const CameraPlacement& camera, const Project& project, double z);

    // Отображает ограждение.
    void drawFence(const Fence& fence);

    // Отображает отрезок наблюдения или запрета.
    void drawSegment(const PlanSegment& segment, PlanObjectId id, bool forbidden,
                     bool selected);

    // Отображает дорогу и её ширину.
    void drawRoad(const Road& road, bool selected);

    // Показывает полосу дороги перед добавлением.
    void previewRoad(const Road& road, bool allowed);

    // Отображает отдельную монтажную опору.
    void drawPole(const MountPole& pole, bool selected);

    // Возвращает допуск выбора края в метрах плана.
    double facadePickTolerance() const;

    // Возвращает идентификатор объекта в указанной точке.
    PlanObjectId objectAt(PlanPoint point) const;

    // Показывает прямоугольник и размеры строящегося здания.
    void showBuildingPreview(const PlanPoint& first, const PlanPoint& second);

    // Показывает строящееся ограждение и его длину.
    void showFencePreview(const PlanSegment& segment, bool allowed);

    // Скрывает элементы предварительного построения.
    void hidePreview();

    // Очищает сцену.
    void clearPlan();

signals:
    // Передаёт координаты нажатия мыши.
    void pointSelected(PlanPoint position);

    // Передаёт координаты курсора.
    void cursorMoved(PlanPoint position);

    // Передаёт конечную точку перетаскивания.
    void dragFinished(PlanPoint position);

protected:
    // Рисует сетку под объектами плана.
    void drawBackground(QPainter* painter, const QRectF& rect) override;

    // Изменяет масштаб относительно курсора.
    void wheelEvent(QWheelEvent* event) override;

    // Показывает подсказку высоты возле объекта.
    bool viewportEvent(QEvent* event) override;

    // Обрабатывает нажатие кнопки мыши.
    void mousePressEvent(QMouseEvent* event) override;

    // Обрабатывает перемещение мыши.
    void mouseMoveEvent(QMouseEvent* event) override;

    // Обрабатывает отпускание кнопки мыши.
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // Показывает размеры рядом с точкой построения.
    void showDimensions(const QString& text, PlanPoint position);

    QLabel* dimensions_ = nullptr;
    QPoint pressPosition_;
    QPoint panPosition_;
    bool panning_ = false;
    bool leftPressed_ = false;
    QGraphicsPathItem* roadPreview_ = nullptr;
    QGraphicsLineItem* fencePreview_ = nullptr;
    QGraphicsRectItem* buildingPreview_ = nullptr;
    QGraphicsPathItem* cameraPreview_ = nullptr;
};

