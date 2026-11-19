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

enum class ObjKind { Storage, House, Forest, Field, Person, Board, Barn, Meadow, Animal, TownHall, TradingPost, Workshop, Deposit,
                     Lantern };
// "storage", "house", "forest", "field", "person", "board", "barn", "meadow", "animal", "town_hall",
// "trading_post", "workshop" (Python prelude, saves)
const char* kind_name(ObjKind k);

enum class Season { Warm, Winter };
const char* season_name(Season s);  // "WARM" / "WINTER" (values of Season.* in Python)

enum class FieldState { Empty, Sown, Ripe };
const char* field_state_name(FieldState s);  // "empty" / "sown" / "ripe"

// What a character is doing during a slice of time; drives needs (see Balance).
enum class Activity { Idle, Wait, Light, Heavy, Rest, SleepBed, SleepHome, SleepGround };

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

// A set of clothes: worn by a character or lying in a house chest. It wears while worn; at 0 it turns into rags.
struct Garment {
    std::string kind;        // "clothes" / "warm_clothes"; empty = nothing
    double condition = 100;  // %
    bool empty() const { return kind.empty(); }
};

// How many garments of every kind: {"warm_clothes": 2}.
inline std::map<std::string, int> garment_counts(const std::vector<Garment>& garments) {
    std::map<std::string, int> n;
    for (const Garment& g : garments) n[g.kind]++;
    return n;
}

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
    std::vector<Garment> chest;  // clothes kept in the house
    int beds = 0;                // house: beds (furniture); residents in the order they came get them
    // Forest
    int trees_total = 0;      // forest: trees that ever grew here (`capacity` — how many stand at most)
    std::vector<double> saplings;  // forest: growth of every planted sapling (0..1)
    int grown = 0;            // forest: saplings that became trees today, logged at midnight
    std::string deposit;      // deposit: "stone" (a quarry) / "ore" (a mine)
    int deposit_left = 0;     // deposit: units left
    int wood_taken = 0;  // units of wood chopped so far
    // Field
    FieldState field_state = FieldState::Empty;
    std::string crop;         // what is sown or ripe: "grain" / "flax"
    double watered_until = 0; // field: watered (by hand or by rain) until this time
    double death_time = -1;   // animal: when it died (an unburied body decays)
    int graves = 0;           // church: buried at its cemetery
    double last_service = -1; // church: when the last service ended
    bool cellar = false;      // storage: a cellar, food spoils slower (cellar_spoil_factor)
    std::map<std::string, double> spoil;  // storage: how far the next unit of each food has spoiled (0..1)
    std::map<std::string, int> spoiled;   // storage: spoiled today, logged at midnight
    double fuel = 0;          // lantern: oil left (units)
    bool lit = false;         // lantern: burning; goes out at dawn or when the oil ends
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
    // Town hall: the settlement's money
    int coins = 0;
    // Trading post: `resources` / `tools` hold what the merchant left (bought goods); these hold what waits for him
    std::map<std::string, int> for_sale;                  // resources and clothes brought for sale
    std::vector<ToolItem> sale_tools;                     // tools brought for sale
    std::vector<std::pair<std::string, int>> orders;      // item -> amount, for the next visit
    // Buildings (storage, house, field, board, barn): a blueprint is a construction site until `built`
    bool built = true;
    std::map<std::string, int> materials_needed;
    std::map<std::string, int> materials_delivered;
    double build_done = 0;   // seconds of construction work done (shared by all builders)
    double build_total = 0;  // seconds of work needed
    int capacity = 0;        // house: max residents; barn: max animals
    std::map<std::string, int> workplace_users;  // workshop: workplace ("mill", "oven", "forge") -> character at it
    std::string workshop;                        // workshop: its type ("smithy"), see Balance::workshops
};

bool is_building(ObjKind k);  // storage, house, field, board, barn, town hall, trading post: have `built`

