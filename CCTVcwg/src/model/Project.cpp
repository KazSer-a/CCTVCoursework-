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

// Вершина контура в миллиметрах.
struct GridPoint
{
    std::int64_t x;
    std::int64_t y;
};

// Контур в целочисленных координатах.
using Ring = std::vector<GridPoint>;

// Определяет положение точки относительно направленной прямой AB.
int orientation(GridPoint a, GridPoint b, GridPoint p)
{
    const auto cross = (b.x - a.x) * (p.y - a.y) -
                       (b.y - a.y) * (p.x - a.x);

    return (cross > 0) - (cross < 0);
}

// Проверяет принадлежность ограничивающему прямоугольнику.
bool withinBounds(GridPoint a, GridPoint b, GridPoint p)
{
    return std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) &&
           std::min(a.y, b.y) <= p.y && p.y <= std::max(a.y, b.y);
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

// Проверяет рёбра и самопересечения контура.
bool isSimpleRing(const Ring& ring)
{
    const std::size_t n = ring.size();

    for (std::size_t i = 0; i < n; ++i)
    {
        const GridPoint a = ring[i];
        const GridPoint b = ring[(i + 1) % n];
        const GridPoint c = ring[(i + 2) % n];

        if (a.x == b.x && a.y == b.y)
            return false;

        if (orientation(a, b, c) == 0 && !withinBounds(a, c, b))
            return false;

        for (std::size_t j = i + 2; j < n; ++j)
        {
            if (i == 0 && j == n - 1)
                continue;

            if (intersects(a, b, ring[j], ring[(j + 1) % n]))
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

        const GridPoint p{
            std::llround(point.xMeters * 1000.0),
            std::llround(point.yMeters * 1000.0)
        };

        ring.push_back(p);
        point = {p.x / 1000.0, p.y / 1000.0};
    }

    return isSimpleRing(ring);
}

// Проверяет пересечение границ двух контуров.
bool boundariesIntersect(const Ring& first, const Ring& second)
{
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        for (std::size_t j = 0; j < second.size(); ++j)
        {
            if (intersects(first[i], first[(i + 1) % first.size()],
                           second[j], second[(j + 1) % second.size()]))
                return true;
        }
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
            return false;

        for (const Ring& other : holes)
        {
            if (boundariesIntersect(other, hole) ||
                inside(other, hole.front()) || inside(hole, other.front()))
                return false;
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
}

// Проверяет геометрию здания и расположение существующих камер.
std::optional<PlanObjectId> Project::addBuilding(Building building)
{
    if (nextId_ == 0 || building.footprints.empty())
        return std::nullopt;

    if (building.heightMeters &&
        (!std::isfinite(*building.heightMeters) || *building.heightMeters <= 0.0))
        return std::nullopt;

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

    if (!cameras_.empty())
    {
        const std::vector<Building> added{building};

        for (const auto& camera : cameras_)
        {
            if (!VisibilityCalculator::mountPoint(camera.pose.position, added, 0.0))
                return std::nullopt;
        }
    }

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
            return std::nullopt;
    }
    else
    {
        const auto& line = std::get<ObservationLine>(target.geometry);

        if (line.centerline.size() < 2 || line.centerline.size() > maxLineVertices ||
            !std::all_of(line.centerline.begin(), line.centerline.end(), isValidPoint) ||
            !std::isfinite(line.heightMeters) || line.heightMeters < 0.0 ||
            !std::isfinite(line.sampleStepMeters) || line.sampleStepMeters <= 0.0)
            return std::nullopt;

        const PlanPoint first = line.centerline.front();

        const bool collapsed = std::all_of(
            line.centerline.begin(), line.centerline.end(),
            [first](const PlanPoint& p) {
                return first.xMeters == p.xMeters && first.yMeters == p.yMeters;
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

// Проверяет концы и высоту ограждения.
std::optional<PlanObjectId> Project::addFence(Fence fence)
{
    const auto a = fence.segment.start;
    const auto b = fence.segment.end;

    if (nextId_ == 0 || !isValidPoint(a) || !isValidPoint(b) ||
        (a.xMeters == b.xMeters && a.yMeters == b.yMeters) ||
        !std::isfinite(fence.heightMeters) || fence.heightMeters <= 0.0)
        return std::nullopt;

    fence.id = nextId_;
    fences_.push_back(fence);
    return nextId_++;
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