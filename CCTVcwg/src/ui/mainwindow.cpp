#include "mainwindow.h"

#include "PlanView.h"
#include "ui_mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <variant>

// Размещает область плана и подключает команды редактора.
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
            this, &MainWindow::updateBuildingPreview);

    setupDrawingTools();

    auto* cancelShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(cancelShortcut, &QShortcut::activated,
            this, &MainWindow::cancelBuilding);

    auto* undoShortcut = new QShortcut(QKeySequence(QKeySequence::Undo), this);
    undoShortcut->setAutoRepeat(false);
    connect(undoShortcut, &QShortcut::activated,
            this, &MainWindow::undoLastAction);

    selectDrawingTool(DrawingTool::ObservationPoint);
}

// Освобождает объект формы главного окна.
MainWindow::~MainWindow()
{
    delete ui;
}

// Размещает взаимоисключающие инструменты на панели редактора.
void MainWindow::setupDrawingTools()
{
    auto* toolbar = addToolBar(tr("Инструменты плана"));
    auto* tools = new QActionGroup(toolbar);

    auto* pointAction = toolbar->addAction(tr("Точка наблюдения"));
    pointAction->setCheckable(true);
    tools->addAction(pointAction);

    auto* buildingAction = toolbar->addAction(tr("Здание"));
    buildingAction->setCheckable(true);
    tools->addAction(buildingAction);

    pointAction->setChecked(true);

    connect(pointAction, &QAction::toggled, this, [this](bool checked) {
        if (checked)
            selectDrawingTool(DrawingTool::ObservationPoint);
    });

    connect(buildingAction, &QAction::toggled, this, [this](bool checked) {
        if (checked)
            selectDrawingTool(DrawingTool::Building);
    });
}

// Сбрасывает первый угол и выводит подсказку выбранного инструмента.
void MainWindow::selectDrawingTool(DrawingTool tool)
{
    drawingTool_ = tool;
    firstBuildingCorner_.reset();
    planView_->hideBuildingPreview();

    if (drawingTool_ == DrawingTool::Building)
        statusBar()->showMessage(tr("Укажите первый угол здания."));
    else
        statusBar()->showMessage(tr("Укажите положение точки наблюдения."));
}

// Передаёт координаты щелчка активному инструменту.
void MainWindow::handlePlanClick(PlanPoint position)
{
    if (drawingTool_ == DrawingTool::Building)
        addBuildingCorner(position);
    else
        addObservationPoint(position);
}

// Показывает предварительный контур при выборе второго угла.
void MainWindow::updateBuildingPreview(PlanPoint position)
{
    if (firstBuildingCorner_)
        planView_->showBuildingPreview(*firstBuildingCorner_, position);
}

// Сбрасывает первый угол и скрывает предварительный контур.
void MainWindow::cancelBuilding()
{
    if (!firstBuildingCorner_)
        return;

    firstBuildingCorner_.reset();
    planView_->hideBuildingPreview();

    statusBar()->showMessage(
        tr("Построение отменено. Укажите первый угол здания."));
}

// Отменяет построение или удаляет последний добавленный объект.
void MainWindow::undoLastAction()
{
    if (firstBuildingCorner_)
    {
        cancelBuilding();
        return;
    }

    if (!project_.undoLastAddition())
    {
        statusBar()->showMessage(tr("Нет действий для отмены."));
        return;
    }

    redrawPlan();
    statusBar()->showMessage(tr("Последнее добавление отменено."));
}

// Отображает здания и точечные цели из модели проекта.
void MainWindow::redrawPlan()
{
    planView_->clearPlan();

    for (const Building& building : project_.buildings())
        planView_->drawBuilding(building);

    for (const ObservationTarget& target : project_.targets())
    {
        if (const auto* point = std::get_if<ObservationPoint>(&target.geometry))
            planView_->drawObservationPoint(point->position);
    }
}

// Добавляет цель в модель и отображает её после успешной проверки.
void MainWindow::addObservationPoint(PlanPoint position)
{
    ObservationTarget target;
    target.label = "Точка наблюдения";
    target.geometry = ObservationPoint{position, 0.0};

    const auto id = project_.addTarget(std::move(target));

    if (!id)
    {
        statusBar()->showMessage(tr("Недопустимые параметры цели."));
        return;
    }

    planView_->drawObservationPoint(position);

    statusBar()->showMessage(
        tr("Цель №%1   X = %2 м   Y = %3 м")
            .arg(static_cast<qulonglong>(*id))
            .arg(position.xMeters, 0, 'f', 2)
            .arg(position.yMeters, 0, 'f', 2));
}

// Формирует контур здания по двум противоположным углам.
void MainWindow::addBuildingCorner(PlanPoint position)
{
    if (!firstBuildingCorner_)
    {
        firstBuildingCorner_ = position;
        statusBar()->showMessage(
            tr("Укажите противоположный угол здания. Esc — отмена."));
        return;
    }

    const PlanPoint first = *firstBuildingCorner_;
    const double left = std::min(first.xMeters, position.xMeters);
    const double right = std::max(first.xMeters, position.xMeters);
    const double bottom = std::min(first.yMeters, position.yMeters);
    const double top = std::max(first.yMeters, position.yMeters);

    if (left == right || bottom == top)
    {
        statusBar()->showMessage(
            tr("Площадь здания должна быть больше нуля. Укажите другой угол."));
        return;
    }

    BuildingFootprint footprint;
    footprint.outerRing = {
        {left, bottom},
        {right, bottom},
        {right, top},
        {left, top}
    };

    Building building;
    building.footprints.push_back(std::move(footprint));

    const auto id = project_.addBuilding(std::move(building));

    if (!id)
    {
        statusBar()->showMessage(
            tr("Модель отклонила контур здания. Укажите другой угол."));
        return;
    }

    firstBuildingCorner_.reset();
    planView_->hideBuildingPreview();
    planView_->drawBuilding(project_.buildings().back());

    statusBar()->showMessage(
        tr("Здание №%1 добавлено. Укажите первый угол следующего здания.")
            .arg(static_cast<qulonglong>(*id)));
}