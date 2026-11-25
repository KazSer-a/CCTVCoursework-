#include "Project.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
// Вершины тестового контура.
using Points = std::vector<PlanPoint>;

// Формирует тестовое здание по внешнему контуру и контурам дворов.
Building building(Points outer, std::vector<Points> holes = {})
{
    return {0, {{std::move(outer), std::move(holes)}}, std::nullopt};
}

// Прерывает выполнение тестов при нарушении проверяемого условия.
void check(bool condition, const char* name)
{
    if (!condition)
        throw std::runtime_error(name);
}

// Проверяет добавление зданий и целей, отказ при ошибке и выдачу ID.
void runTests()
{
    const Points square{{0, 0}, {20, 0}, {20, 20}, {0, 20}};
    const Points hole{{2, 2}, {4, 2}, {4, 4}, {2, 4}};

    Project plan;
    check(plan.addBuilding(building(square, {hole})) == 1, "valid courtyard");

    // Проверяет отказ в добавлении и сохранение исходного числа зданий.
    auto reject = [&](Building b, const char* name) {
        check(!plan.addBuilding(std::move(b)) && plan.buildings().size() == 1, name);
    };

    reject(building({{0, 0}, {1, 0}, {2, 0}}), "collinear");
    reject(building({{0, 0}, {2, 2}, {0, 2}, {2, 0}}), "self intersection");
    reject(building({{0, 0}, {0.0001, 0}, {1, 1}}), "collapsed edge");

    reject(building(square, {{{21, 2}, {22, 2}, {21, 3}}}), "outside");
    reject(building(square, {{{0, 2}, {2, 2}, {2, 3}}}), "touching wall");
    reject(building(square, {hole, hole}), "overlapping holes");
    reject(building(square, {hole, {{2.5, 2.5}, {3, 2.5}, {3, 3}}}), "nested holes");

    reject(building(Points(2049)), "vertex budget");
    reject(building({{0, 0}, {100001, 0}, {0, 2}}), "coordinate limit");
    reject(building({{0, 0}, {std::numeric_limits<double>::quiet_NaN(), 0}, {0, 2}}), "NaN");

    auto reversed = square;
    std::reverse(reversed.begin(), reversed.end());
    check(plan.addBuilding(building(reversed)) == 2, "next building ID");

    ObservationTarget target;
    ObservationLine line{{{1, 1}, {1, 1}}, 0, 1};
    target.geometry = line;
    check(!plan.addTarget(target) && plan.targets().empty(), "zero length");

    line.centerline.back().yMeters = 2;
    target.geometry = line;
    check(plan.addTarget(target) == 3, "next target ID");
}
}

// Запускает тесты и возвращает код завершения для CTest.
int main()
{
    try
    {
        runTests();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}