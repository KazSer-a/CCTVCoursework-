#include "mainwindow.h"

#include "PlanView.h"
#include "ui_mainwindow.h"
#include "../geometry/VisibilityCalculator.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QString>
#include <QToolBar>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <variant>

// Размещает план и подключает команды редактора.
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
    ui(new Ui::MainWindow),
    planView_(new PlanView(this))
{
    ui->setupUi(this);
    setWindowTitle(tr("Редактор плана объекта"));

    auto* layout = new QVBoxLayout(centralWidget());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(planView_);

    connect(planView_, &PlanView::pointSelected,
            this, &MainWindow::handlePlanClick);

    connect(planView_, &PlanView::cursorMoved,
            this, &MainWindow::updateDrawingPreview);

    connect(planView_, &PlanView::dragFinished, this, [this](PlanPoint point) {
        if (drawingTool_ == DrawingTool::Camera && cameraOrigin_)
            placeCamera(point);
    });

    setupDrawingTools();
    setupHeights();

    auto* cancel = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(cancel, &QShortcut::activated, this, &MainWindow::cancelDrawing);

    auto* undo = new QShortcut(QKeySequence(QKeySequence::Undo), this);
    undo->setAutoRepeat(false);
    connect(undo, &QShortcut::activated, this, &MainWindow::undoLastAction);
}

// Освобождает объект формы.
MainWindow::~MainWindow()
{
    delete ui;
}

// Создаёт инструменты построения и список камер.
void MainWindow::setupDrawingTools()
{
    auto* toolbar = addToolBar(tr("Инструменты плана"));
    auto* group = new QActionGroup(toolbar);

    const auto addTool = [&](const QString& title, DrawingTool tool) {
        auto* action = toolbar->addAction(title);
        action->setCheckable(true);
        group->addAction(action);

        connect(action, &QAction::toggled, this, [this, tool](bool checked) {
            if (checked)
                selectDrawingTool(tool);
        });

        return action;
    };

    auto* point = addTool(tr("Точка наблюдения"), DrawingTool::ObservationPoint);
    addTool(tr("Здание"), DrawingTool::Building);
    addTool(tr("Забор"), DrawingTool::Fence);
    addTool(tr("Камера"), DrawingTool::Camera);

    cameraList_ = new QComboBox(toolbar);
    cameraList_->setToolTip(tr("Камера для расчёта покрытия"));
    cameraList_->addItem(tr("Без выделения"));
    toolbar->addWidget(cameraList_);

    connect(cameraList_, &QComboBox::currentIndexChanged,
            this, &MainWindow::redrawPlan);

    point->setChecked(true);
}

