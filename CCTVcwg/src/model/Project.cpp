#include "Project.h"
#include "../geometry/VisibilityCalculator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
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

// Проверяет пересечение полосы дороги с основанием здания.
bool roadConflicts(const BuildingFootprint& road, const BuildingFootprint& building)
{
    Ring roadRing;
    for (const auto& point : road.outerRing)
        roadRing.push_back(toGrid(point));

    for (std::size_t i = 0; i < road.outerRing.size(); ++i)
    {
        const auto& points = road.outerRing;
        if (conflicts({points[i], points[(i + 1) % points.size()]}, building))
            return true;
    }

    return std::any_of(building.outerRing.begin(), building.outerRing.end(),
                       [&](PlanPoint point) { return inside(roadRing, toGrid(point)); });
}

// Проверяет расположение здания относительно камер, ограждений и дорог.
bool fitsPlan(const Building& building, const std::vector<CameraPlacement>& cameras,
              const std::vector<Fence>& fences, const std::vector<Road>& roads)
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

    for (const auto& road : roads)
    {
        const auto footprint = Project::roadFootprint(road);
        if (!footprint)
            continue;
        for (const auto& part : building.footprints)
            if (roadConflicts(*footprint, part))
                return false;
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

    for (const auto& pole : poles_)
        if (!VisibilityCalculator::mountPoint(pole.position, {building}, 0.0))
            return std::nullopt;

    if (!fitsPlan(building, cameras_, fences_, roads_))
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

        if (collapsed || sampleLine(line).empty())
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

    if (isMountForbidden(pose.position) ||
        !VisibilityCalculator::mountPoint(pose.position, buildings_, 0.0))
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
        for (auto& vertex : part.outerRing)
        {
            if (vertex.xMeters == bounds->end.xMeters)
                vertex.xMeters = end.xMeters;
            if (vertex.yMeters == bounds->end.yMeters)
                vertex.yMeters = end.yMeters;
        }

        if (!validateFootprint(part) || !fitsPlan(changed, cameras_, fences_, roads_))
            return false;

        for (const auto& pole : poles_)
            if (!VisibilityCalculator::mountPoint(pole.position, {changed}, 0.0))
                return false;
        rememberEdit(building);
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
            rememberEdit(building);
            building.heightMeters = meters;
            return true;
        }
    }

    for (auto& fence : fences_)
    {
        if (fence.id == id && meters > 0.0)
        {
            rememberEdit(fence);
            fence.heightMeters = meters;
            return true;
        }
    }

    for (auto& camera : cameras_)
    {
        if (camera.id == id)
        {
            rememberEdit(camera);
            camera.pose.heightMeters = meters;
            return true;
        }
    }

    for (auto& target : targets_)
    {
        if (target.id == id)
        {
            rememberEdit(target);
            std::visit([meters](auto& geometry) {
                geometry.heightMeters = meters;
            }, target.geometry);

            return true;
        }
    }

    for (auto& pole : poles_)
    {
        if (pole.id == id && meters > 0.0)
        {
            rememberEdit(pole);
            pole.heightMeters = meters;
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
    const auto poleId = poles_.empty() ? 0 : poles_.back().id;
    const auto banId = bans_.empty() ? 0 : bans_.back().id;
    const auto roadId = roads_.empty() ? 0 : roads_.back().id;
    const PlanObjectId last = std::max({buildingId, targetId, cameraId, fenceId,
                                        poleId, banId, roadId});

    if (last == 0)
        return false;

    if (last == roadId)
        roads_.pop_back();
    else if (last == poleId)
        poles_.pop_back();
    else if (last == banId)
        bans_.pop_back();
    else if (last == fenceId)
        fences_.pop_back();
    else if (last == cameraId)
        cameras_.pop_back();
    else if (last == targetId)
        targets_.pop_back();
    else
        buildings_.pop_back();

    return true;
}

// Вычисляет положение на отрезке по доле его длины.
static PlanPoint interpolate(const PlanSegment& segment, double fraction)
{
    return {segment.start.xMeters + fraction * (segment.end.xMeters - segment.start.xMeters),
            segment.start.yMeters + fraction * (segment.end.yMeters - segment.start.yMeters)};
}

// Формирует точки на ломаной, включая её вершины.
std::vector<ObservationPoint> Project::sampleLine(const ObservationLine& line)
{
    constexpr std::size_t maxSamples = 4096;
    if (line.centerline.size() < 2 || !std::isfinite(line.sampleStepMeters) ||
        line.sampleStepMeters <= 0.0)
        return {};

    std::vector<ObservationPoint> points{{line.centerline.front(), line.heightMeters}};
    for (std::size_t i = 1; i < line.centerline.size(); ++i)
    {
        const PlanSegment segment{line.centerline[i - 1], line.centerline[i]};
        const double length = std::hypot(segment.end.xMeters - segment.start.xMeters,
                                         segment.end.yMeters - segment.start.yMeters);
        const double divisions = std::ceil(length / line.sampleStepMeters);
        if (!std::isfinite(divisions) || divisions > maxSamples - points.size())
            return {};

        const auto count = static_cast<std::size_t>(divisions);
        for (std::size_t j = 1; j <= count; ++j)
        {
            const auto point = j == count ? segment.end
                                          : interpolate(segment, static_cast<double>(j) / count);
            points.push_back({point, line.heightMeters});
        }
    }
    return points.size() > 1 ? std::move(points) : std::vector<ObservationPoint>{};
}

// Проверяет и добавляет отдельную монтажную опору.
std::optional<PlanObjectId> Project::addPole(MountPole pole)
{
    if (nextId_ == 0 || !isValidPoint(pole.position) ||
        !std::isfinite(pole.heightMeters) || pole.heightMeters <= 0 ||
        !VisibilityCalculator::mountPoint(pole.position, buildings_, 0.0))
        return std::nullopt;

    pole.id = nextId_;
    poles_.push_back(pole);
    return nextId_++;
}

// Возвращает контур по его индексу.
static const std::vector<PlanPoint>& facadeRing(const BuildingFootprint& part, std::size_t ring)
{
    return ring == 0 ? part.outerRing : part.innerRings[ring - 1];
}

// Проверяет индексы и возвращает геометрию ребра.
std::optional<PlanSegment> Project::facadeSegment(const FacadeEdge& facade) const
{
    for (const auto& building : buildings_)
    {
        if (building.id != facade.buildingId || facade.footprint >= building.footprints.size())
            continue;

        const auto& part = building.footprints[facade.footprint];
        if (facade.ring > part.innerRings.size())
            return std::nullopt;
        const auto& ring = facadeRing(part, facade.ring);
        if (facade.edge < ring.size())
            return PlanSegment{ring[facade.edge], ring[(facade.edge + 1) % ring.size()]};
    }
    return std::nullopt;
}

// Находит ближайшее ребро в заданном радиусе.
std::optional<FacadeEdge> Project::facadeAt(PlanPoint point, double tolerance,
                                            PlanObjectId onlyBuilding) const
{
    if (!isValidPoint(point) || !std::isfinite(tolerance) || tolerance < 0)
        return std::nullopt;

    std::optional<FacadeEdge> result;
    double best = tolerance * tolerance;
    for (const auto& building : buildings_)
    {
        if (onlyBuilding != 0 && building.id != onlyBuilding)
            continue;

        for (std::size_t partIndex = 0; partIndex < building.footprints.size(); ++partIndex)
        {
            const auto& part = building.footprints[partIndex];
            for (std::size_t ringIndex = 0; ringIndex <= part.innerRings.size(); ++ringIndex)
            {
                const auto& ring = facadeRing(part, ringIndex);
                for (std::size_t edge = 0; edge < ring.size(); ++edge)
                {
                    const PlanSegment segment{ring[edge], ring[(edge + 1) % ring.size()]};
                    const auto dx = segment.end.xMeters - segment.start.xMeters;
                    const auto dy = segment.end.yMeters - segment.start.yMeters;
                    const auto x = point.xMeters - segment.start.xMeters;
                    const auto y = point.yMeters - segment.start.yMeters;
                    const double fraction = std::clamp((x * dx + y * dy) / (dx * dx + dy * dy),
                                                       0.0, 1.0);
                    const double distance = std::hypot(x - fraction * dx, y - fraction * dy);
                    if (distance * distance <= best)
                    {
                        best = distance * distance;
                        result = FacadeEdge{building.id, partIndex, ringIndex, edge};
                    }
                }
            }
        }
    }
    return result;
}

// Проверяет границы запрета и сохраняет его на выбранном ребре.
std::optional<PlanObjectId> Project::addFacadeBan(FacadeBan ban)
{
    if (nextId_ == 0 || !(0 <= ban.from && ban.from < ban.to && ban.to <= 1) ||
        !facadeSegment(ban.facade))
        return std::nullopt;

    ban.id = nextId_;
    bans_.push_back(ban);
    return nextId_++;
}

// Преобразует относительные границы запрета в координаты плана.
std::optional<PlanSegment> Project::bannedSegment(const FacadeBan& ban) const
{
    const auto edge = facadeSegment(ban.facade);
    if (!edge)
        return std::nullopt;

    return PlanSegment{interpolate(*edge, ban.from), interpolate(*edge, ban.to)};
}

// Проверяет, входит ли допустимая позиция в один из запретов данного ребра.
bool Project::facadeMountAllowed(const FacadeEdge& facade, double fraction) const
{
    if (!(0 <= fraction && fraction <= 1) || !facadeSegment(facade))
        return false;

    for (const auto& ban : bans_)
    {
        const auto& edge = ban.facade;
        if (edge.buildingId == facade.buildingId && edge.footprint == facade.footprint &&
            edge.ring == facade.ring && edge.edge == facade.edge &&
            fraction >= ban.from && fraction <= ban.to)
            return false;
    }
    return true;
}

// Запоминает удаляемый объект и освобождает его место в коллекции.
bool Project::deleteObject(PlanObjectId id)
{
    const auto erase = [&](auto& collection) {
        const auto found = std::find_if(collection.begin(), collection.end(),
                                        [id](const auto& item) { return item.id == id; });
        if (found == collection.end())
            return false;

        Deletion action{nextId_ - 1, *found, {}};
        if constexpr (std::is_same_v<std::decay_t<decltype(*found)>, Building>)
            for (const auto& ban : bans_)
                if (ban.facade.buildingId == id)
                    action.bans.push_back(ban);

        deletions_.push_back(std::move(action));
        collection.erase(found);
        if constexpr (std::is_same_v<std::decay_t<decltype(*found)>, Building>)
            bans_.erase(std::remove_if(bans_.begin(), bans_.end(),
                                       [id](const FacadeBan& ban) { return ban.facade.buildingId == id; }), bans_.end());
        return true;
    };

    return id != 0 && (erase(buildings_) || erase(targets_) || erase(cameras_) ||
                       erase(fences_) || erase(poles_) || erase(bans_) || erase(roads_));
}

// Восстанавливает объект по идентификатору, сохраняя порядок коллекции.
template<class Object>
static void restoreObject(std::vector<Object>& collection, Object object, bool replacesExisting = false)
{
    const auto position = std::lower_bound(collection.begin(), collection.end(), object.id,
                                           [](const Object& item, PlanObjectId id) { return item.id < id; });
    if (replacesExisting)
        *position = std::move(object);
    else
        collection.insert(position, std::move(object));
}

// Отменяет добавление либо восстанавливает удалённый объект.
bool Project::undoLastAction()
{
    if (deletions_.empty())
        return undoLastAddition();

    const auto latest = [](const auto& collection) -> PlanObjectId {
        return collection.empty() ? 0 : collection.back().id;
    };

    const auto last = std::max({latest(buildings_), latest(targets_), latest(cameras_),
                                latest(fences_), latest(poles_), latest(bans_), latest(roads_)});
    auto& action = deletions_.back();
    if (last > action.lastIssued)
        return undoLastAddition();

    std::visit([&](auto& object) {
        using Object = std::decay_t<decltype(object)>;
        if constexpr (std::is_same_v<Object, Building>)
            restoreObject(buildings_, std::move(object), action.replacesExisting);
        else if constexpr (std::is_same_v<Object, ObservationTarget>)
            restoreObject(targets_, std::move(object), action.replacesExisting);
        else if constexpr (std::is_same_v<Object, CameraPlacement>)
            restoreObject(cameras_, std::move(object), action.replacesExisting);
        else if constexpr (std::is_same_v<Object, Fence>)
            restoreObject(fences_, std::move(object), action.replacesExisting);
        else if constexpr (std::is_same_v<Object, MountPole>)
            restoreObject(poles_, std::move(object), action.replacesExisting);
        else if constexpr (std::is_same_v<Object, Road>)
            restoreObject(roads_, std::move(object), action.replacesExisting);
        else
            restoreObject(bans_, std::move(object), action.replacesExisting);
    }, action.object);

    for (auto& ban : action.bans)
        restoreObject(bans_, std::move(ban));
    deletions_.pop_back();
    return true;
}


// Проецирует точку на отрезок и ограничивает проекцию его концами.
double Project::segmentFraction(const PlanSegment& segment, PlanPoint point)
{
    const double x = segment.end.xMeters - segment.start.xMeters;
    const double y = segment.end.yMeters - segment.start.yMeters;
    const double lengthSquared = x * x + y * y;
    if (lengthSquared == 0.0)
        return 0.0;

    const double dot = (point.xMeters - segment.start.xMeters) * x +
                       (point.yMeters - segment.start.yMeters) * y;
    return std::clamp(dot / lengthSquared, 0.0, 1.0);
}

// Проверяет совпадение точки крепления с запрещённым отрезком.
bool Project::isMountForbidden(PlanPoint point) const
{
    constexpr double toleranceMeters = 1e-7;
    for (const auto& ban : bans_)
    {
        const auto segment = bannedSegment(ban);
        if (!segment)
            continue;
        const auto nearest = interpolate(*segment, segmentFraction(*segment, point));
        if (std::hypot(point.xMeters - nearest.xMeters, point.yMeters - nearest.yMeters)
            <= toleranceMeters)
            return true;
    }
    return false;
}

// Строит полосу постоянной ширины вокруг прямой оси.
std::optional<BuildingFootprint> Project::roadFootprint(const Road& road)
{
    if (road.centerline.size() != 2 || !isValidPoint(road.centerline.front()) ||
        !isValidPoint(road.centerline.back()) || !std::isfinite(road.widthMeters) ||
        road.widthMeters < 0.1 || road.widthMeters > 1000.0)
        return std::nullopt;

    const auto a = road.centerline.front();
    const auto b = road.centerline.back();
    const double dx = b.xMeters - a.xMeters;
    const double dy = b.yMeters - a.yMeters;
    const double length = std::hypot(dx, dy);
    if (length < 0.01)
        return std::nullopt;

    const double x = -dy / length * (road.widthMeters / 2.0);
    const double y = dx / length * (road.widthMeters / 2.0);
    BuildingFootprint part;
    part.outerRing = {{a.xMeters + x, a.yMeters + y}, {b.xMeters + x, b.yMeters + y},
                      {b.xMeters - x, b.yMeters - y}, {a.xMeters - x, a.yMeters - y}};

    if (!validateFootprint(part))
        return std::nullopt;
    return part;
}

// Проверяет полосу дороги относительно оснований зданий.
bool Project::canPlaceRoad(const Road& road) const
{
    const auto footprint = roadFootprint(road);
    if (!footprint)
        return false;
    for (const auto& building : buildings_)
        for (const auto& part : building.footprints)
            if (roadConflicts(*footprint, part))
                return false;
    return true;
}

// Добавляет проверенную дорогу.
std::optional<PlanObjectId> Project::addRoad(Road road)
{
    if (nextId_ == 0 || !canPlaceRoad(road))
        return std::nullopt;
    road.id = nextId_;
    roads_.push_back(std::move(road));
    return nextId_++;
}

// Меняет ширину дороги с возможностью отмены.
bool Project::setRoadWidth(PlanObjectId id, double width)
{
    const auto road = std::find_if(roads_.begin(), roads_.end(),
                                   [id](const Road& item) { return item.id == id; });
    if (road == roads_.end())
        return false;

    Road changed = *road;
    changed.widthMeters = width;
    if (!canPlaceRoad(changed))
        return false;

    rememberEdit(*road);
    *road = std::move(changed);
    return true;
}

// Привязывает камеру к ближайшей опоре или фасаду.
std::optional<CameraPose> Project::snapCameraMount(PlanPoint point, double height,
                                                   double radius) const
{
    if (!isValidPoint(point) || !std::isfinite(height) || height < 0 ||
        !std::isfinite(radius) || radius < 0)
        return std::nullopt;

    const auto wall = VisibilityCalculator::mountPoint(point, buildings_, radius);
    if (!wall)
        return std::nullopt;

    double best = radius;
    if (const auto edge = facadeAt(point, radius))
    {
        if (const auto segment = facadeSegment(*edge))
        {
            const auto nearest = interpolate(*segment, segmentFraction(*segment, point));
            best = std::hypot(point.xMeters - nearest.xMeters, point.yMeters - nearest.yMeters);
        }
    }

    CameraPose result;
    result.position = *wall;
    result.heightMeters = height;
    for (const auto& pole : poles_)
    {
        const double distance = std::hypot(point.xMeters - pole.position.xMeters,
                                           point.yMeters - pole.position.yMeters);
        if (distance <= best)
        {
            best = distance;
            result.position = pole.position;
            result.heightMeters = pole.heightMeters;
        }
    }
    return result;
}

