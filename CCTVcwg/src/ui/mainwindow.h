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
class QFormLayout;
class QLabel;
class QPushButton;

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
    // Режимы взаимодействия с планом.
    enum class DrawingTool
    {
        Select,
        ObservationPoint,
        Building,
        Camera,
        Fence,
        ObservationLine,
        Pole,
        FacadeBan,
        Road
    };

    // Создаёт панель инструментов.
    void setupDrawingTools();

    // Создаёт контекстную панель свойств.
    void setupHeights();

    // Переключает активный инструмент.
    void selectDrawingTool(DrawingTool tool);

    // Передаёт щелчок активному инструменту.
    void handlePlanClick(PlanPoint position);

    // Обновляет предварительную геометрию.
    void updateDrawingPreview(PlanPoint position);

    // Сбрасывает незавершённое построение или выбор объекта.
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
    CameraPlacement makeCamera(PlanPoint position, double angle) const;

    // Возвращает выбранную для расчёта камеру.
    const CameraPlacement* selectedCamera() const;

    // Выбирает объект под курсором.
    void selectObject(PlanPoint point);

    // Обновляет поля текущего режима.
    void updateInspector();

    // Сохраняет параметры выделенного объекта.
    void applyProperties();

    // Показывает контур здания с введёнными размерами.
    void previewResize();

    // Возвращает выбранное здание.
    const Building* selectedBuilding() const;

    // Принимает концы отрезка наблюдения.
    void addLinePoint(PlanPoint point);

    // Добавляет опору или запрет по щелчку.
    void addPoleAt(PlanPoint point);
    void addBanAt(PlanPoint point);

    // Удаляет выбранный объект.
    void deleteSelected();

    // Перестраивает список камер и восстанавливает выбор по идентификатору.
    void syncCameraList(PlanObjectId active);

    QPushButton* deleteButton_ = nullptr;
    QDoubleSpinBox* lineStep_ = nullptr;
    QDoubleSpinBox* lineHeight_ = nullptr;
    QDoubleSpinBox* poleHeight_ = nullptr;
    // Формирует интервал на закреплённом фасаде.
    FacadeBan banAt(PlanPoint point) const;

    // Принимает концы осевой линии дороги.
    void addRoadPoint(PlanPoint point);

    QDoubleSpinBox* roadWidth_ = nullptr;
    std::optional<PlanPoint> roadStart_;
    std::optional<FacadeBan> banStart_;
    std::optional<double> cameraMountHeight_;
    std::optional<PlanPoint> lineStart_;

    Ui::MainWindow* ui;
    Project project_;
    PlanView* planView_;

    QComboBox* cameraList_ = nullptr;
    QFormLayout* properties_ = nullptr;
    QLabel* propertiesTitle_ = nullptr;
    QLabel* propertiesMessage_ = nullptr;
    QPushButton* apply_ = nullptr;

    QDoubleSpinBox* cameraZ_ = nullptr;
    QDoubleSpinBox* targetZ_ = nullptr;
    QDoubleSpinBox* buildingZ_ = nullptr;
    QDoubleSpinBox* fenceZ_ = nullptr;
    QDoubleSpinBox* tilt_ = nullptr;
    QDoubleSpinBox* vfov_ = nullptr;
    QDoubleSpinBox* sliceZ_ = nullptr;
    QDoubleSpinBox* editWidth_ = nullptr;
    QDoubleSpinBox* editLength_ = nullptr;
    QDoubleSpinBox* editHeight_ = nullptr;

    PlanObjectId selectedId_ = 0;
    DrawingTool drawingTool_ = DrawingTool::Select;

    std::optional<PlanPoint> firstBuildingCorner_;
    std::optional<PlanPoint> fenceStart_;
    std::optional<PlanPoint> cameraOrigin_;
};

