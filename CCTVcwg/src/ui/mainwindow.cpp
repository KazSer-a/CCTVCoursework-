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
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QString>
#include <QToolBar>
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

    // Завершает установку камеры при отпускании кнопки мыши.
    connect(planView_, &PlanView::dragFinished, this, [this](PlanPoint point) {
        if (drawingTool_ == DrawingTool::Camera && cameraOrigin_)
            placeCamera(point);
    });

    setupHeights();
    setupDrawingTools();

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

    // Добавляет кнопку инструмента на панель.
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

    auto* select = addTool(tr("Выбор"), DrawingTool::Select);
    addTool(tr("Точка наблюдения"), DrawingTool::ObservationPoint);
    addTool(tr("Здание"), DrawingTool::Building);
    addTool(tr("Забор"), DrawingTool::Fence);
    addTool(tr("Камера"), DrawingTool::Camera);

    toolbar->addSeparator();
    cameraList_ = new QComboBox(toolbar);
    cameraList_->setToolTip(tr("Камера для расчёта покрытия"));
    cameraList_->addItem(tr("Без выделения"));
    toolbar->addWidget(cameraList_);

    connect(cameraList_, &QComboBox::currentIndexChanged,
            this, &MainWindow::redrawPlan);

    select->setChecked(true);
}

// Создаёт инспектор параметров инструмента или выбранного объекта.
void MainWindow::setupHeights()
{
    auto* dock = new QDockWidget(tr("Свойства"), this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);

    propertiesTitle_ = new QLabel(panel);
    auto titleFont = propertiesTitle_->font();
    titleFont.setBold(true);
    propertiesTitle_->setFont(titleFont);
    layout->addWidget(propertiesTitle_);

    properties_ = new QFormLayout;
    layout->addLayout(properties_);

    // Добавляет числовое поле с единицей измерения.
    const auto field = [&](const QString& label, double low, double high,
                           double value, const QString& unit, int decimals = 2) {
        auto* input = new QDoubleSpinBox(panel);
        input->setDecimals(decimals);
        input->setRange(low, high);
        input->setValue(value);
        input->setSuffix(unit);
        input->setKeyboardTracking(false);
        properties_->addRow(label, input);
        return input;
    };

    const QString meters = tr(" м");
    cameraZ_ = field(tr("Высота"), 0.0, 1000.0, 4.0, meters);
    tilt_ = field(tr("Наклон вниз"), -90.0, 90.0, 12.0, tr("°"));
    vfov_ = field(tr("Вертикальный угол"), 0.1, 180.0, 60.0, tr("°"));
    targetZ_ = field(tr("Высота"), 0.0, 1000.0, 1.5, meters);
    buildingZ_ = field(tr("Высота"), 0.01, 1000.0, 8.0, meters);
    fenceZ_ = field(tr("Высота"), 0.01, 1000.0, 2.0, meters);
    sliceZ_ = field(tr("Высота среза"), 0.0, 1000.0, 1.5, meters);
    editWidth_ = field(tr("Ширина"), 0.001, 200000.0, 1.0, meters, 3);
    editLength_ = field(tr("Длина"), 0.001, 200000.0, 1.0, meters, 3);
    editHeight_ = field(tr("Высота"), 0.0, 1000.0, 0.0, meters, 3);

    propertiesMessage_ = new QLabel(panel);
    propertiesMessage_->setWordWrap(true);
    layout->addWidget(propertiesMessage_);

    apply_ = new QPushButton(tr("Применить"), panel);
    layout->addWidget(apply_);
    layout->addStretch();

    connect(apply_, &QPushButton::clicked, this, &MainWindow::applyProperties);
    connect(editWidth_, &QDoubleSpinBox::valueChanged, this, &MainWindow::previewResize);
    connect(editLength_, &QDoubleSpinBox::valueChanged, this, &MainWindow::previewResize);
    connect(sliceZ_, &QDoubleSpinBox::valueChanged, this, &MainWindow::redrawPlan);

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

// Переключает инструмент и завершает текущее взаимодействие.
void MainWindow::selectDrawingTool(DrawingTool tool)
{
    drawingTool_ = tool;
    selectedId_ = 0;
    cameraOrigin_.reset();
    firstBuildingCorner_.reset();
    fenceStart_.reset();

    updateInspector();
    redrawPlan();

    switch (tool)
    {
    case DrawingTool::Select:
        statusBar()->showMessage(tr("Щёлкните по объекту на плане."));
        break;

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
        statusBar()->showMessage(
            tr("Зажмите левую кнопку и выберите направление."));
        break;
    }
}

// Передаёт точку активному инструменту.
void MainWindow::handlePlanClick(PlanPoint position)
{
    switch (drawingTool_)
    {
    case DrawingTool::Select:
        selectObject(position);
        break;

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

// Обновляет предварительное изображение строящегося объекта.
void MainWindow::updateDrawingPreview(PlanPoint position)
{
    if (firstBuildingCorner_)
    {
        planView_->showBuildingPreview(*firstBuildingCorner_, position);
        return;
    }

    if (fenceStart_)
    {
        const PlanSegment segment{*fenceStart_, position};
        planView_->showFencePreview(segment, project_.canPlaceFence(segment));
        return;
    }

    if (cameraOrigin_)
    {
        const auto angle = VisibilityCalculator::bearing(*cameraOrigin_, position);

        if (angle)
            planView_->previewCamera(makeCamera(*cameraOrigin_, *angle),
                                     project_, sliceZ_->value());
    }
}

// Отменяет незавершённое построение или снимает выделение.
void MainWindow::cancelDrawing()
{
    if (firstBuildingCorner_ || cameraOrigin_ || fenceStart_ || selectedId_ != 0)
        selectDrawingTool(drawingTool_);
}

// Отменяет строящийся объект или последнее добавление.
void MainWindow::undoLastAction()
{
    if (firstBuildingCorner_ || cameraOrigin_ || fenceStart_)
    {
        cancelDrawing();
        return;
    }

    if (!project_.undoLastAddition())
    {
        statusBar()->showMessage(tr("Нет добавлений для отмены."));
        return;
    }

    if (cameraList_->count() - 1 > static_cast<int>(project_.cameras().size()))
    {
        const QSignalBlocker blocker(cameraList_);
        cameraList_->removeItem(cameraList_->count() - 1);
    }

    selectedId_ = 0;
    updateInspector();
    redrawPlan();
    statusBar()->showMessage(tr("Последнее добавление отменено."));
}

// Возвращает камеру по строке списка покрытия.
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
        planView_->drawBuilding(building, building.id == selectedId_);

    for (const auto& fence : project_.fences())
        planView_->drawFence(fence);

    const auto* selected = selectedCamera();

    for (const auto& camera : project_.cameras())
        planView_->drawCamera(camera, &camera == selected, project_, sliceZ_->value());

    for (const auto& target : project_.targets())
    {
        if (const auto* point = std::get_if<ObservationPoint>(&target.geometry))
        {
            const bool covered = selected && VisibilityCalculator::visible(
                                     *selected, *point, project_.buildings(), project_.fences());

            planView_->drawObservationPoint(point->position, covered, target.id);
        }
    }
}

// Создаёт цель на высоте, выбранной в панели свойств.
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

    const PlanPoint first = *firstBuildingCorner_;
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
            tr("Проверьте контур здания, расположение камер и ограждений."));
        return;
    }

    firstBuildingCorner_.reset();
    redrawPlan();
    statusBar()->showMessage(tr("Здание добавлено. Укажите первый угол следующего."));
}