// The travelling merchant (План_гри.md, «Торгівля»): visits the trading post; prices are known ahead.
// Items are resource names ("grain", "warm_clothes") and kinds of tools ("axe") and animals ("cow").
struct Merchant {
    double next_arrival = 0;  // the next (or the current) visit
    double leaves = 0;        // when that visit ends: he trades then
    bool here = false;
    int post = -1;            // the trading post of the current visit
    std::map<std::string, int> prices;  // what he sells, coins per unit, for that visit
    std::map<std::string, int> offers;  // what he pays per unit
};

// What one sees of another character: needs and items (position, name and home are public).
struct PersonView {
    double hunger = 0, fatigue = 0, health = 0, warmth = 0;
    bool alive = true;
    ToolItem tool;
    Garment clothes;
    std::map<std::string, int> carrying;
    double illness_left = 0, injury_left = 0;  // hours
    bool illness_treated = false, injury_treated = false;
    double mood = 0;
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
    Garment clothes;      // worn; empty kind = none
    std::map<std::string, int> carrying;
    // Illness and injury: hours until they pass (0 = healthy); treated in the hospital they pass sooner.
    double illness_left = 0, injury_left = 0;
    bool illness_treated = false, injury_treated = false;
    double mood = 0;      // 0..100; below mood_low_threshold the work is slower
    bool buried = false;  // dead and buried at a church; an unburied body decays (mood, illness)
    double torch_left = 0;  // hours left of the torch burning in the hands (another one is lit from `carrying`)
    std::map<std::string, double> ate;  // food -> when the character last ate it (the diet)
    double clean_until = 0;     // after a bath: lower chances to fall ill
    double last_bath = -1;
    double hangover_until = 0;  // too much beer: slower work, more injuries
    int drink_day = -1, beers = 0;  // mugs drunk on that day
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
    bool mood_warned = false;
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
    int variant = 0;   // craft: which recipe of `item` (chosen in begin_action)
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
    Heat, Wear, Sow, Harvest, Craft,
    Deliver, Build, MoveInto,
    Post, Read,
    Mow, Feed, Milk, CollectEggs, Slaughter, Mine, Water, GatherHerbs, Treat, HoldService, Bury, Light, Plant,
    Bathe, Drink,
    Order,
    Dismantle,
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
    int place = -1;            // bury: the church whose cemetery the body goes to
    double wait_target = kInf;
    double wait_timeout = kInf;
    bool wait_returns_bool = false;
};

// A snapshot of the settlement for the player's charts in the town hall window (not something characters know).
struct StatSample {
    double time = 0;
    std::map<std::string, int> stock;  // resources in all built storages together
    int coins = 0;                     // in the town hall
    int people = 0;                    // living characters
    double health = 0, hunger = 0, warmth = 0, fatigue = 0, mood = 0;  // averages over the living
    int animals = 0;                   // living animals
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
    int add_house(const std::string& name, Vec2 pos, const std::map<std::string, int>& chest);  // new garments by kind
    int add_forest(const std::string& name, Vec2 pos, int trees);
    int add_deposit(const std::string& name, Vec2 pos, const std::string& resource, int amount);
    int add_field(const std::string& name, Vec2 pos);
    int add_character(const std::string& name, int home, Vec2 pos, double hunger, double fatigue, double health);
    int add_board(const std::string& name, Vec2 pos);
    int add_lantern(const std::string& name, Vec2 pos);
    int add_barn(const std::string& name, Vec2 pos);    // a finished barn (scene; the game builds them)
    int add_town_hall(const std::string& name, Vec2 pos);    // with the start coins
    int add_trading_post(const std::string& name, Vec2 pos);  // a finished post (scene; the game builds them)
    // A finished workshop of a type from Balance::workshops (scene and tests; the game builds them from blueprints).
    int add_workshop(const std::string& type, const std::string& name, Vec2 pos);
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
    // For a workshop `workshop` is its type (Balance::workshops).
    // `workshop`: the workshop type of a Workshop, or "cellar" for a Storage that is a cellar.
    int add_blueprint(ObjKind kind, const std::string& name, Vec2 pos, std::string& error, const std::string& workshop = "");

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
    int forest_room(const Object& f) const;  // free places for saplings: capacity - standing trees - saplings
    std::map<std::string, int> materials_missing(const Object& o) const;
    int residents_count(int house_id) const;  // living characters whose home is this house
    bool has_bed(const Character& c) const;  // a bed in the character's house is theirs
    bool has_stove(const Object& o) const;   // a house or a workshop heated like one (the hospital)
    const Object* hospital_at(const Character& c) const;  // the hospital the character stands at
    const Object* nearest_church(Vec2 from) const;          // a built one; nullptr if there is none
    const Object* workshop_at(const Character& c, const std::string& type) const;  // a built one the character stands at
    bool is_body(const Object& o) const;  // a dead person or animal not buried yet
    bool lit_near(const Character& c) const;  // daylight, or a lit lantern within lantern_radius_m
    double spoil_per_day(const std::string& food) const;  // share of a stock spoiling a day (warm season, no cellar)
    std::vector<std::string> diet(const Character& c) const;  // kinds of food eaten over the last diet_days
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

