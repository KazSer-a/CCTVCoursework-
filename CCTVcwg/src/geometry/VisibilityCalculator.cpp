#include "VisibilityCalculator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace
{
using VisibilityCalculator::Span;
using RingPoints = std::vector<PlanPoint>;

constexpr double radiansToDegrees = 57.29577951308232;
constexpr double eps = 1e-8;

// Вычисляет разность координат.
PlanPoint difference(PlanPoint a, PlanPoint b)
{
    return {a.xMeters - b.xMeters, a.yMeters - b.yMeters};
}

// Вычисляет скалярное произведение.
double dot(PlanPoint a, PlanPoint b)
{
    return a.xMeters * b.xMeters + a.yMeters * b.yMeters;
}

// Вычисляет ориентированную площадь параллелограмма.
double cross(PlanPoint a, PlanPoint b)
{
    return a.xMeters * b.yMeters - a.yMeters * b.xMeters;
}

// Возвращает ближайшую к точке позицию на отрезке.
PlanPoint closest(PlanPoint point, PlanPoint a, PlanPoint b)
{
    const PlanPoint edge = difference(b, a);
    const double lengthSquared = dot(edge, edge);

    if (lengthSquared == 0.0)
        return a;

    const double t = std::clamp(
        dot(difference(point, a), edge) / lengthSquared, 0.0, 1.0);

    return {a.xMeters + t * edge.xMeters, a.yMeters + t * edge.yMeters};
}

// Обходит внешний контур части здания и контуры дворов.
template<class Function>
void eachRing(const BuildingFootprint& part, Function visit)
{
    visit(part.outerRing);

    for (const auto& hole : part.innerRings)
        visit(hole);
}

// Проверяет принадлежность точки внутренней части основания здания.
bool strictlyInside(PlanPoint point, const BuildingFootprint& part)
{
    bool inside = false;
    bool boundary = false;

    eachRing(part, [&](const RingPoints& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i)
        {
            const PlanPoint a = ring[i];
            const PlanPoint b = ring[(i + 1) % ring.size()];
            const PlanPoint offset = difference(point, closest(point, a, b));

            if (dot(offset, offset) <= eps * eps)
                boundary = true;

            if ((a.yMeters > point.yMeters) == (b.yMeters > point.yMeters))
                continue;

            const double x = a.xMeters + (point.yMeters - a.yMeters) *
                                             (b.xMeters - a.xMeters) / (b.yMeters - a.yMeters);

            if (point.xMeters < x)
                inside = !inside;
        }
    });

    return inside && !boundary;
}

// Проверяет внутренность всех частей зданий.
bool insideBuilding(PlanPoint point, const std::vector<Building>& buildings)
{
    for (const auto& building : buildings)
    {
        for (const auto& part : building.footprints)
        {
            if (strictlyInside(point, part))
                return true;
        }
    }

    return false;
}

// Проецирует перекрытый препятствием интервал на плоскость целей.
void addShadow(std::vector<Span>& shadows, double entry, double exit,
               double roof, double cameraZ, double targetZ, double limit)
{
    if (exit <= eps || entry > limit || std::min(cameraZ, targetZ) > roof)
        return;

    double first = 0.0;
    double last = 1.0;

    if (cameraZ > roof)
        first = (cameraZ - roof) / (cameraZ - targetZ);
    else if (targetZ > roof)
        last = (roof - cameraZ) / (targetZ - cameraZ);

    if (last <= 0.0)
        return;

    const double begin = std::max(0.0, entry) / last;
    const double end = first > 0.0 ? std::min(limit, exit / first) : limit;

    if (begin <= end)
        shadows.push_back({begin, end});
}