// Создаёт параметры новых объектов и команду редактирования высоты.
void MainWindow::setupHeights()
{
    auto* dock = new QDockWidget(tr("Параметры плана"), this);
    auto* panel = new QWidget(dock);
    auto* form = new QFormLayout(panel);

    const auto field = [&](const QString& label, double low, double high, double value) {
        auto* input = new QDoubleSpinBox(panel);
        input->setDecimals(2);
        input->setRange(low, high);
        input->setValue(value);
        form->addRow(label, input);
        return input;
    };

    cameraZ_ = field(tr("Высота камеры, м"), 0.0, 1000.0, 4.0);
    tilt_ = field(tr("Наклон вниз, °"), -90.0, 90.0, 12.0);
    vfov_ = field(tr("Вертикальный угол, °"), 0.1, 180.0, 60.0);
    targetZ_ = field(tr("Высота цели и среза, м"), 0.0, 1000.0, 1.5);
    buildingZ_ = field(tr("Высота здания, м"), 0.01, 1000.0, 8.0);
    fenceZ_ = field(tr("Высота забора, м"), 0.01, 1000.0, 2.0);

    objects_ = new QComboBox(panel);
    form->addRow(tr("Объект проекта"), objects_);

    auto* edit = new QPushButton(tr("Изменить высоту объекта"), panel);
    form->addRow(edit);

    connect(edit, &QPushButton::clicked, this, [this] {
        if (objects_->currentIndex() < 0)
            return;

        const auto id = objects_->currentData().toULongLong();
        double current = 0.0;
        double minimum = 0.0;

        for (const auto& building : project_.buildings())
        {
            if (building.id == id)
            {
                current = building.heightMeters.value_or(8.0);
                minimum = 0.01;
            }
        }

        for (const auto& fence : project_.fences())
        {
            if (fence.id == id)
            {
                current = fence.heightMeters;
                minimum = 0.01;
            }
        }

        for (const auto& camera : project_.cameras())
        {
            if (camera.id == id)
                current = camera.pose.heightMeters;
        }

        for (const auto& target : project_.targets())
        {
            if (target.id == id)
            {
                current = std::visit(
                    [](const auto& geometry) { return geometry.heightMeters; },
                    target.geometry);
            }
        }

        bool accepted = false;
        const double meters = QInputDialog::getDouble(
            this, objects_->currentText(), tr("Высота, м"),
            current, minimum, 1000.0, 2, &accepted);

        if (accepted && project_.setHeight(id, meters))
            redrawPlan();
    });

    connect(targetZ_, &QDoubleSpinBox::valueChanged, this, &MainWindow::redrawPlan);

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

// Переключает инструмент и сбрасывает предварительное построение.
void MainWindow::selectDrawingTool(DrawingTool tool)
{
    drawingTool_ = tool;
    cameraOrigin_.reset();
    firstBuildingCorner_.reset();
    fenceStart_.reset();
    planView_->hidePreview();

    switch (tool)
    {
    case DrawingTool::ObservationPoint:
        statusBar()->showMessage(tr("Укажите положение цели."));
        break;

    case DrawingTool::Building:
        statusBar()->showMessage(tr("Укажите первый угол здания."));
        break;

    case DrawingTool::Fence:
        statusBar()->showMessage(tr("Укажите начало ограждения."));
        break;

    case DrawingTool::Camera:
        statusBar()->showMessage(tr("Зажмите левую кнопку и выберите направление."));
        break;
    }
}

// Передаёт точку активному инструменту.
void MainWindow::handlePlanClick(PlanPoint position)
{
    switch (drawingTool_)
    {
    case DrawingTool::ObservationPoint:
        addObservationPoint(position);
        break;

    case DrawingTool::Building:
        addBuildingCorner(position);
        break;

    case DrawingTool::Fence:
        addFencePoint(position);
        break;

    case DrawingTool::Camera:
        placeCamera(position);
        break;
    }
}

// Обновляет предварительное изображение здания или камеры.
void MainWindow::updateDrawingPreview(PlanPoint position)
{
    if (firstBuildingCorner_)
    {
        planView_->showBuildingPreview(*firstBuildingCorner_, position);
        return;
    }

    if (cameraOrigin_)
    {
        const auto angle = VisibilityCalculator::bearing(*cameraOrigin_, position);

        if (angle)
        {
            const auto camera = makeCamera(*cameraOrigin_, *angle);
            planView_->previewCamera(camera, project_, targetZ_->value());
        }
    }
}

// Отменяет незавершённое построение.
void MainWindow::cancelDrawing()
{
    if (firstBuildingCorner_ || cameraOrigin_ || fenceStart_)
        selectDrawingTool(drawingTool_);
}

// Отменяет последнее добавление объекта.
void MainWindow::undoLastAction()
{
    if (firstBuildingCorner_ || cameraOrigin_ || fenceStart_)
    {
        cancelDrawing();
        return;
    }

    if (!project_.undoLastAddition())
    {
        statusBar()->showMessage(tr("Нет действий для отмены."));
        return;
    }

    if (cameraList_->count() - 1 > static_cast<int>(project_.cameras().size()))
    {
        const QSignalBlocker blocker(cameraList_);
        cameraList_->removeItem(cameraList_->count() - 1);
    }

    objects_->removeItem(objects_->count() - 1);
    redrawPlan();
    statusBar()->showMessage(tr("Последнее добавление отменено."));
}

// Возвращает камеру по строке списка.
const CameraPlacement* MainWindow::selectedCamera() const
{
    const int index = cameraList_->currentIndex() - 1;

    if (index < 0 || static_cast<std::size_t>(index) >= project_.cameras().size())
        return nullptr;

    return &project_.cameras()[static_cast<std::size_t>(index)];
}

// Восстанавливает план и пересчитывает покрытие целей.
void MainWindow::redrawPlan()
{
    planView_->clearPlan();

    for (const auto& building : project_.buildings())
        planView_->drawBuilding(building);

    for (const auto& fence : project_.fences())
        planView_->drawFence(fence);

    const auto* selected = selectedCamera();

    for (const auto& camera : project_.cameras())
        planView_->drawCamera(camera, &camera == selected, project_, targetZ_->value());

    for (const auto& target : project_.targets())
    {
        if (const auto* point = std::get_if<ObservationPoint>(&target.geometry))
        {
            const bool covered = selected && VisibilityCalculator::visible(
                                     *selected, *point, project_.buildings(), project_.fences());

            planView_->drawObservationPoint(point->position, covered);
        }
    }
}

// Добавляет объект в список изменения высоты.
void MainWindow::rememberObject(const QString& type, PlanObjectId id)
{
    const auto number = static_cast<qulonglong>(id);

    objects_->addItem(
        tr("%1 №%2").arg(type).arg(number),
        QVariant::fromValue(number));

    objects_->setCurrentIndex(objects_->count() - 1);
}

// Создаёт цель на высоте, выбранной в панели параметров.
void MainWindow::addObservationPoint(PlanPoint position)
{
    ObservationTarget target;
    target.label = "Точка наблюдения";
    target.geometry = ObservationPoint{position, targetZ_->value()};

    const auto id = project_.addTarget(std::move(target));

    if (!id)
    {
        statusBar()->showMessage(tr("Недопустимые параметры цели."));
        return;
    }

    rememberObject(tr("Цель"), *id);
    redrawPlan();

    statusBar()->showMessage(
        tr("Цель №%1   X = %2 м   Y = %3 м")
            .arg(static_cast<qulonglong>(*id))
            .arg(position.xMeters, 0, 'f', 2)
            .arg(position.yMeters, 0, 'f', 2));
}

// Формирует прямоугольное здание заданной высоты.
void MainWindow::addBuildingCorner(PlanPoint position)
{
    if (!firstBuildingCorner_)
    {
        firstBuildingCorner_ = position;
        statusBar()->showMessage(tr("Укажите противоположный угол здания."));
        return;
    }

    const auto first = *firstBuildingCorner_;
    const double left = std::min(first.xMeters, position.xMeters);
    const double right = std::max(first.xMeters, position.xMeters);
    const double bottom = std::min(first.yMeters, position.yMeters);
    const double top = std::max(first.yMeters, position.yMeters);

    if (left == right || bottom == top)
    {
        statusBar()->showMessage(tr("Площадь здания должна быть больше нуля."));
        return;
    }

    BuildingFootprint footprint;
    footprint.outerRing = {
        {left, bottom}, {right, bottom}, {right, top}, {left, top}
    };

    Building building;
    building.heightMeters = buildingZ_->value();
    building.footprints.push_back(std::move(footprint));

    const auto id = project_.addBuilding(std::move(building));

    if (!id)
    {
        statusBar()->showMessage(
            tr("Проверьте контур здания и расположение установленных камер."));
        return;
    }

    firstBuildingCorner_.reset();
    rememberObject(tr("Здание"), *id);
    redrawPlan();

    statusBar()->showMessage(tr("Здание добавлено. Укажите первый угол следующего."));
}

// Создаёт прямолинейное ограждение заданной высоты.
void MainWindow::addFencePoint(PlanPoint point)
{
    if (!fenceStart_)
    {
        fenceStart_ = point;
        statusBar()->showMessage(tr("Укажите конец ограждения. Esc — отмена."));
        return;
    }

    Fence fence;
    fence.segment = {*fenceStart_, point};
    fence.heightMeters = fenceZ_->value();

    const auto id = project_.addFence(fence);

    if (!id)
    {
        statusBar()->showMessage(tr("Выберите другую конечную точку."));
        return;
    }

    fenceStart_.reset();
    rememberObject(tr("Забор"), *id);
    redrawPlan();

    statusBar()->showMessage(tr("Ограждение добавлено."));
}

// Формирует камеру по параметрам панели.
CameraPlacement MainWindow::makeCamera(PlanPoint at, double angle) const
{
    CameraPlacement camera;
    camera.model.name = "Учебная модель";
    camera.model.rangeMeters = 20.0;
    camera.model.horizontalFovDegrees = 90.0;
    camera.model.verticalFovDegrees = vfov_->value();

    camera.pose.position = at;
    camera.pose.heightMeters = cameraZ_->value();
    camera.pose.azimuthDegrees = angle;
    camera.pose.tiltDegrees = tilt_->value();

    return camera;
}

// Привязывает камеру к фасаду и фиксирует направление обзора.
void MainWindow::placeCamera(PlanPoint position)
{
    if (!cameraOrigin_)
    {
        constexpr double snapMeters = 0.5;

        cameraOrigin_ = VisibilityCalculator::mountPoint(
            position, project_.buildings(), snapMeters);

        if (!cameraOrigin_)
        {
            statusBar()->showMessage(
                tr("Камера внутри здания. Выберите фасад или наружную точку."));
            return;
        }

        planView_->previewCamera(makeCamera(*cameraOrigin_, 0.0),
                                 project_, targetZ_->value());

        statusBar()->showMessage(
            tr("Поверните камеру. Отпустите кнопку или щёлкните для установки."));
        return;
    }

    const auto angle = VisibilityCalculator::bearing(*cameraOrigin_, position);

    if (!angle)
    {
        statusBar()->showMessage(tr("Отведите курсор от точки крепления."));
        return;
    }

    const auto id = project_.addCamera(makeCamera(*cameraOrigin_, *angle));

    cameraOrigin_.reset();
    planView_->hidePreview();

    if (!id)
    {
        statusBar()->showMessage(tr("Недопустимые параметры установки."));
        return;
    }

    rememberObject(tr("Камера"), *id);

    cameraList_->addItem(
        tr("Камера №%1").arg(static_cast<qulonglong>(*id)));

    cameraList_->setCurrentIndex(cameraList_->count() - 1);

    statusBar()->showMessage(tr("Камера установлена. Покрытие рассчитано."));
}