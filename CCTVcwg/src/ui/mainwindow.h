#pragma once

#include "../model/Project.h"

#include <QMainWindow>

#include <optional>

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

class PlanView;

// Главное окно редактора плана объекта.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Создаёт главное окно и настраивает инструменты редактирования.
    explicit MainWindow(QWidget* parent = nullptr);

    // Освобождает объект формы главного окна.
    ~MainWindow() override;

private:
    // Доступные инструменты редактирования плана.
    enum class DrawingTool
    {
        ObservationPoint,
        Building
    };

    // Создаёт панель выбора инструмента.
    void setupDrawingTools();

    // Выбирает инструмент и сбрасывает незавершённое построение.
    void selectDrawingTool(DrawingTool tool);

    // Передаёт координаты щелчка активному инструменту.
    void handlePlanClick(PlanPoint position);

    // Обновляет предварительный контур по положению курсора.
    void updateBuildingPreview(PlanPoint position);

    // Отменяет незавершённое построение здания.
    void cancelBuilding();

    // Отменяет построение или последнее добавление объекта.
    void undoLastAction();

    // Восстанавливает отображение объектов из модели.
    void redrawPlan();

    // Добавляет и отображает точечную цель наблюдения.
    void addObservationPoint(PlanPoint position);

    // Принимает углы прямоугольника и добавляет здание.
    void addBuildingCorner(PlanPoint position);

    Ui::MainWindow* ui;
    Project project_;
    PlanView* planView_;
    DrawingTool drawingTool_ = DrawingTool::ObservationPoint;
    std::optional<PlanPoint> firstBuildingCorner_;
};