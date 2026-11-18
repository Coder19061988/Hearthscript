#include "simulation.h"

#include <algorithm>
#include <string>

namespace kindle {

namespace {
const char* const kNames[] = {"Остап", "Марта", "Богдан", "Ярина", "Тарас",
                              "Олеся", "Данило", "Злата", "Матвій", "Соломія"};
constexpr int kNameCount = sizeof(kNames) / sizeof(kNames[0]);
}  // namespace

void build_start_scene(World& world, const StartScene& scene) {
    // Distances from the balance doc: house 60 m from the storage, forest 300 m from the storage.
    // The field is ~170 m from the storage, on the other side from the forest.
    world.add_storage("Склад_1", {0, 0}, {{"axe", scene.axes}, {"sickle", scene.sickles}},
                      {{"bread", scene.bread}, {"wood", 0}, {"grain", scene.grain}, {"flour", 0}});
    const int warm_clothes = scene.warm_clothes < 0 ? scene.characters : scene.warm_clothes;
    int house = world.add_house("Будинок_1", {60, 0}, {{"warm_clothes", warm_clothes}, {"clothes", 0}});
    world.add_forest("Ліс_1", {0, 300}, scene.trees);
    world.add_field("Поле_1", {120, -120});
    for (int i = 0; i < scene.characters; ++i) {
        std::string name = kNames[i % kNameCount];
        if (i >= kNameCount) name += "_" + std::to_string(i / kNameCount + 1);
        world.add_character(name, house, {60, 0}, scene.hunger, scene.fatigue, scene.health);
    }
    world.set_start_time(scene.start_time);
}

Simulation::Simulation(const Balance& balance, uint64_t seed) : world_(balance, seed), runtime_(world_) {}

void Simulation::tick(double dt) {
    const double t0 = world_.now();
    const double t1 = t0 + dt;
    for (const std::string& event : world_.begin_tick(t0, t1)) runtime_.post_event(event);
    for (const Character& c : world_.characters()) runtime_.run_until(c.obj, t1);
    world_.end_tick(t1);
}

void Simulation::run_until(double t, double dt) {
    while (world_.now() < t - kEps) tick(std::min(dt, t - world_.now()));
}

}  // namespace kindle
