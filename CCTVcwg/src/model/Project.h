#pragma once

#include "CameraTypes.h"

#include <optional>
#include <variant>
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

    // Формирует контрольные точки с интервалом не больше указанного шага.
    static std::vector<ObservationPoint> sampleLine(const ObservationLine& line);

    // Добавляет монтажную опору.
    std::optional<PlanObjectId> addPole(MountPole pole);

    // Добавляет запрет крепления на участке фасада.
    std::optional<PlanObjectId> addFacadeBan(FacadeBan ban);

    // Возвращает ближайшее ребро здания и соответствующий отрезок.
    std::optional<FacadeEdge> facadeAt(PlanPoint point, double tolerance,
                                       PlanObjectId onlyBuilding = 0) const;
    std::optional<PlanSegment> facadeSegment(const FacadeEdge& facade) const;

    // Возвращает отрезок запрета и проверяет доступность позиции монтажа.
    std::optional<PlanSegment> bannedSegment(const FacadeBan& ban) const;
    bool facadeMountAllowed(const FacadeEdge& facade, double fraction) const;

    // Возвращает монтажные опоры и запреты.
    const std::vector<MountPole>& poles() const { return poles_; }
    const std::vector<FacadeBan>& facadeBans() const { return bans_; }

    // Выбирает ближайшее крепление на опоре или фасаде.
    std::optional<CameraPose> snapCameraMount(PlanPoint point, double height, double radius) const;

    // Проверяет, находится ли крепление на запрещённом участке.
    bool isMountForbidden(PlanPoint point) const;

    // Возвращает долю длины ближайшей точки отрезка.
    static double segmentFraction(const PlanSegment& segment, PlanPoint point);

    // Строит прямоугольный контур дороги.
    static std::optional<BuildingFootprint> roadFootprint(const Road& road);

    // Проверяет положение и ширину дороги.
    bool canPlaceRoad(const Road& road) const;

    // Создаёт дорогу и присваивает ей идентификатор.
    std::optional<PlanObjectId> addRoad(Road road);

    // Изменяет ширину дороги после проверки.
    bool setRoadWidth(PlanObjectId id, double width);

    // Возвращает дороги проекта.
    const std::vector<Road>& roads() const { return roads_; }

    // Удаляет объект, сохраняя данные для восстановления.
    bool deleteObject(PlanObjectId id);

    // Отменяет последнее добавление или удаление.
    bool undoLastAction();

private:
    using DeletedObject = std::variant<Building, ObservationTarget, CameraPlacement,
                                       Fence, MountPole, FacadeBan, Road>;

    // Содержит удалённый объект и связанные запреты.
    struct Deletion
    {
        PlanObjectId lastIssued;
        DeletedObject object;
        std::vector<FacadeBan> bans;
        bool replacesExisting = false;
    };

    // Сохраняет состояние объекта перед изменением.
    template<class Object>
    void rememberEdit(const Object& object)
    {
        deletions_.push_back({nextId_ - 1, object, {}, true});
    }

    std::vector<Road> roads_;
    std::vector<MountPole> poles_;
    std::vector<FacadeBan> bans_;
    std::vector<Deletion> deletions_;

    PlanObjectId nextId_ = 1;
    std::vector<Building> buildings_;
    std::vector<ObservationTarget> targets_;
    std::vector<CameraPlacement> cameras_;
    std::vector<Fence> fences_;
};