// Находит участки луча, проходящие через основание здания.
void buildingShadows(std::vector<Span>& shadows, const BuildingFootprint& part,
                     PlanPoint origin, PlanPoint direction, double roof,
                     double cameraZ, double targetZ, double limit)
{
    std::vector<double> hits;

    eachRing(part, [&](const RingPoints& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i)
        {
            const PlanPoint a = difference(ring[i], origin);
            const PlanPoint b = difference(ring[(i + 1) % ring.size()], origin);

            const double y1 = cross(direction, a);
            const double y2 = cross(direction, b);

            if ((y1 > 0.0) == (y2 > 0.0))
                continue;

            const double t = y1 / (y1 - y2);
            hits.push_back(dot(a, direction) + t * dot(difference(b, a), direction));
        }
    });

    std::sort(hits.begin(), hits.end());

    for (std::size_t i = 1; i < hits.size(); i += 2)
    {
        if (hits[i] - hits[i - 1] > eps)
        {
            addShadow(shadows, hits[i - 1], hits[i], roof,
                      cameraZ, targetZ, limit);
        }
    }
}
}

namespace VisibilityCalculator
{
// Возвращает направление от первой точки ко второй в градусах.
std::optional<double> bearing(const PlanPoint& from, const PlanPoint& to)
{
    const PlanPoint delta = difference(to, from);

    if (!std::isfinite(delta.xMeters) || !std::isfinite(delta.yMeters) ||
        (delta.xMeters == 0.0 && delta.yMeters == 0.0))
    {
        return std::nullopt;
    }

    const double angle = std::atan2(delta.xMeters, delta.yMeters) * radiansToDegrees;
    return angle < 0.0 ? angle + 360.0 : angle;
}

// Проверяет попадание точки в горизонтальный сектор камеры.
bool inSector(const CameraPlacement& camera, const PlanPoint& target)
{
    const double range = camera.model.rangeMeters;
    const double width = camera.model.horizontalFovDegrees;
    const double azimuth = camera.pose.azimuthDegrees;

    if (!std::isfinite(range) || range <= 0.0 ||
        !std::isfinite(width) || width <= 0.0 || width > 360.0 ||
        !std::isfinite(azimuth))
    {
        return false;
    }

    const PlanPoint delta = difference(target, camera.pose.position);
    const double distance = std::hypot(delta.xMeters, delta.yMeters);

    if (!std::isfinite(distance) || distance > range)
        return false;

    const auto angle = bearing(camera.pose.position, target);

    if (!angle)
        return false;

    const double differenceDegrees =
        std::remainder(*angle - std::remainder(azimuth, 360.0), 360.0);

    return std::abs(differenceDegrees) <= width / 2.0 + 1e-10;
}

// Отклоняет внутреннюю точку и привязывает наружную к ближайшему фасаду.
std::optional<PlanPoint> mountPoint(
    PlanPoint point, const std::vector<Building>& buildings, double snapMeters)
{
    if (!std::isfinite(point.xMeters) || !std::isfinite(point.yMeters) ||
        !std::isfinite(snapMeters) || snapMeters < 0.0 ||
        insideBuilding(point, buildings))
    {
        return std::nullopt;
    }

    if (snapMeters == 0.0)
        return point;

    PlanPoint result = point;
    double best = snapMeters * snapMeters;

    for (const auto& building : buildings)
    {
        for (const auto& part : building.footprints)
        {
            eachRing(part, [&](const RingPoints& ring) {
                for (std::size_t i = 0; i < ring.size(); ++i)
                {
                    const auto a = ring[i];
                    const auto b = ring[(i + 1) % ring.size()];
                    const auto candidate = closest(point, a, b);
                    const auto delta = difference(candidate, point);
                    const double distanceSquared = dot(delta, delta);

                    if (distanceSquared <= best && !insideBuilding(candidate, buildings))
                    {
                        best = distanceSquared;
                        result = candidate;
                    }
                }
            });
        }
    }

    return result;
}

// Вычитает тени препятствий из интервала вертикального поля зрения.
std::vector<Span> visibleRay(
    const CameraPlacement& camera, double angle, double targetZ,
    const std::vector<Building>& buildings, const std::vector<Fence>& fences)
{
    const CameraPose& pose = camera.pose;
    const CameraModel& model = camera.model;
    const double dz = pose.heightMeters - targetZ;

    if (!std::isfinite(angle) || !std::isfinite(targetZ) || targetZ < 0.0 ||
        !std::isfinite(dz) || !std::isfinite(model.rangeMeters) ||
        model.rangeMeters <= std::abs(dz) || !std::isfinite(pose.tiltDegrees) ||
        !std::isfinite(model.verticalFovDegrees) ||
        model.verticalFovDegrees <= 0.0 || model.verticalFovDegrees > 180.0)
    {
        return {};
    }

    const double low = std::max(-90.0, pose.tiltDegrees - model.verticalFovDegrees / 2.0);
    const double high = std::min(90.0, pose.tiltDegrees + model.verticalFovDegrees / 2.0);

    double start = 0.0;
    double end = std::sqrt((model.rangeMeters - std::abs(dz)) *
                           (model.rangeMeters + std::abs(dz)));

    const double lower = dz < 0.0 ? -high : low;
    const double upper = dz < 0.0 ? -low : high;

    if (dz == 0.0 && (low > 0.0 || high < 0.0))
        return {};

    if (dz != 0.0)
    {
        if (upper <= 0.0 || lower >= 90.0)
            return {};

        if (upper < 90.0)
            start = std::abs(dz) / std::tan(upper / radiansToDegrees);

        if (lower > 0.0)
            end = std::min(end, std::abs(dz) / std::tan(lower / radiansToDegrees));
    }

    if (start > end)
        return {};

    const PlanPoint direction{
        std::sin(angle / radiansToDegrees),
        std::cos(angle / radiansToDegrees)
    };

    std::vector<Span> shadows;

    for (const auto& building : buildings)
    {
        // Неизвестная высота задаёт консервативную оценку видимости.
        const double roof = building.heightMeters.value_or(
            std::numeric_limits<double>::infinity());

        for (const auto& part : building.footprints)
        {
            buildingShadows(shadows, part, pose.position, direction, roof,
                            pose.heightMeters, targetZ, end);
        }
    }

    for (const auto& fence : fences)
    {
        const auto a = difference(fence.segment.start, pose.position);
        const auto b = difference(fence.segment.end, pose.position);

        const double y1 = cross(direction, a);
        const double y2 = cross(direction, b);

        if (std::abs(y1) < eps && std::abs(y2) < eps)
        {
            const double x1 = dot(a, direction);
            const double x2 = dot(b, direction);

            addShadow(shadows, std::min(x1, x2), std::max(x1, x2),
                      fence.heightMeters, pose.heightMeters, targetZ, end);
        }
        else if ((y1 <= 0.0 && y2 >= 0.0) || (y2 <= 0.0 && y1 >= 0.0))
        {
            const double t = std::clamp(y1 / (y1 - y2), 0.0, 1.0);
            const double x = dot(a, direction) + t * dot(difference(b, a), direction);

            addShadow(shadows, x, x, fence.heightMeters,
                      pose.heightMeters, targetZ, end);
        }
    }

    std::sort(shadows.begin(), shadows.end(), [](Span a, Span b) {
        return a.nearMeters < b.nearMeters;
    });

    std::vector<Span> result;

    for (const auto& shadow : shadows)
    {
        if (shadow.nearMeters > start + eps)
            result.push_back({start, std::min(end, shadow.nearMeters - eps)});

        start = std::max(start, shadow.farMeters + eps);

        if (start > end)
            break;
    }

    if (start <= end)
        result.push_back({start, end});

    return result;
}

// Проверяет цель по её собственным координатам и высоте.
bool visible(const CameraPlacement& camera, const ObservationPoint& target,
             const std::vector<Building>& buildings, const std::vector<Fence>& fences)
{
    if (!inSector(camera, target.position))
        return false;

    const double angle = *bearing(camera.pose.position, target.position);
    const auto delta = difference(target.position, camera.pose.position);
    const double distance = std::hypot(delta.xMeters, delta.yMeters);

    for (const auto& span : visibleRay(camera, angle, target.heightMeters, buildings, fences))
    {
        if (distance >= span.nearMeters && distance <= span.farMeters)
            return true;
    }

    return false;
}
}