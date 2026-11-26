#pragma once

#include "../model/CameraTypes.h"

#include <optional>
#include <vector>

namespace VisibilityCalculator
{
// Видимый интервал вдоль луча в горизонтальной плоскости.
struct Span
{
    double nearMeters;
    double farMeters;
};

// Вычисляет азимут от севера по часовой стрелке.
std::optional<double> bearing(const PlanPoint& from, const PlanPoint& to);

// Проверяет горизонтальное поле зрения и дальность на плане.
bool inSector(const CameraPlacement& camera, const PlanPoint& target);

// Проверяет место установки и привязывает наружную точку к фасаду.
std::optional<PlanPoint> mountPoint(
    PlanPoint point, const std::vector<Building>& buildings, double snapMeters);

// Вычисляет видимые интервалы на заданной высоте и направлении.
std::vector<Span> visibleRay(
    const CameraPlacement& camera, double bearingDegrees, double targetHeight,
    const std::vector<Building>& buildings, const std::vector<Fence>& fences);

// Проверяет видимость конкретной точки с учётом её высоты.
bool visible(const CameraPlacement& camera, const ObservationPoint& target,
             const std::vector<Building>& buildings, const std::vector<Fence>& fences);
}