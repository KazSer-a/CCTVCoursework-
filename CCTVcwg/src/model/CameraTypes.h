#pragma once

#include "PlanObjects.h"

#include <cstdint>
#include <string>
#include <vector>

using CameraModelId = std::uint64_t;

// Характеристики модели камеры из каталога.
struct CameraModel
{
    CameraModelId id = 0;
    std::string name;
    std::uint64_t priceKopecks = 0;
    double rangeMeters = 0.0;
    double horizontalFovDegrees = 0.0;
    double verticalFovDegrees = 0.0;
};

// Положение и ориентация камеры.
struct CameraPose
{
    PlanPoint position;
    double heightMeters = 0.0;

    // Азимут отсчитывается по часовой стрелке от севера.
    double azimuthDegrees = 0.0;

    // Положительный наклон направлен вниз.
    double tiltDegrees = 0.0;
};

// Установленная камера с параметрами модели.
struct CameraPlacement
{
    CameraModel model;
    CameraPose pose;
    PlanObjectId id = 0;
};

// Контрольная точка для проверки покрытия.
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

// Результат подбора оборудования.
struct OptimizationResult
{
    OptimizationStatus status = OptimizationStatus::NotRun;
    std::vector<CameraPlacement> placements;
    std::uint64_t totalPriceKopecks = 0;
    std::vector<CoverageSample> uncoveredSamples;
};