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
class QComboBox;
class QDoubleSpinBox;

// Главное окно редактора плана.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Создаёт окно и подключает инструменты.
    explicit MainWindow(QWidget* parent = nullptr);

    // Освобождает объект формы.
    ~MainWindow() override;

private:
    // Инструменты редактирования плана.
    enum class DrawingTool
    {
        ObservationPoint,
        Building,
        Camera,
        Fence
    };

    // Создаёт панель инструментов.
    void setupDrawingTools();

    // Создаёт панель параметров высоты.
    void setupHeights();

    // Переключает активный инструмент.
    void selectDrawingTool(DrawingTool tool);

    // Передаёт щелчок активному инструменту.
    void handlePlanClick(PlanPoint position);

    // Обновляет предварительную геометрию.
    void updateDrawingPreview(PlanPoint position);

    // Сбрасывает незавершённое построение.
    void cancelDrawing();

    // Отменяет построение или последнее добавление.
    void undoLastAction();

    // Отображает текущее состояние проекта.
    void redrawPlan();

    // Добавляет цель на заданной высоте.
    void addObservationPoint(PlanPoint position);

    // Принимает противоположные углы здания.
    void addBuildingCorner(PlanPoint position);

    // Принимает конечные точки ограждения.
    void addFencePoint(PlanPoint position);

    // Задаёт место установки и направление камеры.
    void placeCamera(PlanPoint position);

    // Формирует камеру по параметрам панели.
    CameraPlacement makeCamera(PlanPoint at, double angle) const;

    // Возвращает выбранную для расчёта камеру.
    const CameraPlacement* selectedCamera() const;

    // Добавляет объект в список изменения высоты.
    void rememberObject(const QString& type, PlanObjectId id);

    Ui::MainWindow* ui;
    Project project_;
    PlanView* planView_;

    QComboBox* cameraList_ = nullptr;
    QComboBox* objects_ = nullptr;
    QDoubleSpinBox* cameraZ_ = nullptr;
    QDoubleSpinBox* targetZ_ = nullptr;
    QDoubleSpinBox* buildingZ_ = nullptr;
    QDoubleSpinBox* fenceZ_ = nullptr;
    QDoubleSpinBox* tilt_ = nullptr;
    QDoubleSpinBox* vfov_ = nullptr;

    DrawingTool drawingTool_ = DrawingTool::ObservationPoint;
    std::optional<PlanPoint> firstBuildingCorner_;
    std::optional<PlanPoint> fenceStart_;
    std::optional<PlanPoint> cameraOrigin_;
};