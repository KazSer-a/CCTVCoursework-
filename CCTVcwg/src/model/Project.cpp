#include "Project.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
constexpr double maxCoordinateMeters = 100000.0;
constexpr size_t maxBuildingVertices = 2048;
constexpr size_t maxLineVertices = 65536;

// Проверяет границы локальных координат точки.
bool isValidPoint(const PlanPoint& point)
{
    return abs(point.xMeters) <= maxCoordinateMeters &&
           abs(point.yMeters) <= maxCoordinateMeters;
}

// Вершина контура в миллиметрах.
struct GridPoint
{
    int64_t x;
    int64_t y;
};

// Контур с целочисленными координатами.
using Ring = vector<GridPoint>;

// Определяет положение точки относительно направленной прямой AB.
int orientation(GridPoint a, GridPoint b, GridPoint p)
{
    const auto cross = (b.x - a.x) * (p.y - a.y) -
                       (b.y - a.y) * (p.x - a.x);

    return (cross > 0) - (cross < 0);
}

// Проверяет принадлежность точки ограничивающему прямоугольнику отрезка.
bool withinBounds(GridPoint a, GridPoint b, GridPoint p)
{
    return min(a.x, b.x) <= p.x && p.x <= max(a.x, b.x) &&
           min(a.y, b.y) <= p.y && p.y <= max(a.y, b.y);
}

// Проверяет пересечение двух отрезков, включая касание и наложение.
bool intersects(GridPoint a, GridPoint b, GridPoint c, GridPoint d)
{
    const int abc = orientation(a, b, c);
    const int abd = orientation(a, b, d);
    const int cda = orientation(c, d, a);
    const int cdb = orientation(c, d, b);

    return (abc == 0 && withinBounds(a, b, c)) ||
           (abd == 0 && withinBounds(a, b, d)) ||
           (cda == 0 && withinBounds(c, d, a)) ||
           (cdb == 0 && withinBounds(c, d, b)) ||
           (abc * abd < 0 && cda * cdb < 0);
}

// Проверяет контур на нулевые рёбра, обратный ход и самопересечения.
bool isSimpleRing(const Ring& ring)
{
    const size_t n = ring.size();

    for (size_t i = 0; i < n; ++i)
    {
        const GridPoint a = ring[i];
        const GridPoint b = ring[(i + 1) % n];
        const GridPoint c = ring[(i + 2) % n];

        if (a.x == b.x && a.y == b.y)
            return false;

        if (orientation(a, b, c) == 0 && !withinBounds(a, c, b))
            return false;

        for (size_t j = i + 2; j < n; ++j)
        {
            // Первое и последнее рёбра имеют общую вершину.
            if (i == 0 && j == n - 1)
                continue;

            if (intersects(a, b, ring[j], ring[(j + 1) % n]))
                return false;
        }
    }

    return true;
}

// Округляет координаты до миллиметра и проверяет полученный контур.
bool readRing(vector<PlanPoint>& points, Ring& ring)
{
    ring.reserve(points.size());

    for (auto& point : points)
    {
        if (!isValidPoint(point))
            return false;

        const GridPoint p{
            llround(point.xMeters * 1000),
            llround(point.yMeters * 1000)
        };

        ring.push_back(p);
        point = {p.x / 1000.0, p.y / 1000.0};
    }

    return isSimpleRing(ring);
}

// Проверяет наличие общих точек у границ двух контуров.
bool boundariesIntersect(const Ring& first, const Ring& second)
{
    for (size_t i = 0; i < first.size(); ++i)
    {
        for (size_t j = 0; j < second.size(); ++j)
        {
            if (intersects(first[i], first[(i + 1) % first.size()],
                           second[j], second[(j + 1) % second.size()]))
                return true;
        }
    }

    return false;
}

// Проверяет положение точки внутри контура после исключения касания границы.
bool inside(const Ring& ring, GridPoint point)
{
    bool result = false;

    for (size_t i = 0; i < ring.size(); ++i)
    {
        const GridPoint a = ring[i];
        const GridPoint b = ring[(i + 1) % ring.size()];

        if ((a.y > point.y) == (b.y > point.y))
            continue;

        if ((orientation(a, b, point) > 0) == (b.y > a.y))
            result = !result;
    }

    return result;
}

// Проверяет внешний контур части здания и расположение её внутренних дворов.
bool validateFootprint(BuildingFootprint& footprint)
{
    Ring outer;
    if (!readRing(footprint.outerRing, outer))
        return false;

    vector<Ring> holes;

    for (auto& points : footprint.innerRings)
    {
        Ring hole;
        if (!readRing(points, hole) || boundariesIntersect(outer, hole) ||
            !inside(outer, hole.front()))
            return false;

        for (const auto& other : holes)
        {
            if (boundariesIntersect(other, hole) || inside(other, hole.front()) ||
                inside(hole, other.front()))
                return false;
        }

        holes.push_back(std::move(hole));
    }

    return true;
}

// Проверяет число вершин контура и уменьшает остаток лимита здания.
bool accountRing(size_t vertices, size_t& remaining)
{
    if (vertices < 3 || vertices > remaining)
        return false;

    remaining -= vertices;
    return true;
}
}

// Проверяет здание, добавляет его в проект и возвращает присвоенный ID.
optional<PlanObjectId> Project::addBuilding(Building building)
{
    if (nextId_ == 0 || building.footprints.empty())
        return nullopt;

    if (building.heightMeters &&
        (!isfinite(*building.heightMeters) || *building.heightMeters <= 0.0))
        return nullopt;

    size_t remaining = maxBuildingVertices;

    for (const auto& footprint : building.footprints)
    {
        if (!accountRing(footprint.outerRing.size(), remaining))
            return nullopt;

        for (const auto& ring : footprint.innerRings)
        {
            if (!accountRing(ring.size(), remaining))
                return nullopt;
        }
    }

    for (auto& footprint : building.footprints)
    {
        if (!validateFootprint(footprint))
            return nullopt;
    }

    building.id = nextId_;
    buildings_.push_back(std::move(building));
    return nextId_++;
}

// Проверяет цель наблюдения, добавляет её в проект и возвращает присвоенный ID.
optional<PlanObjectId> Project::addTarget(ObservationTarget target)
{
    if (nextId_ == 0)
        return nullopt;

    if (const auto* point = get_if<ObservationPoint>(&target.geometry))
    {
        if (!isValidPoint(point->position) ||
            !isfinite(point->heightMeters) || point->heightMeters < 0.0)
            return nullopt;
    }
    else
    {
        const auto& line = get<ObservationLine>(target.geometry);

        if (line.centerline.size() < 2 || line.centerline.size() > maxLineVertices ||
            !all_of(line.centerline.begin(), line.centerline.end(), isValidPoint) ||
            !isfinite(line.heightMeters) || line.heightMeters < 0.0 ||
            !isfinite(line.sampleStepMeters) || line.sampleStepMeters <= 0.0)
            return nullopt;

        const PlanPoint first = line.centerline.front();

        // Линия имеет нулевую длину, если все её точки совпадают.
        if (all_of(line.centerline.begin(), line.centerline.end(), [first](const PlanPoint& p) {
                return p.xMeters == first.xMeters && p.yMeters == first.yMeters;
            }))
            return nullopt;
    }

    target.id = nextId_;
    targets_.push_back(std::move(target));
    return nextId_++;
}

// Возвращает коллекцию зданий проекта для чтения.
const vector<Building>& Project::buildings() const
{
    return buildings_;
}

// Возвращает коллекцию целей наблюдения для чтения.
const vector<ObservationTarget>& Project::targets() const
{
    return targets_;
}