#include "Project.h"
#include "../geometry/VisibilityCalculator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>

namespace
{
constexpr double maxCoordinateMeters = 100000.0;
constexpr std::size_t maxBuildingVertices = 2048;
constexpr std::size_t maxLineVertices = 65536;

// Проверяет допустимость координат точки.
bool isValidPoint(const PlanPoint& point)
{
    return std::abs(point.xMeters) <= maxCoordinateMeters &&
           std::abs(point.yMeters) <= maxCoordinateMeters;
}

// Представляет точку в целых миллиметрах.
struct GridPoint
{
    std::int64_t x;
    std::int64_t y;
};

using Ring = std::vector<GridPoint>;

// Переводит допустимую точку плана в миллиметры.
GridPoint toGrid(PlanPoint point)
{
    return {
        std::llround(point.xMeters * 1000.0),
        std::llround(point.yMeters * 1000.0)
    };
}

// Определяет положение точки относительно направленной прямой AB.
int orientation(GridPoint a, GridPoint b, GridPoint point)
{
    const auto cross = (b.x - a.x) * (point.y - a.y) -
                       (b.y - a.y) * (point.x - a.x);

    return (cross > 0) - (cross < 0);
}

// Проверяет принадлежность точки ограничивающему прямоугольнику отрезка.
bool withinBounds(GridPoint a, GridPoint b, GridPoint point)
{
    return std::min(a.x, b.x) <= point.x && point.x <= std::max(a.x, b.x) &&
           std::min(a.y, b.y) <= point.y && point.y <= std::max(a.y, b.y);
}

// Проверяет пересечение отрезков с учётом касания.
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

// Проверяет контур на нулевые рёбра и самопересечения.
bool isSimpleRing(const Ring& ring)
{
    const std::size_t count = ring.size();

    for (std::size_t i = 0; i < count; ++i)
    {
        const GridPoint a = ring[i];
        const GridPoint b = ring[(i + 1) % count];
        const GridPoint c = ring[(i + 2) % count];

        if (a.x == b.x && a.y == b.y)
            return false;

        if (orientation(a, b, c) == 0 && !withinBounds(a, c, b))
            return false;

        for (std::size_t j = i + 2; j < count; ++j)
        {
            // Первое и последнее рёбра имеют общую вершину.
            if (i == 0 && j == count - 1)
                continue;

            if (intersects(a, b, ring[j], ring[(j + 1) % count]))
                return false;
        }
    }

    return true;
}

// Округляет координаты до миллиметра и проверяет контур.
bool readRing(std::vector<PlanPoint>& points, Ring& ring)
{
    ring.reserve(points.size());

    for (auto& point : points)
    {
        if (!isValidPoint(point))
            return false;

        const GridPoint grid = toGrid(point);
        ring.push_back(grid);
        point = {grid.x / 1000.0, grid.y / 1000.0};
    }

    return isSimpleRing(ring);
}

// Проверяет касание или пересечение отрезка с контуром.
bool touchesRing(GridPoint a, GridPoint b, const Ring& ring)
{
    for (std::size_t i = 0; i < ring.size(); ++i)
    {
        if (intersects(a, b, ring[i], ring[(i + 1) % ring.size()]))
            return true;
    }

    return false;
}

// Проверяет пересечение границ двух контуров.
bool boundariesIntersect(const Ring& first, const Ring& second)
{
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        if (touchesRing(first[i], first[(i + 1) % first.size()], second))
            return true;
    }

    return false;
}

