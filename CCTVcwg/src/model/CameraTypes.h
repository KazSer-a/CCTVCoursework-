#pragma once

#include "PlanObjects.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace std;

using CameraModelId = uint64_t;

// Характеристики и стоимость модели камеры из каталога.
struct CameraModel
{
    CameraModelId id = 0;
    string name;
    uint64_t priceKopecks = 0;
    double rangeMeters = 0.0;
    double horizontalFovDegrees = 0.0;
    double verticalFovDegrees = 0.0;
};

// Положение и ориентация камеры на плане.
struct CameraPose
{
    PlanPoint position;
    double heightMeters = 0.0;

    // Азимут отсчитывается по часовой стрелке от севера.
    double azimuthDegrees = 0.0;

    // Положительный наклон направлен вниз.
    double tiltDegrees = 0.0;
};

// Установка камеры с характеристиками модели на момент расчёта.
struct CameraPlacement
{
    CameraModel model;
    CameraPose pose;
};

// Контрольная точка, для которой проверяется видимость.
struct CoverageSample
{
    PlanObjectId targetId = 0;
    PlanPoint position;
    double heightMeters = 0.0;
};

// Состояние расчёта комплекта камер.
enum class OptimizationStatus
{
    NotRun,
    Optimal,
    Feasible,
    Infeasible,
    TimedOut,
    Cancelled
};

// Результат подбора камер и проверки покрытия.
struct OptimizationResult
{
    OptimizationStatus status = OptimizationStatus::NotRun;
    vector<CameraPlacement> placements;
    uint64_t totalPriceKopecks = 0;
    vector<CoverageSample> uncoveredSamples;
};