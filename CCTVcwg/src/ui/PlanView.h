#pragma once

#include "../model/PlanObjects.h"

#include <QGraphicsView>

class QGraphicsRectItem;
class QMouseEvent;

// Графическое представление плана объекта.
class PlanView : public QGraphicsView
{
    Q_OBJECT

public:
    // Создаёт графическую сцену плана.
    explicit PlanView(QWidget* parent = nullptr);

    // Отображает контуры здания и его внутренних дворов.
    void drawBuilding(const Building& building);

    // Отображает точечную цель наблюдения.
    void drawObservationPoint(const PlanPoint& position);

    // Отображает предварительный контур здания по двум углам.
    void showBuildingPreview(const PlanPoint& first, const PlanPoint& second);

    // Скрывает предварительный контур здания.
    void hideBuildingPreview();

    // Удаляет графические объекты плана.
    void clearPlan();

signals:
    // Передаёт координаты выбранной точки в метрах.
    void pointSelected(PlanPoint position);

    // Передаёт текущие координаты курсора в метрах.
    void cursorMoved(PlanPoint position);

protected:
    // Обрабатывает выбор точки левой кнопкой мыши.
    void mousePressEvent(QMouseEvent* event) override;

    // Обрабатывает перемещение курсора над планом.
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QGraphicsRectItem* buildingPreview_ = nullptr;
};