// Проверяет положение точки внутри контура после исключения касания.
bool inside(const Ring& ring, GridPoint point)
{
    bool result = false;

    for (std::size_t i = 0; i < ring.size(); ++i)
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

// Проверяет внешний контур здания и расположение дворов.
bool validateFootprint(BuildingFootprint& footprint)
{
    Ring outer;

    if (!readRing(footprint.outerRing, outer))
        return false;

    std::vector<Ring> holes;

    for (auto& points : footprint.innerRings)
    {
        Ring hole;

        if (!readRing(points, hole) || boundariesIntersect(outer, hole) ||
            !inside(outer, hole.front()))
        {
            return false;
        }

        for (const Ring& other : holes)
        {
            if (boundariesIntersect(other, hole) ||
                inside(other, hole.front()) || inside(hole, other.front()))
            {
                return false;
            }
        }

        holes.push_back(std::move(hole));
    }

    return true;
}

// Проверяет число вершин и учитывает остаток лимита.
bool accountRing(std::size_t vertices, std::size_t& remaining)
{
    if (vertices < 3 || vertices > remaining)
        return false;

    remaining -= vertices;
    return true;
}

// Проверяет пересечение ограждения с основанием здания или стеной двора.
bool conflicts(const PlanSegment& segment, const BuildingFootprint& part)
{
    const GridPoint a = toGrid(segment.start);
    const GridPoint b = toGrid(segment.end);

    // Создаёт целочисленное представление контура.
    const auto asGrid = [](const std::vector<PlanPoint>& points) {
        Ring ring;
        ring.reserve(points.size());

        for (PlanPoint point : points)
            ring.push_back(toGrid(point));

        return ring;
    };

    const Ring outer = asGrid(part.outerRing);

    if (touchesRing(a, b, outer))
        return true;

    if (!inside(outer, a))
        return false;

    for (const auto& points : part.innerRings)
    {
        const Ring hole = asGrid(points);

        if (touchesRing(a, b, hole))
            return true;

        if (inside(hole, a))
            return false;
    }

    return true;
}

// Проверяет расположение здания относительно камер и ограждений.
bool fitsPlan(const Building& building, const std::vector<CameraPlacement>& cameras,
              const std::vector<Fence>& fences)
{
    if (!cameras.empty())
    {
        const std::vector<Building> footprint{building};

        for (const auto& camera : cameras)
        {
            if (!VisibilityCalculator::mountPoint(camera.pose.position, footprint, 0.0))
                return false;
        }
    }

    for (const auto& fence : fences)
    {
        for (const auto& part : building.footprints)
        {
            if (conflicts(fence.segment, part))
                return false;
        }
    }

    return true;
}
}

// Проверяет геометрию здания и его положение на плане.
std::optional<PlanObjectId> Project::addBuilding(Building building)
{
    if (nextId_ == 0 || building.footprints.empty())
        return std::nullopt;

    if (building.heightMeters &&
        (!std::isfinite(*building.heightMeters) || *building.heightMeters <= 0.0))
    {
        return std::nullopt;
    }

    std::size_t remaining = maxBuildingVertices;

    for (const auto& part : building.footprints)
    {
        if (!accountRing(part.outerRing.size(), remaining))
            return std::nullopt;

        for (const auto& hole : part.innerRings)
        {
            if (!accountRing(hole.size(), remaining))
                return std::nullopt;
        }
    }

    for (auto& part : building.footprints)
    {
        if (!validateFootprint(part))
            return std::nullopt;
    }

    if (!fitsPlan(building, cameras_, fences_))
        return std::nullopt;

    building.id = nextId_;
    buildings_.push_back(std::move(building));
    return nextId_++;
}

// Проверяет и добавляет точечную или линейную цель.
std::optional<PlanObjectId> Project::addTarget(ObservationTarget target)
{
    if (nextId_ == 0)
        return std::nullopt;

    if (const auto* point = std::get_if<ObservationPoint>(&target.geometry))
    {
        if (!isValidPoint(point->position) ||
            !std::isfinite(point->heightMeters) || point->heightMeters < 0.0)
        {
            return std::nullopt;
        }
    }
    else
    {
        const auto& line = std::get<ObservationLine>(target.geometry);

        if (line.centerline.size() < 2 || line.centerline.size() > maxLineVertices ||
            !std::all_of(line.centerline.begin(), line.centerline.end(), isValidPoint) ||
            !std::isfinite(line.heightMeters) || line.heightMeters < 0.0 ||
            !std::isfinite(line.sampleStepMeters) || line.sampleStepMeters <= 0.0)
        {
            return std::nullopt;
        }

        const PlanPoint first = line.centerline.front();

        const bool collapsed = std::all_of(
            line.centerline.begin(), line.centerline.end(),
            [first](const PlanPoint& point) {
                return first.xMeters == point.xMeters &&
                       first.yMeters == point.yMeters;
            });

        if (collapsed)
            return std::nullopt;
    }

    target.id = nextId_;
    targets_.push_back(std::move(target));
    return nextId_++;
}