// Создаёт прямолинейное ограждение заданной высоты.
void MainWindow::addFencePoint(PlanPoint point)
{
    if (!fenceStart_)
    {
        fenceStart_ = point;
        planView_->showFencePreview({point, point}, true);
        statusBar()->showMessage(tr("Укажите конец ограждения. Esc — отмена."));
        return;
    }

    Fence fence;
    fence.segment = {*fenceStart_, point};
    fence.heightMeters = fenceZ_->value();

    const auto id = project_.addFence(fence);

    if (!id)
    {
        planView_->showFencePreview(fence.segment, false);
        statusBar()->showMessage(
            tr("Выберите отрезок ненулевой длины вне стен и контура здания."));
        return;
    }

    fenceStart_.reset();
    redrawPlan();
    statusBar()->showMessage(tr("Ограждение добавлено."));
}

// Формирует камеру по параметрам панели.
CameraPlacement MainWindow::makeCamera(PlanPoint position, double angle) const
{
    CameraPlacement camera;
    camera.model.name = "Учебная модель";
    camera.model.rangeMeters = 20.0;
    camera.model.horizontalFovDegrees = 90.0;
    camera.model.verticalFovDegrees = vfov_->value();

    camera.pose.position = position;
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
                                 project_, sliceZ_->value());

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

    cameraList_->addItem(tr("Камера №%1").arg(static_cast<qulonglong>(*id)));
    cameraList_->setCurrentIndex(cameraList_->count() - 1);

    statusBar()->showMessage(tr("Камера установлена. Покрытие рассчитано."));
}

// Выбирает объект плана и обновляет его свойства.
void MainWindow::selectObject(PlanPoint point)
{
    selectedId_ = planView_->objectAt(point);

    const auto& cameras = project_.cameras();
    const auto camera = std::find_if(
        cameras.begin(), cameras.end(),
        [this](const CameraPlacement& item) { return item.id == selectedId_; });

    if (camera != cameras.end())
    {
        const QSignalBlocker blocker(cameraList_);
        cameraList_->setCurrentIndex(static_cast<int>(camera - cameras.begin()) + 1);
    }

    updateInspector();
    redrawPlan();
}

