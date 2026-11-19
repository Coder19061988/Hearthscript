#include "balance.h"

#include <fstream>
#include <sstream>

#include "json.hpp"

namespace kindle {

namespace {

struct Field {
    const char* key;
    double Balance::*ptr;
};

#define KINDLE_FIELD(name) {#name, &Balance::name}
const Field kFields[] = {
    KINDLE_FIELD(day_real_minutes),
    KINDLE_FIELD(night_start_hour),
    KINDLE_FIELD(night_end_hour),
    KINDLE_FIELD(think_seconds_per_line),
    KINDLE_FIELD(idle_log_hours),
    KINDLE_FIELD(time_of_day_grace_minutes),
    KINDLE_FIELD(walk_meters_per_minute),
    KINDLE_FIELD(walk_loaded_meters_per_minute),
    KINDLE_FIELD(weight_limit_kg),
    KINDLE_FIELD(wood_kg),
    KINDLE_FIELD(bread_kg),
    KINDLE_FIELD(tool_take_minutes),
    KINDLE_FIELD(tool_wear_per_hour),
    KINDLE_FIELD(tool_worn_warning),
    KINDLE_FIELD(dismantle_minutes),
    KINDLE_FIELD(axe_scrap),
    KINDLE_FIELD(scythe_scrap),
    KINDLE_FIELD(sickle_scrap),
    KINDLE_FIELD(dismantle_wood),
    KINDLE_FIELD(scrap_kg),
    KINDLE_FIELD(transfer_base_minutes),
    KINDLE_FIELD(transfer_per_unit_minutes),
    KINDLE_FIELD(chop_minutes_per_unit),
    KINDLE_FIELD(eat_minutes),
    KINDLE_FIELD(wood_per_tree),
    KINDLE_FIELD(hunger_per_hour),
    KINDLE_FIELD(bread_hunger_relief),
    KINDLE_FIELD(hunger_damage_threshold),
    KINDLE_FIELD(hunger_damage_per_hour),
    KINDLE_FIELD(hunger_full_damage_per_hour),
    KINDLE_FIELD(fatigue_heavy_per_hour),
    KINDLE_FIELD(fatigue_light_per_hour),
    KINDLE_FIELD(fatigue_rest_per_hour),
    KINDLE_FIELD(fatigue_sleep_home_per_hour),
    KINDLE_FIELD(fatigue_sleep_ground_per_hour),
    KINDLE_FIELD(fatigue_slow_threshold),
    KINDLE_FIELD(fatigue_slow_factor),
    KINDLE_FIELD(fatigue_full_damage_per_hour),
    KINDLE_FIELD(health_regen_per_hour),
    KINDLE_FIELD(health_regen_max_hunger),
    KINDLE_FIELD(health_regen_max_fatigue),
    KINDLE_FIELD(rain_chance_per_day),
    KINDLE_FIELD(rain_min_hours),
    KINDLE_FIELD(rain_max_hours),
    KINDLE_FIELD(rain_earliest_start_hour),
    KINDLE_FIELD(rain_latest_start_hour),
    KINDLE_FIELD(rain_work_factor),
    KINDLE_FIELD(rain_extra_fatigue_per_hour),
    KINDLE_FIELD(season_warm_days),
    KINDLE_FIELD(season_winter_days),
    KINDLE_FIELD(temp_warm_avg),
    KINDLE_FIELD(temp_winter_avg),
    KINDLE_FIELD(temp_day_amplitude),
    KINDLE_FIELD(temp_rain_drop),
    KINDLE_FIELD(comfort_temperature),
    KINDLE_FIELD(warmth_recover_per_hour),
    KINDLE_FIELD(warmth_loss_per_degree_hour),
    KINDLE_FIELD(clothes_bonus),
    KINDLE_FIELD(warm_clothes_bonus),
    KINDLE_FIELD(bed_bonus),
    KINDLE_FIELD(work_heat_bonus),
    KINDLE_FIELD(warmth_damage_threshold),
    KINDLE_FIELD(warmth_damage_per_hour),
    KINDLE_FIELD(warmth_zero_damage_per_hour),
    KINDLE_FIELD(health_regen_min_warmth),
    KINDLE_FIELD(wear_minutes),
    KINDLE_FIELD(house_heated_temperature),
    KINDLE_FIELD(house_shelter_bonus),
    KINDLE_FIELD(house_warmup_hours),
    KINDLE_FIELD(house_cooldown_hours),
    KINDLE_FIELD(firewood_burn_hours),
    KINDLE_FIELD(field_seed_grain),
    KINDLE_FIELD(field_sow_hours),
    KINDLE_FIELD(field_grow_days),
    KINDLE_FIELD(field_yield_grain),
    KINDLE_FIELD(field_harvest_hours),
    KINDLE_FIELD(grain_kg),
    KINDLE_FIELD(flour_kg),
    KINDLE_FIELD(house_capacity),
    KINDLE_FIELD(house_build_wood),
    KINDLE_FIELD(house_build_hours),
    KINDLE_FIELD(storage_build_wood),
    KINDLE_FIELD(storage_build_hours),
    KINDLE_FIELD(field_build_hours),
    KINDLE_FIELD(move_in_minutes),
    KINDLE_FIELD(blueprint_min_distance),
    KINDLE_FIELD(animal_hunger_per_hour),
    KINDLE_FIELD(animal_graze_per_hour),
    KINDLE_FIELD(animal_hunger_damage_threshold),
    KINDLE_FIELD(animal_hunger_damage_per_hour),
    KINDLE_FIELD(animal_health_regen_per_hour),
    KINDLE_FIELD(animal_produce_max_hunger),
    KINDLE_FIELD(cow_hay_relief),
    KINDLE_FIELD(chicken_grain_relief),
    KINDLE_FIELD(cow_milk_per_day),
    KINDLE_FIELD(cow_milk_max),
    KINDLE_FIELD(chicken_eggs_per_day),
    KINDLE_FIELD(chicken_eggs_max),
    KINDLE_FIELD(cow_meat),
    KINDLE_FIELD(chicken_meat),
    KINDLE_FIELD(slaughter_minutes_per_meat),
    KINDLE_FIELD(milk_minutes_per_unit),
    KINDLE_FIELD(meadow_hay_max),
    KINDLE_FIELD(meadow_regrow_days),
    KINDLE_FIELD(mow_minutes_per_unit),
    KINDLE_FIELD(barn_build_wood),
    KINDLE_FIELD(barn_build_hours),
    KINDLE_FIELD(barn_capacity),
    KINDLE_FIELD(hay_kg),
    KINDLE_FIELD(milk_kg),
    KINDLE_FIELD(eggs_kg),
    KINDLE_FIELD(meat_kg),
    KINDLE_FIELD(meat_hunger_relief),
    KINDLE_FIELD(milk_hunger_relief),
    KINDLE_FIELD(eggs_hunger_relief),
    KINDLE_FIELD(start_coins),
    KINDLE_FIELD(merchant_first_day),
    KINDLE_FIELD(merchant_interval_days),
    KINDLE_FIELD(merchant_arrive_hour),
    KINDLE_FIELD(merchant_leave_hour),
    KINDLE_FIELD(merchant_price_spread),
    KINDLE_FIELD(merchant_offer_share),
    KINDLE_FIELD(trading_post_build_wood),
    KINDLE_FIELD(trading_post_build_hours),
    KINDLE_FIELD(order_minutes),
    KINDLE_FIELD(clothes_kg),
    KINDLE_FIELD(value_wood),
    KINDLE_FIELD(value_grain),
    KINDLE_FIELD(value_flour),
    KINDLE_FIELD(value_bread),
    KINDLE_FIELD(value_hay),
    KINDLE_FIELD(value_milk),
    KINDLE_FIELD(value_eggs),
    KINDLE_FIELD(value_meat),
    KINDLE_FIELD(value_clothes),
    KINDLE_FIELD(value_warm_clothes),
    KINDLE_FIELD(value_axe),
    KINDLE_FIELD(value_sickle),
    KINDLE_FIELD(value_scythe),
    KINDLE_FIELD(value_chicken),
    KINDLE_FIELD(value_cow),
    KINDLE_FIELD(value_scrap),
    KINDLE_FIELD(flax_seed),
    KINDLE_FIELD(flax_yield),
    KINDLE_FIELD(flax_kg),
    KINDLE_FIELD(cloth_kg),
    KINDLE_FIELD(rags_kg),
    KINDLE_FIELD(clothes_wear_per_day),
    KINDLE_FIELD(warm_clothes_wear_per_day),
    KINDLE_FIELD(clothes_worn_warning),
    KINDLE_FIELD(value_flax),
    KINDLE_FIELD(value_cloth),
    KINDLE_FIELD(value_rags),
    KINDLE_FIELD(planks_kg),
    KINDLE_FIELD(bed_kg),
    KINDLE_FIELD(fatigue_sleep_bed_per_hour),
    KINDLE_FIELD(value_planks),
    KINDLE_FIELD(value_bed),
    KINDLE_FIELD(quarry_stone),
    KINDLE_FIELD(mine_ore),
    KINDLE_FIELD(mine_stone_minutes),
    KINDLE_FIELD(mine_ore_minutes),
    KINDLE_FIELD(stone_kg),
    KINDLE_FIELD(ore_kg),
    KINDLE_FIELD(charcoal_kg),
    KINDLE_FIELD(iron_kg),
    KINDLE_FIELD(pickaxe_scrap),
    KINDLE_FIELD(value_stone),
    KINDLE_FIELD(value_ore),
    KINDLE_FIELD(value_charcoal),
    KINDLE_FIELD(value_iron),
    KINDLE_FIELD(value_pickaxe),
    KINDLE_FIELD(water_kg),
    KINDLE_FIELD(field_water_units),
    KINDLE_FIELD(field_water_minutes),
    KINDLE_FIELD(field_water_days),
    KINDLE_FIELD(field_water_growth_bonus),
    KINDLE_FIELD(field_rain_soak_hours),
    KINDLE_FIELD(illness_hours),
    KINDLE_FIELD(illness_treated_hours),
    KINDLE_FIELD(illness_damage_per_hour),
    KINDLE_FIELD(illness_rest_damage_per_hour),
    KINDLE_FIELD(illness_fatigue_per_hour),
    KINDLE_FIELD(illness_work_factor),
    KINDLE_FIELD(illness_cold_per_hour),
    KINDLE_FIELD(illness_hunger_per_hour),
    KINDLE_FIELD(illness_exhaustion_per_hour),
    KINDLE_FIELD(illness_rain_per_hour),
    KINDLE_FIELD(injury_hours),
    KINDLE_FIELD(injury_treated_hours),
    KINDLE_FIELD(injury_work_factor),
    KINDLE_FIELD(injury_fatigue_threshold),
    KINDLE_FIELD(injury_fatigue_per_hour),
    KINDLE_FIELD(injury_dark_per_hour),
    KINDLE_FIELD(injury_mine_per_hour),
    KINDLE_FIELD(injury_forge_per_hour),
    KINDLE_FIELD(herbs_minutes),
    KINDLE_FIELD(herbs_kg),
    KINDLE_FIELD(bandage_kg),
    KINDLE_FIELD(treat_minutes),
    KINDLE_FIELD(treat_herbs),
    KINDLE_FIELD(value_herbs),
    KINDLE_FIELD(value_bandage),
    KINDLE_FIELD(mood_start),
    KINDLE_FIELD(mood_heavy_per_hour),
    KINDLE_FIELD(mood_light_per_hour),
    KINDLE_FIELD(mood_rest_per_hour),
    KINDLE_FIELD(mood_bed_per_hour),
    KINDLE_FIELD(mood_ground_per_hour),
    KINDLE_FIELD(mood_hunger_per_hour),
    KINDLE_FIELD(mood_cold_per_hour),
    KINDLE_FIELD(mood_exhaustion_per_hour),
    KINDLE_FIELD(mood_ailment_per_hour),
    KINDLE_FIELD(mood_body_per_hour),
    KINDLE_FIELD(mood_low_threshold),
    KINDLE_FIELD(mood_low_work_factor),
    KINDLE_FIELD(body_decay_hours),
    KINDLE_FIELD(illness_body_per_hour),
    KINDLE_FIELD(service_minutes),
    KINDLE_FIELD(service_mood),
    KINDLE_FIELD(service_interval_hours),
    KINDLE_FIELD(burial_minutes),
    KINDLE_FIELD(lantern_build_wood),
    KINDLE_FIELD(lantern_build_hours),
    KINDLE_FIELD(lantern_radius_m),
    KINDLE_FIELD(lantern_oil_hours),
    KINDLE_FIELD(torch_hours),
    KINDLE_FIELD(light_minutes),
    KINDLE_FIELD(oil_kg),
    KINDLE_FIELD(torch_kg),
    KINDLE_FIELD(value_oil),
    KINDLE_FIELD(value_torch),
    KINDLE_FIELD(bread_spoil_per_day),
    KINDLE_FIELD(milk_spoil_per_day),
    KINDLE_FIELD(eggs_spoil_per_day),
    KINDLE_FIELD(meat_spoil_per_day),
    KINDLE_FIELD(smoked_meat_spoil_per_day),
    KINDLE_FIELD(cheese_spoil_per_day),
    KINDLE_FIELD(winter_spoil_factor),
    KINDLE_FIELD(cellar_spoil_factor),
    KINDLE_FIELD(cellar_build_wood),
    KINDLE_FIELD(cellar_build_stone),
    KINDLE_FIELD(cellar_build_hours),
    KINDLE_FIELD(smoked_meat_kg),
    KINDLE_FIELD(cheese_kg),
    KINDLE_FIELD(smoked_meat_hunger_relief),
    KINDLE_FIELD(cheese_hunger_relief),
    KINDLE_FIELD(value_smoked_meat),
    KINDLE_FIELD(value_cheese),
    KINDLE_FIELD(diet_days),
    KINDLE_FIELD(diet_variety_kinds),
    KINDLE_FIELD(diet_variety_mood_per_hour),
    KINDLE_FIELD(diet_monotony_mood_per_hour),
    KINDLE_FIELD(sapling_kg),
    KINDLE_FIELD(plant_minutes),
    KINDLE_FIELD(tree_grow_days),
    KINDLE_FIELD(value_sapling),
    KINDLE_FIELD(bath_minutes),
    KINDLE_FIELD(bath_water),
    KINDLE_FIELD(bath_fatigue),
    KINDLE_FIELD(bath_mood),
    KINDLE_FIELD(bath_clean_hours),
    KINDLE_FIELD(bath_illness_factor),
    KINDLE_FIELD(bath_interval_hours),
    KINDLE_FIELD(beer_kg),
    KINDLE_FIELD(beer_mood),
    KINDLE_FIELD(beer_hunger_relief),
    KINDLE_FIELD(drink_minutes),
    KINDLE_FIELD(hangover_beers),
    KINDLE_FIELD(hangover_hours),
    KINDLE_FIELD(hangover_work_factor),
    KINDLE_FIELD(injury_hangover_per_hour),
    KINDLE_FIELD(tavern_rest_mood_per_hour),
    KINDLE_FIELD(value_beer),
    KINDLE_FIELD(skill_gain_per_hour),
    KINDLE_FIELD(skill_speed_bonus),
    KINDLE_FIELD(skill_injury_reduction),
    KINDLE_FIELD(mentor_gap),
    KINDLE_FIELD(mentor_factor),
    KINDLE_FIELD(mentor_distance_m),
    KINDLE_FIELD(tool_quality2_skill),
    KINDLE_FIELD(tool_quality3_skill),
    KINDLE_FIELD(teach_min_skill),
    KINDLE_FIELD(teach_minutes),
    KINDLE_FIELD(lesson_gain),
    KINDLE_FIELD(lesson_max_share),
    KINDLE_FIELD(start_skill_main),
    KINDLE_FIELD(start_skill_second),
    KINDLE_FIELD(board_build_wood),
    KINDLE_FIELD(board_build_hours),
    KINDLE_FIELD(board_post_minutes_per_note),
    KINDLE_FIELD(board_read_base_minutes),
    KINDLE_FIELD(board_read_per_note_minutes),
};
#undef KINDLE_FIELD

// Minimal parser for a flat JSON object whose values are numbers or strings.
// "recipes": {"flour": {"at": "house", "workplace": "mill", "inputs": {"grain": 1}, "minutes": 3, "heavy": true}, ...}
bool load_recipe(const nlohmann::json& r, const std::string& where, Recipe& rec, std::string& error) {
    if (!r.is_object() || !r.contains("at") || !r.contains("workplace") || !r.contains("inputs") || !r.contains("minutes")) {
        error = where + "потрібні at, workplace, inputs і minutes";
        return false;
    }
    rec.at = r.at("at").get<std::string>();
    rec.workplace = r.at("workplace").get<std::string>();
    rec.minutes = r.at("minutes").get<double>();
    rec.fire = r.value("fire", false);
    rec.heavy = r.value("heavy", false);
    for (const auto& [res, n] : r.at("inputs").items()) rec.inputs[res] = n.get<int>();
    if (rec.minutes <= 0) {  // inputs may be empty: water from the well
        error = where + "minutes має бути більше нуля";
        return false;
    }
    return true;
}

// "recipes": {"flour": {"at": "house", ...}, "iron": [{...from ore...}, {...from scrap...}]}
bool load_recipes(const nlohmann::json& j, std::map<std::string, std::vector<Recipe>>& out, std::string& error) {
    if (!j.is_object()) {
        error = "\"recipes\" має бути об'єктом";
        return false;
    }
    out.clear();
    for (const auto& [item, value] : j.items()) {
        const std::string where = "рецепт \"" + item + "\": ";
        const nlohmann::json variants = value.is_array() ? value : nlohmann::json::array({value});
        for (const auto& r : variants) {
            Recipe rec;
            if (!load_recipe(r, where, rec, error)) return false;
            out[item].push_back(rec);
        }
    }
    return true;
}

// "workshops": {"smithy": {"name": "Кузня", "materials": {"wood": 40}, "hours": 12}, ...}
bool load_workshops(const nlohmann::json& j, std::map<std::string, WorkshopType>& out, std::string& error) {
    if (!j.is_object()) {
        error = "\"workshops\" має бути об'єктом";
        return false;
    }
    out.clear();
    for (const auto& [type, w] : j.items()) {
        if (!w.is_object() || !w.contains("name") || !w.contains("materials") || !w.contains("hours")) {
            error = "майстерня \"" + type + "\": потрібні name, materials і hours";
            return false;
        }
        WorkshopType t;
        t.name = w.at("name").get<std::string>();
        t.hours = w.at("hours").get<double>();
        t.stove = w.value("stove", false);
        for (const auto& [res, n] : w.at("materials").items()) t.materials[res] = n.get<int>();
        out[type] = t;
    }
    return true;
}

}  // namespace

bool Balance::load_json_file(const std::string& path, std::string& error, std::vector<std::string>& warnings) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "не вдалося відкрити " + path;
        return false;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    if (!load_json_text(buf.str(), error, warnings)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

bool Balance::load_json_text(const std::string& source, std::string& error, std::vector<std::string>& warnings) {
    std::string text = source;
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);  // UTF-8 BOM
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = "файл балансу — не JSON-об'єкт";
        return false;
    }
    try {
        for (const auto& [key, value] : j.items()) {
            if (!key.empty() && key[0] == '_') continue;  // comment
            if (key == "recipes") {
                if (!load_recipes(value, recipes, error)) return false;
                continue;
            }
            if (key == "workshops") {
                if (!load_workshops(value, workshops, error)) return false;
                continue;
            }
            bool known = false;
            for (const auto& f : kFields) {
                if (key == f.key) {
                    known = true;
                    if (value.is_number())
                        this->*f.ptr = value.get<double>();
                    else
                        warnings.push_back("параметр \"" + key + "\" має бути числом");
                    break;
                }
            }
            if (!known) warnings.push_back("невідомий параметр \"" + key + "\" (пропущено)");
        }
    } catch (const std::exception& e) {
        error = std::string("неправильні дані балансу: ") + e.what();
        return false;
    }
    return true;
}

}  // namespace kindle
