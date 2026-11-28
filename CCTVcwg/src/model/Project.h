#pragma once

#include "CameraTypes.h"

#include <optional>
#include <vector>

// Хранит объекты плана и проверяет операции редактирования.
class Project
{
public:
    // Проверяет и добавляет здание.
    std::optional<PlanObjectId> addBuilding(Building building);

    // Проверяет и добавляет цель наблюдения.
    std::optional<PlanObjectId> addTarget(ObservationTarget target);

    // Проверяет и добавляет камеру.
    std::optional<PlanObjectId> addCamera(CameraPlacement camera);

    // Проверяет допустимость отрезка ограждения.
    bool canPlaceFence(const PlanSegment& segment) const;

    // Проверяет и добавляет ограждение.
    std::optional<PlanObjectId> addFence(Fence fence);

    // Возвращает противоположные углы прямоугольного здания.
    static std::optional<PlanSegment> rectangleBounds(const Building& building);

    // Изменяет размеры и высоту прямоугольного здания после проверки.
    bool resizeBuilding(PlanObjectId id, double width, double length, double height);

    // Возвращает здания проекта.
    const std::vector<Building>& buildings() const;

    // Возвращает цели наблюдения.
    const std::vector<ObservationTarget>& targets() const;

    // Возвращает установленные камеры.
    const std::vector<CameraPlacement>& cameras() const;

    // Возвращает ограждения проекта.
    const std::vector<Fence>& fences() const;

    // Изменяет высоту объекта по идентификатору.
    bool setHeight(PlanObjectId id, double meters);

    // Удаляет последний добавленный объект.
    bool undoLastAddition();

private:
    PlanObjectId nextId_ = 1;
    std::vector<Building> buildings_;
    std::vector<ObservationTarget> targets_;
    std::vector<CameraPlacement> cameras_;
    std::vector<Fence> fences_;
};