#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "balance.h"

namespace kindle {

constexpr double kDay = 86400.0;
constexpr double kEps = 1e-6;
constexpr double kInf = std::numeric_limits<double>::infinity();

struct Vec2 {
    double x = 0, y = 0;
};

enum class ObjKind { Storage, House, Forest, Field, Person, Board, Barn, Meadow, Animal };
// "storage", "house", "forest", "field", "person", "board", "barn", "meadow", "animal" (Python prelude, saves)
const char* kind_name(ObjKind k);

enum class Season { Warm, Winter };
const char* season_name(Season s);  // "WARM" / "WINTER" (values of Season.* in Python)

enum class FieldState { Empty, Sown, Ripe };
const char* field_state_name(FieldState s);  // "empty" / "sown" / "ripe"

// What a character is doing during a slice of time; drives needs (see Balance).
enum class Activity { Idle, Wait, Light, Heavy, Rest, SleepHome, SleepGround };

enum class LogKind { Info, Notify, Print, Warning, Error, Death, Outcome };  // Outcome: the first winter survived / all dead
const char* log_kind_name(LogKind k);  // "info", "notify", "print", "warning", "error", "death", "outcome" (saves, Godot)

struct LogEntry {
    double time = 0;
    int object_id = -1;  // character object id, or -1 for world events
    LogKind kind = LogKind::Info;
    std::string text;
    std::string detail;  // e.g. a full traceback
};

// One tool: lies in a storage or is held by a character. It wears with work and is broken at condition 0
// (there is no repair yet). Kinds: "axe", "sickle"; an empty kind means "no tool".
struct ToolItem {
    int id = -1;
    std::string kind;
    int quality = 1;         // 1..5 (only 1 exists until tools can be made)
    double condition = 100;  // %, 0 = broken
    int reserved_by = -1;    // in a storage: the character on its way to take it
    bool empty() const { return kind.empty(); }
    bool broken() const { return !kind.empty() && condition <= 0; }
};

// Every named thing in the world. Kind-specific fields are unused for other kinds.
// Clothes are identified by strings: "clothes", "warm_clothes".
struct Object {
    int id = -1;
    ObjKind kind = ObjKind::Storage;
    std::string name;
    Vec2 pos;
    // Storage
    std::map<std::string, int> resources;       // "wood", "bread", "grain", "flour"
    std::vector<ToolItem> tools;                // tools lying in the storage (whole and broken)
    // House: stove (heating and baking), chest, hand mill
    double firewood = 0;     // units of wood in the stove; it burns while > 0
    double temperature = 0;  // inside, °C
    std::map<std::string, int> chest;  // clothes kept in the house
    // Forest
    int trees_total = 0;
    int wood_taken = 0;  // units of wood chopped so far
    // Field
    FieldState field_state = FieldState::Empty;
    bool seeded = false;      // seed grain is in the ground (sowing started)
    double sow_done = 0;      // seconds of sowing work done
    double growth = 0;        // 0..1 while sown
    int grain_left = 0;       // ripe grain still standing
    // Person
    int char_index = -1;
    // Animal (cow / chicken): lives in its barn (`home`) and stands at it
    std::string species;     // "cow" / "chicken"
    int home = -1;
    double hunger = 0;
    double health = 100;
    bool alive = true;       // a dead animal stays in the barn
    bool removed = false;    // slaughtered: gone from the world
    double produce = 0;      // cow: milk ready (units); chicken: eggs laid (units)
    bool hunger_warned = false;
    std::string death_cause;
    // Meadow
    double hay = 0;          // standing grass, units of hay; regrows in the warm season
    // Buildings (storage, house, field, board, barn): a blueprint is a construction site until `built`
    bool built = true;
    std::map<std::string, int> materials_needed;
    std::map<std::string, int> materials_delivered;
    double build_done = 0;   // seconds of construction work done (shared by all builders)
    double build_total = 0;  // seconds of work needed
    int capacity = 0;        // house: max residents; barn: max animals
    int mill_user = -1;      // house: character at the hand mill (one workplace)
    int oven_user = -1;      // house: character at the oven (one workplace)
};

bool is_building(ObjKind k);  // storage, house, field, board, barn: can be placed as a blueprint

// What one sees of another character: needs and items (position, name and home are public).
struct PersonView {
    double hunger = 0, fatigue = 0, health = 0, warmth = 0;
    bool alive = true;
    ToolItem tool;
    std::string clothes;
    std::map<std::string, int> carrying;
};

// What a character knows about an object (limited knowledge, План_гри.md §1): a copy of its state
// as seen at `time`. Characters see an object only while standing at it; the notice board passes
// such notes between characters. Public facts (name, position, is_built, capacity, residents, home)
// are not part of it.
struct Knowledge {
    double time = -1;  // game time of the observation
    Object state;      // the object as it was (for a person: its Object)
    PersonView person; // for a person
};

struct Character {
    int obj = -1;  // object id (position lives in the Object)
    std::string name;
    double hunger = 0, fatigue = 0, health = 100, warmth = 100;
    bool alive = true;
    ToolItem tool;        // in hands (empty kind = none)
    std::string clothes;  // "" / "clothes" / "warm_clothes"
    std::map<std::string, int> carrying;
    int home = -1;  // house object id
    // Absolute game time this character has been simulated up to. Within a tick
    // every character advances independently; Simulation::tick() syncs them.
    double local_time = 0;
    double last_action_time = 0;
    std::map<int, double> work_partial;  // forest/field id -> progress of the unit being gathered (0..1)
    std::vector<int> action_stack;       // active actions; more than one while an @on handler runs
    std::map<int, Knowledge> known;      // object id -> what this character knows about it
    // Log bookkeeping
    bool idle_warned = false, hunger_warned = false, fatigue_warned = false, cold_warned = false;
    int health_warn_level = 100;
    double death_time = -1;
    std::string death_cause;
};

struct ActionParams {
    std::optional<int> amount;
    std::optional<double> until;     // time of day, seconds since midnight
    std::optional<double> duration;  // hours=, in seconds
    std::optional<double> seconds;   // wait: plain duration
    std::optional<double> timeout;   // wait: timeout, seconds
    std::string tool;
    int tool_id = -1;  // take: a particular tool (-1 = the best whole one of `tool`)
    std::string item;  // clothes for wear()
    std::string resource;
    int count = 0;
    std::vector<int> objects;  // post: what to write on the board (empty = everything the character knows)
};

// A failed action; `type` is the Python exception class name (NoItemError, ...).
struct ActionError {
    std::string type;
    std::string reason;
    double done = 0;
};

enum class ValueKind { None, Int, Bool };
struct ActionValue {
    ValueKind kind = ValueKind::None;
    long long i = 0;
    bool b = false;
};

struct BeginResult {
    int action_id = -1;
    std::optional<ActionError> error;
};

enum class StepStatus { Running, Done, Failed };
struct StepResult {
    StepStatus status = StepStatus::Running;
    double used = 0;  // seconds consumed; equals the budget while Running
    ActionValue value;
    ActionError error;
};

enum class ActionKind {
    GoTo, Take, PutBack, ChopWood, Drop, PickUp, Eat, Sleep, Rest, Wait,
    Heat, Wear, Sow, Harvest, Grind, Bake,
    Deliver, Build, MoveInto,
    Post, Read,
    Mow, Feed, Milk, CollectEggs, Slaughter,
};

struct Action {
    int id = -1;
    int cid = -1;      // character object id
    std::string name;  // API name: "chop_wood", "go_home", ...
    ActionKind kind = ActionKind::Wait;
    int target = -1;
    ActionParams params;
    double deadline = kInf;  // from until= / hours=
    bool has_limits = false;
    int done = 0;
    double progress = 0;       // seconds spent in the fixed-length phase
    double handle_total = 0;   // length of that phase
    bool noop = false;
    bool transferred = false;
    bool reserved = false;     // holds a reservation of the tool params.tool_id in `target`
    double wait_target = kInf;
    double wait_timeout = kInf;
    bool wait_returns_bool = false;
};

struct RainInterval {
    double start, end;
};

class World {
public:
    World(const Balance& balance, uint64_t seed);

