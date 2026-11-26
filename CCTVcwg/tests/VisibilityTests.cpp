#include "Project.h"
#include "VisibilityCalculator.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
// Прерывает тест при нарушении условия.
void check(bool condition, const char* name)
{
    if (!condition)
        throw std::runtime_error(name);
}

// Возвращает камеру с известными параметрами.
CameraPlacement testCamera()
{
    CameraPlacement camera;
    camera.model.rangeMeters = 20.0;
    camera.model.horizontalFovDegrees = 90.0;
    camera.model.verticalFovDegrees = 90.0;
    return camera;
}

// Проверяет границы сектора и порядок отмены добавлений.
void testSectorAndUndo()
{
    auto camera = testCamera();

    const auto inside = [&](PlanPoint point) {
        return VisibilityCalculator::inSector(camera, point);
    };

    check(inside({0, 20}), "range boundary");
    check(!inside({0, 20.01}), "beyond range");
    check(inside({5, 5}) && inside({-5, 5}), "angular boundaries");
    check(!inside({5.01, 5}) && !inside({0, -1}), "outside sector");
    check(!inside({0, 0}), "undefined direction");
    check(!inside({std::numeric_limits<double>::quiet_NaN(), 0}), "NaN");

    camera.pose.azimuthDegrees = 350.0;
    check(inside({1, 10}), "north crossing");

    Project plan;
    check(plan.addCamera(camera) == 1, "camera ID");
    check(plan.addTarget(ObservationTarget{}) == 2, "target ID");
    check(plan.undoLastAddition() && plan.targets().empty(), "undo target");
    check(plan.cameras().size() == 1, "camera preserved");
    check(plan.undoLastAddition() && plan.cameras().empty(), "undo camera");
    check(!plan.undoLastAddition(), "empty undo");
}

// Проверяет высоты, препятствия и привязку к фасаду.
void testObstacles()
{
    Project plan;
    auto camera = testCamera();

    camera.pose.position = {0, 0};
    camera.pose.heightMeters = 4.0;
    camera.pose.azimuthDegrees = 90.0;

    const ObservationPoint target{{10, 0}, 1.5};

    const auto seen = [&] {
        return VisibilityCalculator::visible(
            camera, target, plan.buildings(), plan.fences());
    };

    check(seen(), "unobstructed target");

    Fence fence;
    fence.segment = {{5, -2}, {5, 2}};
    fence.heightMeters = 2.0;

    const auto fenceId = plan.addFence(fence);
    check(fenceId.has_value(), "add fence");
    check(seen(), "ray clears low fence");
    check(plan.setHeight(*fenceId, 2.75) && !seen(), "ray touches fence top");
    check(plan.setHeight(*fenceId, 4.0) && !seen(), "high fence");
    check(plan.undoLastAddition() && seen(), "remove fence");

    camera.pose.tiltDegrees = -70.0;
    check(!seen(), "vertical field excludes target");
    camera.pose.tiltDegrees = 0.0;

    BuildingFootprint footprint;
    footprint.outerRing = {{2, -2}, {6, -2}, {6, 2}, {2, 2}};

    Building box;
    box.heightMeters = 8.0;
    box.footprints.push_back(footprint);

    check(plan.addBuilding(box).has_value() && !seen(), "building shadow");

    camera.pose.position = {3, 0};
    check(!plan.addCamera(camera), "reject interior camera");

    const auto mount = VisibilityCalculator::mountPoint(
        {6.3, 0}, plan.buildings(), 0.5);

    check(mount && mount->xMeters == 6.0 && mount->yMeters == 0.0,
          "snap to facade");

    camera.pose.position = *mount;
    check(seen(), "outward ray from facade");

    camera.pose.azimuthDegrees = 270.0;

    check(!VisibilityCalculator::visible(
              camera, {{0, 0}, 1.5}, plan.buildings(), {}),
          "inward ray from facade");

    box.footprints.front().innerRings = {
        {{3, -1}, {5, -1}, {5, 1}, {3, 1}}
    };

    check(VisibilityCalculator::mountPoint({4, 0}, {box}, 0.5).has_value(),
          "courtyard");

    check(!VisibilityCalculator::mountPoint({5.1, 0}, {box}, 0.5),
          "inside near facade");

    camera.pose.position = {0, 0};
    check(plan.addCamera(camera).has_value(), "add exterior camera");

    BuildingFootprint enclosing;
    enclosing.outerRing = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    box.footprints = {enclosing};

    check(!plan.addBuilding(box), "building would enclose camera");
}
}

// Запускает проверки геометрии и модели.
int main()
{
    try
    {
        testSectorAndUndo();
        testObstacles();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}