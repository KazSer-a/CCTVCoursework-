#pragma once

#include "../model/Project.h"

#include <QGraphicsView>
#include <QPoint>

class QGraphicsPathItem;
class QGraphicsRectItem;
class QMouseEvent;

// Графическое представление плана.
class PlanView : public QGraphicsView
{
    Q_OBJECT

public:
    // Создаёт сцену плана.
    explicit PlanView(QWidget* parent = nullptr);

    // Отображает части здания и дворы.
    void drawBuilding(const Building& building);

    // Отображает точечную цель.
    void drawObservationPoint(const PlanPoint& position, bool covered = false);

    // Отображает камеру и покрытие выбранной высоты.
    void drawCamera(const CameraPlacement& camera, bool selected,
                    const Project& project, double z);

    // Обновляет предварительную область обзора.
    void previewCamera(const CameraPlacement& camera, const Project& project, double z);

    // Отображает ограждение.
    void drawFence(const Fence& fence);

    // Обновляет прямоугольник строящегося здания.
    void showBuildingPreview(const PlanPoint& first, const PlanPoint& second);

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
    // Обрабатывает нажатие кнопки мыши.
    void mousePressEvent(QMouseEvent* event) override;

    // Обрабатывает перемещение мыши.
    void mouseMoveEvent(QMouseEvent* event) override;

    // Обрабатывает отпускание кнопки мыши.
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPoint pressPosition_;
    QGraphicsRectItem* buildingPreview_ = nullptr;
    QGraphicsPathItem* cameraPreview_ = nullptr;
};