    const Balance& balance() const { return balance_; }

    // --- scene building (before the first tick)
    // `tools`: how many new tools of each kind lie in the storage.
    int add_storage(const std::string& name, Vec2 pos, const std::map<std::string, int>& tools,
                    const std::map<std::string, int>& resources);
    ToolItem make_tool(const std::string& kind);  // a new tool (quality 1, condition 100) with its own id
    int add_house(const std::string& name, Vec2 pos, const std::map<std::string, int>& chest);
    int add_forest(const std::string& name, Vec2 pos, int trees);
    int add_field(const std::string& name, Vec2 pos);
    int add_character(const std::string& name, int home, Vec2 pos, double hunger, double fatigue, double health);
    int add_board(const std::string& name, Vec2 pos);
    int add_barn(const std::string& name, Vec2 pos);    // a finished barn (scene; the game builds them)
    int add_meadow(const std::string& name, Vec2 pos);  // full of grass
    // A new animal ("cow" / "chicken") in a built barn with a free place — for trade (bought animals
    // go straight into a barn) and tests. Named "Корова_N" / "Курка_N". Returns its id, or -1 and `error`.
    int add_animal(const std::string& species, int barn, std::string& error);
    void set_start_time(double t);
    // Start knowledge: every character knows what it sees where it stands (its house, housemates).
    void init_knowledge();
    void set_random_rain(bool enabled) { random_rain_ = enabled; }
    void add_rain(double start, double end);
    // A blueprint placed by the player (any time): returns its id, or -1 and sets `error`.
    int add_blueprint(ObjKind kind, const std::string& name, Vec2 pos, std::string& error);

    // Player's global variables: characters read them, only the player changes them.
    using GlobalValue = std::variant<bool, long long, double, std::string>;
    void set_global(const std::string& name, const GlobalValue& v) { globals_[name] = v; }
    bool remove_global(const std::string& name) { return globals_.erase(name) > 0; }
    const std::map<std::string, GlobalValue>& globals() const { return globals_; }

