#pragma once

#include <memory>
#include <vector>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "simulation.h"

namespace kindle_godot {

// Godot-facing wrapper around kindle::Simulation (world + script runtime).
// GDScript drives time with advance() and reads state every frame for rendering and UI.
// Player code runs inside coroutines during advance(); nothing in there touches the Godot API,
// log entries are only queued and handed out by take_log().
class KindleSimulation : public godot::RefCounted {
    GDCLASS(KindleSimulation, godot::RefCounted)

public:
    // Balance JSON (same format as config/balance.json). Returns "" or a description of problems.
    godot::String load_balance_json(const godot::String& text);
    // New world with the start scene.
    // Options: characters, axes, sickles, bread, grain, wood, trees, warm_clothes, random_rain, seed;
    // for tests: start_day (1..), start_hour, hunger, health; barn (a finished "Хлів_1") with cows, chickens.
    void start(const godot::Dictionary& options);
    bool is_started() const { return sim_ != nullptr; }
    // Advances the world by `game_seconds` (split into ticks of at most 60 s).
    void advance(double game_seconds);

    double get_time_scale() const;   // game seconds per real second at x1
    double get_now() const;          // absolute game seconds since day 1, 00:00
    double get_time_of_day() const;  // seconds since midnight
    int get_day() const;
    godot::String get_time_text() const;
    bool is_raining() const;
    bool is_night() const;
    godot::String get_season() const;  // "WARM" / "WINTER"
    int get_season_days_left() const;
    double get_temperature() const;    // outside, °C
    // The MVP goal: {winters_survived, alive, total}. The first winter is survived when winters_survived >= 1;
    // the game is lost when alive == 0.
    godot::Dictionary get_outcome() const;

    // [{id, kind, name, position: Vector2 (meters), ...}] plus, by kind:
    //   storage: resources, tools (whole, by kind), broken_tools; house: firewood, temperature, heated, chest, capacity, residents;
    //   forest: trees_left, trees_total; deposit: resource, left; field: state, crop, watered, growth, grain_left; board: notes (count);
    //   barn: capacity, animals (count); meadow: hay, hay_max; town_hall: coins;
    //   trading_post: for_sale, received ({item: n}), orders ([[item, n]]), merchant_here; workshop: workshop (type);
    //   every building: built, progress (0..1), needs (missing materials), materials_needed
    godot::Array get_buildings() const;
    // [{id, name, position, alive, hunger, fatigue, health, warmth, tool, tool_condition (%, -1 = none), clothes, clothes_condition,
    //   carrying, home, state,
    //   action_item (craft: what is being made), state_text, line, line_file ("own" / "shared" / ""), action, action_target, death_cause, death_time (-1 while alive), death_time_text}]
    godot::Array get_characters() const;

    // Animals (slaughtered ones are gone): [{id, name, species ("cow" / "chicken"), position, home (barn id),
    //   alive, hunger, health, produce (milk / eggs ready)}]
    godot::Array get_animals() const;
    // The travelling merchant: {here, days_until, arrival_text, post (name while here), prices, offers}
    // — prices: what he sells, offers: what he pays, coins per unit.
    godot::Dictionary get_merchant() const;
    // Charts of the town hall: a sample every game hour. get_stats_size() is cheap — ask for get_stats() only
    // when it changes. {days (game day as a number, 1.0 = day 1 00:00), coins, people, health, hunger, warmth,
    //   fatigue, animals: PackedFloat64Array; stock: {resource: PackedFloat64Array} — those ever above 0}
    int get_stats_size() const;
    godot::Dictionary get_stats() const;
    // A new animal in a built barn with a free place (for trade); returns "" or why not.
    godot::String add_animal(const godot::String& species, const godot::String& barn);

    // Blueprint placed by the player: a key of get_build_costs() ("house", "barn", a workshop type...), in meters.
    // Returns "" or the reason why it can't be placed.
    godot::String add_blueprint(const godot::String& kind, const godot::String& name, const godot::Vector2& position);
    // What can be built, in the order of the "Будувати" menu (workshop types come from the balance):
    // {"house": {name, prefix (blueprints are named "<prefix>_N"), materials: {"wood": 40}, hours}, ...}
    godot::Dictionary get_build_costs() const;
    // What the character knows about buildings, fields and forests (limited knowledge):
    // [{name, kind, ago}] where ago = game seconds since it saw the object, -1 = never.
    godot::Array get_knowledge(int character_id) const;

    // Player's global variables (bool / int / float / String). set_global returns "" or an error.
    godot::String set_global(const godot::String& name, const godot::Variant& value);
    bool remove_global(const godot::String& name);
    godot::Dictionary get_globals() const;

    // Whole game as JSON text (see core/save.h).
    godot::String save_game();
    // Replaces the current game with a save; returns "" or why it failed (the current game stays then).
    godot::String load_game(const godot::String& text);

    // The character's own code (runs after the shared code; its definitions win). Restarts the character.
    void set_code(int character_id, const godot::String& code);
    godot::String get_code(int character_id) const;
    // The settlement's shared code; with `restart`, every living character starts its program again.
    void set_shared_code(const godot::String& code, bool restart = true);
    godot::String get_shared_code() const;
    // Log entries since the previous call: [{time_text, kind, who, text, detail}]
    godot::Array take_log();

protected:
    static void _bind_methods();

private:
    kindle::Balance balance_;
    std::unique_ptr<kindle::Simulation> sim_;
    std::vector<kindle::LogEntry> pending_log_;
};

}  // namespace kindle_godot
