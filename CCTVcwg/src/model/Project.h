#pragma once

#include "PlanObjects.h"

#include <optional>
#include <vector>

// План объекта и операции изменения его состава.
class Project
{
public:
    // Проверяет и добавляет здание, возвращает присвоенный идентификатор.
    std::optional<PlanObjectId> addBuilding(Building building);

    // Проверяет и добавляет цель, возвращает присвоенный идентификатор.
    std::optional<PlanObjectId> addTarget(ObservationTarget target);

    // Возвращает коллекцию зданий проекта.
    const std::vector<Building>& buildings() const;

    // Возвращает коллекцию целей наблюдения.
    const std::vector<ObservationTarget>& targets() const;

    // Удаляет последнее добавленное здание или цель наблюдения.
    bool undoLastAddition()
    {
        if (buildings_.empty() && targets_.empty())
            return false;

        if (targets_.empty() ||
            (!buildings_.empty() && buildings_.back().id > targets_.back().id))
        {
            buildings_.pop_back();
        }
        else
        {
            targets_.pop_back();
        }

        return true;
    }

private:
    PlanObjectId nextId_ = 1;
    std::vector<Building> buildings_;
    std::vector<ObservationTarget> targets_;
};