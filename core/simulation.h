#pragma once

#include <cstdint>

#include "balance.h"
#include "runtime.h"
#include "world.h"

namespace kindle {

// Start scene of the prototype ("Баланс_прототипу.md", section 5).
struct StartScene {
    int characters = 1;
    int axes = 1;
    int bread = 10;
    int trees = 200;
    double start_time = 5.5 * 3600.0;  // day 1, 05:30
    double hunger = 30, fatigue = 10, health = 100;
};

void build_start_scene(World& world, const StartScene& scene);

// World + script runtime, advanced in fixed ticks. Not copyable or movable (the runtime keeps
// a reference to the world), so hold it by pointer if it has to outlive a scope.
class Simulation {
public:
    Simulation(const Balance& balance, uint64_t seed);

    World& world() { return world_; }
    ScriptRuntime& runtime() { return runtime_; }

    // One tick: weather/events, then every character runs until now + dt.
    void tick(double dt);
    void run_until(double t, double dt = 60.0);
    void run_for(double seconds, double dt = 60.0) { run_until(world_.now() + seconds, dt); }

private:
    World world_;  // must be declared before runtime_
    ScriptRuntime runtime_;
};

}  // namespace kindle
