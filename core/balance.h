#pragma once

#include <string>
#include <vector>

namespace kindle {

// Tunable numbers of the prototype. Defaults mirror "Баланс_прототипу.md";
// config/balance.json may override any subset of them (same key names).
struct Balance {
    // Time
    double day_real_minutes = 12.0;          // real minutes per game day at x1 (for the UI layer)
    double night_start_hour = 21.0;
    double night_end_hour = 5.0;
    double think_seconds_per_line = 2.0;     // "thinking" cost of every executed line of player code
    double idle_log_hours = 4.0;             // log a warning after this long without any action
    double time_of_day_grace_minutes = 10.0; // until=/wait_until(time) that passed less than this ago counts as "now"

    // Movement and load
    double walk_meters_per_minute = 60.0;
    double walk_loaded_meters_per_minute = 45.0;  // when carrying more than half of the limit
    double weight_limit_kg = 50.0;
    double wood_kg = 5.0;
    double bread_kg = 0.5;

    // Action durations, game minutes
    double tool_take_minutes = 1.0;          // take / put_back
    double transfer_base_minutes = 1.0;      // pick_up / drop
    double transfer_per_unit_minutes = 0.2;
    double chop_minutes_per_unit = 20.0;
    double eat_minutes = 15.0;
    double wood_per_tree = 5.0;

    // Hunger
    double hunger_per_hour = 4.0;
    double bread_hunger_relief = 40.0;
    double hunger_damage_threshold = 80.0;
    double hunger_damage_per_hour = 1.0;
    double hunger_full_damage_per_hour = 1.5;  // at hunger == 100

    // Fatigue, per hour (negative values restore)
    double fatigue_heavy_per_hour = 6.0;
    double fatigue_light_per_hour = 3.0;
    double fatigue_rest_per_hour = -8.0;
    double fatigue_sleep_home_per_hour = -12.0;
    double fatigue_sleep_ground_per_hour = -6.0;
    double fatigue_slow_threshold = 90.0;
    double fatigue_slow_factor = 0.5;
    double fatigue_full_damage_per_hour = 1.0;  // at fatigue == 100

    // Health
    double health_regen_per_hour = 1.0;
    double health_regen_max_hunger = 50.0;
    double health_regen_max_fatigue = 70.0;

    // Rain
    double rain_chance_per_day = 0.3;
    double rain_min_hours = 1.0;
    double rain_max_hours = 3.0;
    double rain_earliest_start_hour = 6.0;
    double rain_latest_start_hour = 20.0;
    double rain_work_factor = 0.7;
    double rain_extra_fatigue_per_hour = 2.0;

    // Loads a flat JSON object {"key": number, ...}; keys starting with "_" are comments.
    // Returns false and sets `error` when the file can't be read or parsed.
    // Unknown keys are reported in `warnings` and ignored.
    bool load_json_file(const std::string& path, std::string& error, std::vector<std::string>& warnings);
};

}  // namespace kindle