// Проверяет параметры и место установки камеры.
std::optional<PlanObjectId> Project::addCamera(CameraPlacement camera)
{
    const CameraModel& model = camera.model;
    const CameraPose& pose = camera.pose;

    if (nextId_ == 0 || !isValidPoint(pose.position) ||
        !std::isfinite(model.rangeMeters) || model.rangeMeters <= 0.0 ||
        !std::isfinite(model.horizontalFovDegrees) ||
        model.horizontalFovDegrees <= 0.0 || model.horizontalFovDegrees > 360.0 ||
        !std::isfinite(model.verticalFovDegrees) ||
        model.verticalFovDegrees <= 0.0 || model.verticalFovDegrees > 180.0 ||
        !std::isfinite(pose.azimuthDegrees) ||
        !std::isfinite(pose.heightMeters) || pose.heightMeters < 0.0 ||
        !std::isfinite(pose.tiltDegrees) || std::abs(pose.tiltDegrees) > 90.0)
    {
        return std::nullopt;
    }

    if (!VisibilityCalculator::mountPoint(pose.position, buildings_, 0.0))
        return std::nullopt;

    camera.id = nextId_;
    cameras_.push_back(std::move(camera));
    return nextId_++;
}

// Проверяет длину отрезка и его расположение относительно зданий.
bool Project::canPlaceFence(const PlanSegment& segment) const
{
    if (!isValidPoint(segment.start) || !isValidPoint(segment.end))
        return false;

    const GridPoint a = toGrid(segment.start);
    const GridPoint b = toGrid(segment.end);

    if (a.x == b.x && a.y == b.y)
        return false;

    for (const auto& building : buildings_)
    {
        for (const auto& part : building.footprints)
        {
            if (conflicts(segment, part))
                return false;
        }
    }

    return true;
}

// Проверяет и добавляет ограждение.
std::optional<PlanObjectId> Project::addFence(Fence fence)
{
    if (nextId_ == 0 || !std::isfinite(fence.heightMeters) ||
        fence.heightMeters <= 0.0 || !canPlaceFence(fence.segment))
    {
        return std::nullopt;
    }

    const GridPoint a = toGrid(fence.segment.start);
    const GridPoint b = toGrid(fence.segment.end);

    fence.segment = {{a.x / 1000.0, a.y / 1000.0},
                     {b.x / 1000.0, b.y / 1000.0}};
    fence.id = nextId_;
    fences_.push_back(fence);
    return nextId_++;
}

// Определяет границы прямоугольника, ориентированного вдоль осей плана.
std::optional<PlanSegment> Project::rectangleBounds(const Building& building)
{
    if (building.footprints.size() != 1)
        return std::nullopt;

    const auto& part = building.footprints.front();

    if (!part.innerRings.empty() || part.outerRing.size() != 4)
        return std::nullopt;

    PlanPoint low = part.outerRing.front();
    PlanPoint high = low;

    for (const PlanPoint point : part.outerRing)
    {
        low.xMeters = std::min(low.xMeters, point.xMeters);
        low.yMeters = std::min(low.yMeters, point.yMeters);
        high.xMeters = std::max(high.xMeters, point.xMeters);
        high.yMeters = std::max(high.yMeters, point.yMeters);
    }

    if (low.xMeters >= high.xMeters || low.yMeters >= high.yMeters)
        return std::nullopt;

    unsigned corners = 0;

    for (const PlanPoint point : part.outerRing)
    {
        if ((point.xMeters != low.xMeters && point.xMeters != high.xMeters) ||
            (point.yMeters != low.yMeters && point.yMeters != high.yMeters))
        {
            return std::nullopt;
        }

        const unsigned index = (point.xMeters == high.xMeters ? 1u : 0u) +
                               (point.yMeters == high.yMeters ? 2u : 0u);

        corners |= 1u << index;
    }

    if (corners != 15u)
        return std::nullopt;

    return PlanSegment{low, high};
}

