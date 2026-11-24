#pragma once

#include "PlanObjects.h"

#include <optional>
#include <vector>

using namespace std;

// План объекта и операции добавления его элементов.
class Project
{
public:
    // Возвращают ID добавленного объекта или nullopt при отказе.
    optional<PlanObjectId> addBuilding(Building building);
    optional<PlanObjectId> addTarget(ObservationTarget target);

    const vector<Building>& buildings() const;
    const vector<ObservationTarget>& targets() const;

private:
    PlanObjectId nextId_ = 1;
    vector<Building> buildings_;
    vector<ObservationTarget> targets_;
};