    // --- trade
    const Merchant& merchant() const { return merchant_; }

    // --- statistics: a sample at the start and at the beginning of every game hour
    const std::vector<StatSample>& stats() const { return stats_; }
    bool merchant_sells(const std::string& item) const { return merchant_.prices.count(item) > 0; }

    // --- outcome of the game (MVP goal: survive the first winter)
    int alive_count() const;
    int winters_survived() const { return winters_survived_; }  // winters that ended with someone alive

    // --- time
    std::vector<std::string> begin_tick(double t0, double t1);  // returns events that fired in [t0, t1)
    void end_tick(double t1);
    // `danger`: extra chance per hour of an injury at this work (the mine, the forge).
    void pass_time(Character& c, double seconds, Activity a, double danger = 0);

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
    const Object* shelter_at(const Character& c) const;  // a house or a hospital: warm inside, dry in the rain
    double work_factor(const Character& c, bool outdoors) const;  // rain, fatigue, illness, injury
    void catch_ailments(Character& c, double hours, Activity a, double danger);
    bool works_in_dark(Character& c, double hours, Activity a);
    void spoil(Object& storage, double t, double dt);  // heavy work at night without light (burns a torch)
    bool chance(double expected);  // an event with this expected count happens (health_rng_)
    bool at(const Character& c, Vec2 p) const;
    double walk(Character& c, Vec2 dest, double max_t);
    double handle(Character& c, Action& a, double total, double max_t, Activity act, bool& finished);
    StepResult step_gather(Action& a, Character& c, double budget);
    StepResult step_craft(Action& a, Character& c, double budget);
    StepResult step_sow(Action& a, Character& c, double budget);
    StepResult step_build(Action& a, Character& c, double budget);
    void complete_building(Object& o, int builder);
    StepResult step_recover(Action& a, Character& c, Activity act, double budget, double used);
    StepResult step_action_impl(Action& a, Character& c, double budget);
    StepResult step_board(Action& a, Character& c, double budget);
    StepResult step_animal(Action& a, Character& c, double budget);  // feed, milk, collect eggs, slaughter
    void update_animal(Object& a, double t0, double dt);
    StepResult step_order(Action& a, Character& c, double budget);
    void plan_visit(double arrival);  // the visit and its prices
    void merchant_arrives(double t, std::vector<std::string>& events);
    void merchant_trades(double t);   // buys what is for sale, fulfils the orders
    Object* town_hall();
    void record_stats(double t);
    Object* barn_with_place();
    void check_thresholds(Character& c);
    void die(Character& c, const std::string& cause);
    void winter_ended(double t);
    ToolItem* tool_in(Object& storage, int tool_id);
    bool wear_tool(Character& c, double seconds);
    void wear_clothes(Character& c, double seconds);  // returns true when the tool broke just now

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
    Merchant merchant_;
    std::mt19937_64 trade_rng_;  // prices; separate from the weather, so trade never changes the rain
    int decaying_bodies_ = 0;  // unburied bodies older than body_decay_hours; counted every tick
    std::mt19937_64 health_rng_;  // illness and injuries; separate, so they never change the rain or the prices
    std::vector<StatSample> stats_;
};

}  // namespace kindle
