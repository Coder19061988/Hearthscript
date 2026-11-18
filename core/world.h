#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "balance.h"

namespace kindle {

constexpr double kDay = 86400.0;
constexpr double kEps = 1e-6;
constexpr double kInf = std::numeric_limits<double>::infinity();

struct Vec2 {
    double x = 0, y = 0;
};

enum class ObjKind { Storage, House, Forest, Person };
const char* kind_name(ObjKind k);  // "storage", "house", "forest", "person" (used by the Python prelude)

// What a character is doing during a slice of time; drives needs (see Balance).
enum class Activity { Idle, Wait, Light, Heavy, Rest, SleepHome, SleepGround };

enum class LogKind { Info, Notify, Print, Warning, Error, Death };

struct LogEntry {
    double time = 0;
    int object_id = -1;  // character object id, or -1 for world events
    LogKind kind = LogKind::Info;
    std::string text;
    std::string detail;  // e.g. a full traceback
};

// Every named thing in the world. Kind-specific fields are unused for other kinds.
struct Object {
    int id = -1;
    ObjKind kind = ObjKind::Storage;
    std::string name;
    Vec2 pos;
    // Storage
    std::map<std::string, int> resources;
    int axes = 0;
    int axes_reserved = 0;
    // Forest
    int trees_total = 0;
    int wood_taken = 0;  // units of wood chopped so far
    // Person
    int char_index = -1;
};

struct Character {
    int obj = -1;  // object id (position lives in the Object)
    std::string name;
    double hunger = 0, fatigue = 0, health = 100;
    bool alive = true;
    std::string tool;  // "" or "axe"
    std::map<std::string, int> carrying;
    int home = -1;  // house object id
    // Absolute game time this character has been simulated up to. Within a tick
    // every character advances independently; Simulation::tick() syncs them.
    double local_time = 0;
    double last_action_time = 0;
    std::map<int, double> chop_partial;  // forest id -> progress of the unit being chopped (0..1)
    std::vector<int> action_stack;       // active actions; more than one while an @on handler runs
    // Log bookkeeping
    bool idle_warned = false, hunger_warned = false, fatigue_warned = false;
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
    std::string resource;
    int count = 0;
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

enum class ActionKind { GoTo, Take, PutBack, ChopWood, Drop, PickUp, Eat, Sleep, Rest, Wait };

struct Action {
    int id = -1;
    int cid = -1;  // character object id
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
    bool reserved = false;     // holds a reservation of a tool in `target`
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
    int add_storage(const std::string& name, Vec2 pos, int axes, const std::map<std::string, int>& resources);
    int add_house(const std::string& name, Vec2 pos);
    int add_forest(const std::string& name, Vec2 pos, int trees);
    int add_character(const std::string& name, int home, Vec2 pos, double hunger, double fatigue, double health);
    void set_start_time(double t);
    void set_random_rain(bool enabled) { random_rain_ = enabled; }
    void add_rain(double start, double end);

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
    bool is_raining_at(double t) const;
    bool is_night_at(double t) const;
    bool is_edible(const std::string& resource) const { return resource == "bread"; }
    bool is_resource(const std::string& resource) const { return resource == "wood" || resource == "bread"; }
    double resource_kg(const std::string& resource) const;
    double resolve_time_of_day(double tod, double now) const;
    std::string format_time(double t) const;

    // --- time
    std::vector<std::string> begin_tick(double t0, double t1);  // returns events that fired in [t0, t1)
    void end_tick(double t1) { now_ = t1; }
    void pass_time(Character& c, double seconds, Activity a);

    // --- actions (called by the script runtime from inside a character's coroutine)
    BeginResult begin_action(int char_obj, const std::string& kind, int target, const ActionParams& p);
    StepResult step_action(int action_id, double budget);
    void end_action(int action_id);
    void cancel_actions(int char_obj);

    // --- event log
    void log(int obj, LogKind kind, const std::string& text, const std::string& detail = {}, double time = -1);
    const std::vector<LogEntry>& log_entries() const { return log_; }
    std::function<void(const LogEntry&)> on_log;

private:
    int add_object(ObjKind kind, const std::string& name, Vec2 pos);
    double random01();
    void ensure_weather(double until);
    double fatigue_rate(Activity a) const;
    bool at(const Character& c, Vec2 p) const;
    double walk(Character& c, Vec2 dest, double max_t);
    double handle(Character& c, Action& a, double total, double max_t, Activity act, bool& finished);
    StepResult step_chop(Action& a, Character& c, double budget);
    StepResult step_recover(Action& a, Character& c, Activity act, double budget, double used);
    void check_thresholds(Character& c);
    void die(Character& c, const std::string& cause);

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
};

}  // namespace kindle