    // --- queries
    double now() const { return now_; }
    const Object* find(const std::string& name) const;
    Object* object(int id);
    const std::vector<Object>& objects() const { return objects_; }
    Character& character(int obj_id);
    std::vector<Character>& characters() { return chars_; }
    int char_obj_id(int index) const { return chars_.at(index).obj; }
    double carry_weight(const Character& c) const;
    double walk_speed(const Character& c) const;  // meters per second
    double travel_seconds(const Character& c, Vec2 to) const;
    int forest_units_left(const Object& f) const;
    int forest_trees_left(const Object& f) const;
    std::map<std::string, int> materials_missing(const Object& o) const;
    int residents_count(int house_id) const;  // living characters whose home is this house
    int animals_count(int barn_id) const;     // living animals in this barn
    bool is_raining_at(double t) const;
    bool is_night_at(double t) const;
    Season season_at(double t) const;
    int season_days_left(double t) const;  // days until the season changes, today included
    double outdoor_temperature(double t) const;
    double environment_temperature(const Character& c) const;  // inside a house or outside
    // An unheated house shelters from the cold (+house_shelter_bonus) but is never warmer than a heated one.
    double unheated_house_temperature(double t) const;
    double food_relief(const std::string& resource) const;  // hunger one unit takes away; 0 = not food
    bool is_resource(const std::string& resource) const;
    double resource_kg(const std::string& resource) const;
    double resolve_time_of_day(double tod, double now) const;
    std::string format_time(double t) const;

    // --- limited knowledge
    // What `viewer` knows about object `obj_id`; refreshed first while the viewer stands at it.
    // nullptr if the viewer has never seen it.
    const Knowledge* knowledge(Character& viewer, int obj_id);
    void observe(Character& c);                 // learn everything at the character's spot
    bool sees(const Character& c, const Object& o) const;  // standing at it right now
    void learn(Character& c, const Object& o);  // a fresh note about `o` in c's memory
    const std::map<int, Knowledge>& board_notes(int board_id) const;  // notes on a notice board

    // --- outcome of the game (MVP goal: survive the first winter)
    int alive_count() const;
    int winters_survived() const { return winters_survived_; }  // winters that ended with someone alive

    // --- time
    std::vector<std::string> begin_tick(double t0, double t1);  // returns events that fired in [t0, t1)
    void end_tick(double t1) { now_ = t1; }
    void pass_time(Character& c, double seconds, Activity a);

    // --- actions (called by the script runtime from inside a character's coroutine)
    BeginResult begin_action(int char_obj, const std::string& kind, int target, const ActionParams& p);
    StepResult step_action(int action_id, double budget);
    void end_action(int action_id);
    void cancel_actions(int char_obj);
    const Action* current_action(int char_obj);  // innermost running action (nullptr while thinking/idle)

    // --- event log
    void log(int obj, LogKind kind, const std::string& text, const std::string& detail = {}, double time = -1);
    const std::vector<LogEntry>& log_entries() const { return log_; }
    std::function<void(const LogEntry&)> on_log;

private:
    friend struct SaveAccess;  // core/save.cpp

    int add_object(ObjKind kind, const std::string& name, Vec2 pos);
    double random01();
    void ensure_weather(double until);
    void update_environment(double t0, double t1);
    double fatigue_rate(Activity a) const;
    const Object* house_at(const Character& c) const;
    bool at(const Character& c, Vec2 p) const;
    double walk(Character& c, Vec2 dest, double max_t);
    double handle(Character& c, Action& a, double total, double max_t, Activity act, bool& finished);
    StepResult step_gather(Action& a, Character& c, double budget);
    StepResult step_convert(Action& a, Character& c, double budget);
    StepResult step_sow(Action& a, Character& c, double budget);
    StepResult step_build(Action& a, Character& c, double budget);
    void complete_building(Object& o, int builder);
    StepResult step_recover(Action& a, Character& c, Activity act, double budget, double used);
    StepResult step_action_impl(Action& a, Character& c, double budget);
    StepResult step_board(Action& a, Character& c, double budget);
    StepResult step_animal(Action& a, Character& c, double budget);  // feed, milk, collect eggs, slaughter
    void update_animal(Object& a, double t0, double dt);
    void check_thresholds(Character& c);
    void die(Character& c, const std::string& cause);
    void winter_ended(double t);
    ToolItem* tool_in(Object& storage, int tool_id);
    bool wear_tool(Character& c, double seconds);  // returns true when the tool broke just now

    Balance balance_;
    std::mt19937_64 rng_;
    double now_ = 0;
    std::vector<Object> objects_;
    std::unordered_map<std::string, int> by_name_;
    std::vector<Character> chars_;
    std::unordered_map<int, Action> actions_;
    int next_action_id_ = 1;
    bool random_rain_ = true;
    int weather_days_ = 0;  // days with generated weather
    std::vector<RainInterval> rain_;
    std::vector<LogEntry> log_;
    std::map<std::string, GlobalValue> globals_;
    int winters_survived_ = 0;
    std::map<int, std::map<int, Knowledge>> boards_;  // board id -> object id -> the note about it
    int next_tool_id_ = 1;
};

}  // namespace kindle
