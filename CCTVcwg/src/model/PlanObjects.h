#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using PlanObjectId = std::uint64_t;

// Точка в локальной системе координат плана.
struct PlanPoint
{
    double xMeters = 0.0;
    double yMeters = 0.0;
};

// Прямолинейный отрезок на плане.
struct PlanSegment
{
    PlanPoint start;
    PlanPoint end;
};

// Контур одной части здания с учётом внутренних дворов.
struct BuildingFootprint
{
    // В каждом контуре первая вершина в конце не дублируется.
    std::vector<PlanPoint> outerRing;
    std::vector<std::vector<PlanPoint>> innerRings;
};

// Здание на плане и его высота.
struct Building
{
    PlanObjectId id = 0;
    std::vector<BuildingFootprint> footprints;

    // Отсутствие значения означает, что высота неизвестна.
    std::optional<double> heightMeters;
};

// Прямолинейный участок ограждения.
struct Fence
{
    PlanObjectId id = 0;
    PlanSegment segment;
    double heightMeters = 0.0;
};

// Осевая линия дороги.
struct Road
{
    PlanObjectId id = 0;
    std::vector<PlanPoint> centerline;
    double widthMeters = 4.0;
};

// Координаты и высота точечной цели.
struct ObservationPoint
{
    PlanPoint position;
    double heightMeters = 0.0;
};

// Линия, вдоль которой проверяется видимость.
struct ObservationLine
{
    std::vector<PlanPoint> centerline;
    double heightMeters = 0.0;
    double sampleStepMeters = 1.0;
};

// Именованная точечная или линейная цель наблюдения.
struct ObservationTarget
{
    PlanObjectId id = 0;
    std::string label;
    std::variant<ObservationPoint, ObservationLine> geometry;
};

// Точка или отрезок, где разрешён монтаж камеры.
struct MountZone
{
    PlanObjectId id = 0;
    std::variant<PlanPoint, PlanSegment> geometry;
    std::vector<double> allowedHeightsMeters;
    double placementStepMeters = 1.0;

    // При монтаже на фасаде указывает на здание.
    // В остальных случаях значение отсутствует.
    std::optional<PlanObjectId> buildingId;
};

// Параметры монтажа камеры на отдельной опоре.
struct MountPole
{
    PlanObjectId id = 0;
    PlanPoint position;
    double heightMeters = 3.0;
};

// Индекс ребра внешнего контура или внутреннего двора.
struct FacadeEdge
{
    PlanObjectId buildingId = 0;
    std::size_t footprint = 0;
    std::size_t ring = 0;
    std::size_t edge = 0;
};

// Запрещённый интервал вдоль ребра в долях его длины.
struct FacadeBan
{
    PlanObjectId id = 0;
    FacadeEdge facade;
    double from = 0.0;
    double to = 1.0;
};

// Географические координаты начала локальной системы плана.
struct MapReference
{
    double originLatitudeDegrees = 0.0;
    double originLongitudeDegrees = 0.0;
};

