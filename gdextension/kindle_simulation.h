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
    // Options: characters, axes, sickles, bread, grain, wood, trees, warm_clothes, random_rain, seed.
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

    // [{id, kind, name, position: Vector2 (meters), ...}] plus, by kind:
    //   storage: resources, tools; house: firewood, temperature, heated, chest, capacity, residents;
    //   forest: trees_left, trees_total; field: state, growth, grain_left;
    //   every building: built, progress (0..1), needs (missing materials), materials_needed
    godot::Array get_buildings() const;
    // [{id, name, position, alive, hunger, fatigue, health, warmth, tool, clothes, carrying, home, state,
    //   state_text, line, line_file ("own" / "shared" / ""), action, action_target, death_cause}]
    godot::Array get_characters() const;

    // Blueprint placed by the player: kind "house" / "storage" / "field", position in meters.
    // Returns "" or the reason why it can't be placed.
    godot::String add_blueprint(const godot::String& kind, const godot::String& name, const godot::Vector2& position);
    // {"house": {"wood": 40, "hours": 16}, ...}
    godot::Dictionary get_build_costs() const;

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