// Возвращает здание по идентификатору выделения.
const Building* MainWindow::selectedBuilding() const
{
    const auto& buildings = project_.buildings();
    const auto found = std::find_if(
        buildings.begin(), buildings.end(),
        [this](const Building& item) { return item.id == selectedId_; });

    return found != buildings.end() ? &*found : nullptr;
}

// Показывает параметры активного инструмента или выбранного объекта.
void MainWindow::updateInspector()
{
    for (auto* input : {cameraZ_, tilt_, vfov_, targetZ_, buildingZ_, fenceZ_,
                        sliceZ_, editWidth_, editLength_, editHeight_})
    {
        properties_->setRowVisible(input, false);
    }

    apply_->hide();
    propertiesMessage_->clear();
    propertiesMessage_->hide();

    switch (drawingTool_)
    {
    case DrawingTool::Building:
        propertiesTitle_->setText(tr("Новое здание"));
        properties_->setRowVisible(buildingZ_, true);
        return;

    case DrawingTool::Fence:
        propertiesTitle_->setText(tr("Новый забор"));
        properties_->setRowVisible(fenceZ_, true);
        return;

    case DrawingTool::ObservationPoint:
        propertiesTitle_->setText(tr("Новая точка наблюдения"));
        properties_->setRowVisible(targetZ_, true);
        return;

    case DrawingTool::Camera:
        propertiesTitle_->setText(tr("Новая камера"));

        for (auto* input : {cameraZ_, tilt_, vfov_, sliceZ_})
            properties_->setRowVisible(input, true);

        return;

    case DrawingTool::Select:
        break;
    }

    QString name;
    double height = 0.0;
    double minimum = 0.0;

    if (const auto* building = selectedBuilding())
    {
        name = tr("Здание");
        height = building->heightMeters.value_or(buildingZ_->value());
        minimum = 0.001;

        if (!building->heightMeters)
        {
            propertiesMessage_->setText(
                tr("Высота здания не задана. Укажите значение перед сохранением."));
            propertiesMessage_->show();
        }

        if (const auto bounds = Project::rectangleBounds(*building))
        {
            const QSignalBlocker blockWidth(editWidth_);
            const QSignalBlocker blockLength(editLength_);

            editWidth_->setValue(bounds->end.xMeters - bounds->start.xMeters);
            editLength_->setValue(bounds->end.yMeters - bounds->start.yMeters);

            properties_->setRowVisible(editWidth_, true);
            properties_->setRowVisible(editLength_, true);
        }
    }

    for (const auto& fence : project_.fences())
    {
        if (fence.id == selectedId_)
        {
            name = tr("Забор");
            height = fence.heightMeters;
            minimum = 0.001;
        }
    }

    for (const auto& target : project_.targets())
    {
        if (target.id == selectedId_)
        {
            name = tr("Точка наблюдения");
            height = std::visit(
                [](const auto& shape) { return shape.heightMeters; }, target.geometry);
        }
    }

    for (const auto& camera : project_.cameras())
    {
        if (camera.id == selectedId_)
        {
            name = tr("Камера");
            height = camera.pose.heightMeters;
            properties_->setRowVisible(sliceZ_, true);
        }
    }

    if (name.isEmpty())
    {
        propertiesTitle_->setText(tr("Выберите объект на плане"));
        return;
    }

    propertiesTitle_->setText(
        tr("%1 №%2").arg(name).arg(static_cast<qulonglong>(selectedId_)));

    editHeight_->setRange(minimum, std::max(1000.0, height));
    editHeight_->setValue(height);
    properties_->setRowVisible(editHeight_, true);
    apply_->show();
}

// Рисует предполагаемый контур с закреплённым нижним левым углом.
void MainWindow::previewResize()
{
    const auto* building = selectedBuilding();

    if (!building)
        return;

    const auto bounds = Project::rectangleBounds(*building);

    if (!bounds)
        return;

    const PlanPoint origin = bounds->start;
    const PlanPoint end{
        origin.xMeters + editWidth_->value(),
        origin.yMeters + editLength_->value()
    };

    planView_->showBuildingPreview(origin, end);
}

// Сохраняет свойства и пересчитывает покрытие после проверки.
void MainWindow::applyProperties()
{
    if (selectedId_ == 0)
        return;

    const auto* building = selectedBuilding();
    bool saved = false;

    if (building && Project::rectangleBounds(*building))
    {
        saved = project_.resizeBuilding(
            selectedId_, editWidth_->value(), editLength_->value(), editHeight_->value());
    }
    else
    {
        saved = project_.setHeight(selectedId_, editHeight_->value());
    }

    if (!saved)
    {
        propertiesMessage_->setText(
            tr("Проверьте размеры и высоту. Новый контур должен проходить вне "
               "ограждений и установленных камер."));
        propertiesMessage_->show();
        planView_->hidePreview();
        return;
    }

    updateInspector();
    redrawPlan();
    statusBar()->showMessage(tr("Свойства сохранены."), 3000);
}