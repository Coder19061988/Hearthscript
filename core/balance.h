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
    double tool_wear_per_hour = 2.5;         // % of condition per hour of work (chop / harvest); ~4 days of 10 h
    double tool_worn_warning = 25.0;         // the log warns once the condition drops below this, %
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

    // Seasons and temperature (MVP: warm season + winter; it rains only in the warm season)
    double season_warm_days = 10.0;
    double season_winter_days = 5.0;
    double temp_warm_avg = 16.0;          // °C, daily average
    double temp_winter_avg = -15.0;
    double temp_day_amplitude = 5.0;      // coldest at 02:00, warmest at 14:00
    double temp_rain_drop = 3.0;

    // Warmth of a character (0..100)
    double comfort_temperature = 10.0;    // perceived temperature at which warmth stops dropping
    double warmth_recover_per_hour = 10.0;
    double warmth_loss_per_degree_hour = 0.8;
    double clothes_bonus = 5.0;           // regular clothes add this to the perceived temperature
    double warm_clothes_bonus = 20.0;
    double bed_bonus = 10.0;              // sleeping in a bed at home (instead of clothes)
    double work_heat_bonus = 5.0;         // heavy work warms up
    double warmth_damage_threshold = 30.0;
    double warmth_damage_per_hour = 1.0;
    double warmth_zero_damage_per_hour = 2.0;
    double health_regen_min_warmth = 50.0;
    double wear_minutes = 5.0;

    // House: stove (heating + baking), chest, hand mill
    double house_heated_temperature = 20.0;
    double house_shelter_bonus = 5.0;     // an unheated house is this much warmer than outside
    double house_warmup_hours = 1.0;      // time constants of the inside temperature
    double house_cooldown_hours = 4.0;
    double firewood_burn_hours = 2.0;     // one unit of wood in the stove burns this long

    // Field and food chain: field -> grain -> hand mill -> flour -> oven -> bread
    double field_seed_grain = 10.0;
    double field_sow_hours = 3.0;
    double field_grow_days = 4.0;         // growing happens only in the warm season
    double field_yield_grain = 80.0;
    double field_harvest_hours = 6.0;
    double grind_minutes_per_unit = 3.0;
    double bake_minutes_per_unit = 5.0;
    double grain_kg = 0.5;
    double flour_kg = 0.5;

    // Construction from blueprints, moving in
    double house_capacity = 4.0;
    double house_build_wood = 40.0;
    double house_build_hours = 16.0;
    double storage_build_wood = 30.0;
    double storage_build_hours = 8.0;
    double field_build_hours = 4.0;        // ploughing, no materials
    double move_in_minutes = 10.0;
    double blueprint_min_distance = 25.0;  // meters to other buildings (forest: + its radius)

    // Animals (cows and chickens live in a barn), the meadow and hay
    double animal_hunger_per_hour = 2.5;         // in winter: they live on stored feed
    double animal_graze_per_hour = 5.0;          // warm season: grazing lowers hunger
    double animal_hunger_damage_threshold = 80.0;
    double animal_hunger_damage_per_hour = 1.5;
    double animal_health_regen_per_hour = 1.0;
    double animal_produce_max_hunger = 50.0;     // milk and eggs only while fed
    double cow_hay_relief = 10.0;                // hunger per unit of hay
    double chicken_grain_relief = 60.0;          // hunger per unit of grain
    double cow_milk_per_day = 4.0;
    double cow_milk_max = 8.0;
    double chicken_eggs_per_day = 1.0;
    double chicken_eggs_max = 3.0;
    double cow_meat = 20.0;
    double chicken_meat = 2.0;
    double slaughter_minutes_per_meat = 4.0;
    double milk_minutes_per_unit = 5.0;
    double meadow_hay_max = 60.0;
    double meadow_regrow_days = 4.0;             // from empty to full, warm season only
    double mow_minutes_per_unit = 10.0;
    double barn_build_wood = 30.0;
    double barn_build_hours = 8.0;
    double barn_capacity = 8.0;
    double hay_kg = 2.0;
    double milk_kg = 1.0;
    double eggs_kg = 0.1;
    double meat_kg = 1.0;
    double meat_hunger_relief = 30.0;
    double milk_hunger_relief = 15.0;
    double eggs_hunger_relief = 10.0;

    // Notice board (limited knowledge: characters share what they saw)
    double board_build_wood = 5.0;
    double board_build_hours = 1.0;
    double board_post_minutes_per_note = 1.0;  // me.post: per note written (at least one)
    double board_read_base_minutes = 1.0;      // me.read: looking at the board
    double board_read_per_note_minutes = 0.2;  // me.read: per note that was news

    // Loads a flat JSON object {"key": number, ...}; keys starting with "_" are comments.
    // Returns false and sets `error` when the file can't be read or parsed.
    // Unknown keys are reported in `warnings` and ignored.
    bool load_json_file(const std::string& path, std::string& error, std::vector<std::string>& warnings);
    bool load_json_text(const std::string& text, std::string& error, std::vector<std::string>& warnings);
};

}  // namespace kindle