// Изменяет прямоугольник относительно его нижнего левого угла.
bool Project::resizeBuilding(PlanObjectId id, double width, double length, double height)
{
    if (!std::isfinite(width) || !std::isfinite(length) || !std::isfinite(height) ||
        width <= 0.0 || length <= 0.0 || height <= 0.0)
    {
        return false;
    }

    for (Building& building : buildings_)
    {
        if (building.id != id)
            continue;

        const auto bounds = rectangleBounds(building);

        if (!bounds)
            return false;

        const PlanPoint origin = bounds->start;
        const PlanPoint end{origin.xMeters + width, origin.yMeters + length};

        Building changed = building;
        changed.heightMeters = height;

        auto& part = changed.footprints.front();
        part.outerRing = {
            origin,
            {end.xMeters, origin.yMeters},
            end,
            {origin.xMeters, end.yMeters}
        };

        if (!validateFootprint(part) || !fitsPlan(changed, cameras_, fences_))
            return false;

        building = std::move(changed);
        return true;
    }

    return false;
}

// Возвращает здания проекта.
const std::vector<Building>& Project::buildings() const
{
    return buildings_;
}

// Возвращает цели наблюдения.
const std::vector<ObservationTarget>& Project::targets() const
{
    return targets_;
}

// Возвращает камеры проекта.
const std::vector<CameraPlacement>& Project::cameras() const
{
    return cameras_;
}

// Возвращает ограждения проекта.
const std::vector<Fence>& Project::fences() const
{
    return fences_;
}

// Изменяет высоту объекта с указанным идентификатором.
bool Project::setHeight(PlanObjectId id, double meters)
{
    if (!std::isfinite(meters) || meters < 0.0)
        return false;

    for (auto& building : buildings_)
    {
        if (building.id == id && meters > 0.0)
        {
            building.heightMeters = meters;
            return true;
        }
    }

    for (auto& fence : fences_)
    {
        if (fence.id == id && meters > 0.0)
        {
            fence.heightMeters = meters;
            return true;
        }
    }

    for (auto& camera : cameras_)
    {
        if (camera.id == id)
        {
            camera.pose.heightMeters = meters;
            return true;
        }
    }

    for (auto& target : targets_)
    {
        if (target.id == id)
        {
            std::visit([meters](auto& geometry) {
                geometry.heightMeters = meters;
            }, target.geometry);

            return true;
        }
    }

    return false;
}

// Удаляет объект с наибольшим присвоенным идентификатором.
bool Project::undoLastAddition()
{
    const PlanObjectId buildingId = buildings_.empty() ? 0 : buildings_.back().id;
    const PlanObjectId targetId = targets_.empty() ? 0 : targets_.back().id;
    const PlanObjectId cameraId = cameras_.empty() ? 0 : cameras_.back().id;
    const PlanObjectId fenceId = fences_.empty() ? 0 : fences_.back().id;
    const PlanObjectId last = std::max({buildingId, targetId, cameraId, fenceId});

    if (last == 0)
        return false;

    if (last == fenceId)
        fences_.pop_back();
    else if (last == cameraId)
        cameras_.pop_back();
    else if (last == targetId)
        targets_.pop_back();
    else
        buildings_.pop_back();

    return true;
}