#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace kindle {

namespace {

constexpr double kArriveDist = 0.01;  // meters
constexpr double kPi = 3.14159265358979323846;

std::string quote(const std::string& s) { return "«" + s + "»"; }

std::string fmt_num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

// Ukrainian names of items for messages: {genitive singular, nominative plural}.
struct ItemNames {
    const char* genitive;
    const char* plural;
};
ItemNames item_names(const std::string& item) {
    if (item == "axe") return {"сокири", "сокири"};
    if (item == "sickle") return {"серпа", "серпи"};
    if (item == "warm_clothes") return {"теплого одягу", "теплий одяг"};
    if (item == "clothes") return {"звичайного одягу", "звичайний одяг"};
    return {"предмета", "предмети"};
}

bool is_tool(const std::string& tool) { return tool == "axe" || tool == "sickle"; }

std::string resource_ua(const std::string& r) {
    if (r == "wood") return "дрова";
    if (r == "bread") return "хліб";
    if (r == "grain") return "зерно";
    if (r == "flour") return "борошно";
    return r;
}

// "дрова 12, ..." for messages about missing materials.
std::string describe(const std::map<std::string, int>& materials) {
    std::string out;
    for (const auto& [res, n] : materials) out += (out.empty() ? "" : ", ") + resource_ua(res) + " " + std::to_string(n);
    return out;
}

StepResult running(double used) {
    StepResult r;
    r.status = StepStatus::Running;
    r.used = used;
    return r;
}

StepResult done(double used) {
    StepResult r;
    r.status = StepStatus::Done;
    r.used = used;
    return r;
}

StepResult done_int(double used, long long v) {
    StepResult r = done(used);
    r.value.kind = ValueKind::Int;
    r.value.i = v;
    return r;
}

StepResult failed(double used, const char* type, std::string reason, double done_amount = 0) {
    StepResult r;
    r.status = StepStatus::Failed;
    r.used = used;
    r.error = ActionError{type, std::move(reason), done_amount};
    return r;
}

}  // namespace

const char* kind_name(ObjKind k) {
    switch (k) {
        case ObjKind::Storage: return "storage";
        case ObjKind::House: return "house";
        case ObjKind::Forest: return "forest";
        case ObjKind::Field: return "field";
        case ObjKind::Person: return "person";
    }
    return "?";
}

const char* season_name(Season s) { return s == Season::Winter ? "WINTER" : "WARM"; }

const char* field_state_name(FieldState s) {
    switch (s) {
        case FieldState::Empty: return "empty";
        case FieldState::Sown: return "sown";
        case FieldState::Ripe: return "ripe";
    }
    return "empty";
}

bool is_building(ObjKind k) { return k == ObjKind::Storage || k == ObjKind::House || k == ObjKind::Field; }

World::World(const Balance& balance, uint64_t seed) : balance_(balance), rng_(seed) {}

// ---------------------------------------------------------------- scene

int World::add_object(ObjKind kind, const std::string& name, Vec2 pos) {
    if (by_name_.count(name)) throw std::invalid_argument("duplicate object name: " + name);
    Object o;
    o.id = static_cast<int>(objects_.size());
    o.kind = kind;
    o.name = name;
    o.pos = pos;
    objects_.push_back(o);
    by_name_[name] = o.id;
    return o.id;
}

int World::add_storage(const std::string& name, Vec2 pos, const std::map<std::string, int>& tools,
                       const std::map<std::string, int>& resources) {
    int id = add_object(ObjKind::Storage, name, pos);
    objects_[id].tools = tools;
    objects_[id].resources = resources;
    return id;
}

int World::add_house(const std::string& name, Vec2 pos, const std::map<std::string, int>& chest) {
    int id = add_object(ObjKind::House, name, pos);
    objects_[id].chest = chest;
    objects_[id].capacity = static_cast<int>(std::lround(balance_.house_capacity));
    objects_[id].temperature = unheated_house_temperature(now_);
    return id;
}

int World::add_forest(const std::string& name, Vec2 pos, int trees) {
    int id = add_object(ObjKind::Forest, name, pos);
    objects_[id].trees_total = trees;
    return id;
}

int World::add_field(const std::string& name, Vec2 pos) { return add_object(ObjKind::Field, name, pos); }

int World::add_character(const std::string& name, int home, Vec2 pos, double hunger, double fatigue, double health) {
    int id = add_object(ObjKind::Person, name, pos);
    objects_[id].char_index = static_cast<int>(chars_.size());
    Character c;
    c.obj = id;
    c.name = name;
    c.home = home;
    c.hunger = hunger;
    c.fatigue = fatigue;
    c.health = health;
    c.clothes = "clothes";
    c.local_time = now_;
    c.last_action_time = now_;
    chars_.push_back(c);
    return id;
}

void World::set_start_time(double t) {
    now_ = t;
    for (auto& c : chars_) {
        c.local_time = t;
        c.last_action_time = t;
    }
    for (auto& o : objects_)
        if (o.kind == ObjKind::House) o.temperature = unheated_house_temperature(t);
}

void World::add_rain(double start, double end) { rain_.push_back({start, end}); }

int World::add_blueprint(ObjKind kind, const std::string& name, Vec2 pos, std::string& error) {
    const Balance& b = balance_;
    if (!is_building(kind)) {
        error = "За кресленням можна збудувати будинок, склад або поле";
        return -1;
    }
    if (name.empty() || name.size() > 60) {
        error = "Назва креслення не може бути порожньою або задовгою";
        return -1;
    }
    if (by_name_.count(name)) {
        error = "Назва " + quote(name) + " уже зайнята";
        return -1;
    }
    for (const Object& o : objects_) {
        if (o.kind == ObjKind::Person) continue;
        const double min_dist = b.blueprint_min_distance + (o.kind == ObjKind::Forest ? 60.0 : 0.0);
        if (std::hypot(o.pos.x - pos.x, o.pos.y - pos.y) < min_dist) {
            error = "Занадто близько до " + quote(o.name);
            return -1;
        }
    }
    const int id = add_object(kind, name, pos);
    Object& o = objects_[id];
    o.built = false;
    const char* what = "Поле";
    if (kind == ObjKind::House) {
        what = "Будинок";
        o.materials_needed["wood"] = static_cast<int>(std::lround(b.house_build_wood));
        o.build_total = b.house_build_hours * 3600.0;
        o.capacity = static_cast<int>(std::lround(b.house_capacity));
        o.chest = {{"warm_clothes", 0}, {"clothes", 0}};
    } else if (kind == ObjKind::Storage) {
        what = "Склад";
        o.materials_needed["wood"] = static_cast<int>(std::lround(b.storage_build_wood));
        o.build_total = b.storage_build_hours * 3600.0;
    } else {
        o.build_total = b.field_build_hours * 3600.0;
    }
    std::string needs = describe(o.materials_needed);
    log(-1, LogKind::Info,
        std::string("Нове креслення: ") + what + " " + quote(name) + (needs.empty() ? "" : " (потрібно: " + needs + ")"));
    return id;
}

// ---------------------------------------------------------------- queries

const Object* World::find(const std::string& name) const {
    auto it = by_name_.find(name);
    return it == by_name_.end() ? nullptr : &objects_[it->second];
}

Object* World::object(int id) {
    if (id < 0 || id >= static_cast<int>(objects_.size())) return nullptr;
    return &objects_[id];
}

Character& World::character(int obj_id) {
    const Object& o = objects_.at(obj_id);
    if (o.kind != ObjKind::Person) throw std::invalid_argument("not a character: " + o.name);
    return chars_.at(o.char_index);
}

bool World::is_resource(const std::string& resource) const {
    return resource == "wood" || resource == "bread" || resource == "grain" || resource == "flour";
}

double World::resource_kg(const std::string& resource) const {
    if (resource == "wood") return balance_.wood_kg;
    if (resource == "bread") return balance_.bread_kg;
    if (resource == "grain") return balance_.grain_kg;
    if (resource == "flour") return balance_.flour_kg;
    return 0;
}

double World::carry_weight(const Character& c) const {
    double w = 0;
    for (const auto& [res, n] : c.carrying) w += n * resource_kg(res);
    return w;
}

double World::walk_speed(const Character& c) const {
    bool loaded = carry_weight(c) > balance_.weight_limit_kg / 2 + kEps;
    double per_minute = loaded ? balance_.walk_loaded_meters_per_minute : balance_.walk_meters_per_minute;
    return per_minute / 60.0;
}

double World::travel_seconds(const Character& c, Vec2 to) const {
    const Vec2& p = objects_[c.obj].pos;
    double dist = std::hypot(to.x - p.x, to.y - p.y);
    return dist / walk_speed(c);
}

int World::forest_units_left(const Object& f) const {
    int total = static_cast<int>(std::floor(f.trees_total * balance_.wood_per_tree + kEps));
    return std::max(0, total - f.wood_taken);
}

std::map<std::string, int> World::materials_missing(const Object& o) const {
    std::map<std::string, int> missing;
    for (const auto& [res, need] : o.materials_needed) {
        auto it = o.materials_delivered.find(res);
        const int have = it == o.materials_delivered.end() ? 0 : it->second;
        if (have < need) missing[res] = need - have;
    }
    return missing;
}

int World::residents_count(int house_id) const {
    int n = 0;
    for (const Character& c : chars_) n += c.alive && c.home == house_id ? 1 : 0;
    return n;
}

int World::forest_trees_left(const Object& f) const {
    int used = static_cast<int>(std::floor(f.wood_taken / balance_.wood_per_tree + kEps));
    return std::max(0, f.trees_total - used);
}

bool World::is_raining_at(double t) const {
    for (const auto& r : rain_)
        if (t >= r.start && t < r.end) return true;
    return false;
}

bool World::is_night_at(double t) const {
    double h = std::fmod(t, kDay) / 3600.0;
    if (balance_.night_start_hour > balance_.night_end_hour)
        return h >= balance_.night_start_hour || h < balance_.night_end_hour;
    return h >= balance_.night_start_hour && h < balance_.night_end_hour;
}

Season World::season_at(double t) const {
    const int warm = std::max(1, static_cast<int>(std::lround(balance_.season_warm_days)));
    const int winter = std::max(0, static_cast<int>(std::lround(balance_.season_winter_days)));
    const int day = static_cast<int>(std::floor(t / kDay));
    return day % (warm + winter) < warm ? Season::Warm : Season::Winter;
}

int World::season_days_left(double t) const {
    const int warm = std::max(1, static_cast<int>(std::lround(balance_.season_warm_days)));
    const int winter = std::max(0, static_cast<int>(std::lround(balance_.season_winter_days)));
    const int pos = static_cast<int>(std::floor(t / kDay)) % (warm + winter);
    return pos < warm ? warm - pos : warm + winter - pos;
}

double World::outdoor_temperature(double t) const {
    const Balance& b = balance_;
    double base = season_at(t) == Season::Winter ? b.temp_winter_avg : b.temp_warm_avg;
    double hour = std::fmod(t, kDay) / 3600.0;
    double daily = -std::cos((hour - 2.0) / 24.0 * 2.0 * kPi) * b.temp_day_amplitude;  // min at 02:00
    return base + daily - (is_raining_at(t) ? b.temp_rain_drop : 0.0);
}

const Object* World::house_at(const Character& c) const {
    for (const Object& o : objects_)
        if (o.kind == ObjKind::House && o.built && at(c, o.pos)) return &o;
    return nullptr;
}

double World::unheated_house_temperature(double t) const {
    const double outside = outdoor_temperature(t);
    if (outside >= balance_.house_heated_temperature) return outside;
    return std::min(outside + balance_.house_shelter_bonus, balance_.house_heated_temperature);
}

double World::environment_temperature(const Character& c) const {
    const Object* house = house_at(c);
    return house ? house->temperature : outdoor_temperature(c.local_time);
}

// The nearest moment (>= now) when the time of day equals `tod`. A target that passed
// less than `time_of_day_grace_minutes` ago counts as reached, so a daily loop like
// `sleep(until=time(6, 0)); wait_until(time(6, 0))` does not skip a whole day.
double World::resolve_time_of_day(double tod, double now) const {
    double day_start = std::floor(now / kDay) * kDay;
    double candidate = day_start + tod;
    if (candidate <= now) {
        if (now - candidate < balance_.time_of_day_grace_minutes * 60.0) return now;
        candidate += kDay;
    }
    return candidate;
}

std::string World::format_time(double t) const {
    int day = static_cast<int>(std::floor(t / kDay)) + 1;
    int tod = static_cast<int>(std::fmod(t, kDay));
    char buf[48];
    std::snprintf(buf, sizeof buf, "день %d, %02d:%02d", day, tod / 3600, (tod % 3600) / 60);
    return buf;
}

// ---------------------------------------------------------------- time

double World::random01() { return static_cast<double>(rng_() >> 11) * (1.0 / 9007199254740992.0); }

void World::ensure_weather(double until) {
    int last_day = static_cast<int>(std::floor(until / kDay));
    while (weather_days_ <= last_day + 1) {
        int d = weather_days_++;
        if (!random_rain_) continue;
        const Balance& b = balance_;
        // Draw the numbers for every day, so a season length change doesn't shift the weather of other days.
        double roll = random01();
        double start_h = b.rain_earliest_start_hour + random01() * (b.rain_latest_start_hour - b.rain_earliest_start_hour);
        double len_h = b.rain_min_hours + random01() * (b.rain_max_hours - b.rain_min_hours);
        if (roll >= b.rain_chance_per_day || season_at(d * kDay) == Season::Winter) continue;  // no rain in winter
        double start = d * kDay + start_h * 3600.0;
        rain_.push_back({start, start + len_h * 3600.0});
    }
}

std::vector<std::string> World::begin_tick(double t0, double t1) {
    ensure_weather(t1);
    std::vector<std::string> events;
    for (const auto& r : rain_) {
        if (r.start >= t0 && r.start < t1) {
            events.push_back("RAIN");
            log(-1, LogKind::Info, "Почався дощ", {}, r.start);
        }
        if (r.end >= t0 && r.end < t1) {
            events.push_back("RAIN_STOPPED");
            log(-1, LogKind::Info, "Дощ закінчився", {}, r.end);
        }
    }
    update_environment(t0, t1);
    return events;
}

// Seasons, stoves and fields advance once per tick (characters see the state from the tick start).
void World::update_environment(double t0, double t1) {
    const Balance& b = balance_;
    const double dt = t1 - t0;
    if (dt <= 0) return;

    const double day_start = std::floor(t1 / kDay) * kDay;
    if (day_start > t0 && season_at(t0) != season_at(t1)) {
        bool winter = season_at(t1) == Season::Winter;
        log(-1, LogKind::Info, winter ? "Почалася зима" : "Почалася тепла пора", {}, day_start);
    }

    for (Object& o : objects_) {
        if (!o.built) continue;  // blueprints neither burn wood nor grow
        if (o.kind == ObjKind::House) {
            const bool burning = o.firewood > 0;
            if (burning) {
                o.firewood = std::max(0.0, o.firewood - dt / (b.firewood_burn_hours * 3600.0));
                if (o.firewood <= kEps) {
                    o.firewood = 0;
                    log(-1, LogKind::Info, "У будинку " + quote(o.name) + " згасла піч", {}, t1);
                }
            }
            double target = burning ? b.house_heated_temperature : unheated_house_temperature(t0);
            double tau = (target > o.temperature ? b.house_warmup_hours : b.house_cooldown_hours) * 3600.0;
            o.temperature += (target - o.temperature) * (1.0 - std::exp(-dt / std::max(1.0, tau)));
        } else if (o.kind == ObjKind::Field && o.field_state == FieldState::Sown && season_at(t0) == Season::Warm) {
            o.growth += dt / (std::max(0.01, b.field_grow_days) * kDay);
            if (o.growth >= 1.0) {
                o.growth = 1.0;
                o.field_state = FieldState::Ripe;
                o.grain_left = static_cast<int>(std::lround(b.field_yield_grain));
                log(-1, LogKind::Info, "Поле " + quote(o.name) + " дозріло", {}, t1);
            }
        }
    }
}

double World::fatigue_rate(Activity a) const {
    switch (a) {
        case Activity::Heavy: return balance_.fatigue_heavy_per_hour;
        case Activity::Light: return balance_.fatigue_light_per_hour;
        case Activity::Rest: return balance_.fatigue_rest_per_hour;
        case Activity::SleepHome: return balance_.fatigue_sleep_home_per_hour;
        case Activity::SleepGround: return balance_.fatigue_sleep_ground_per_hour;
        case Activity::Idle:
        case Activity::Wait: return 0;
    }
    return 0;
}

void World::pass_time(Character& c, double seconds, Activity a) {
    if (seconds <= 0 || !c.alive) return;
    const Balance& b = balance_;
    const double h = seconds / 3600.0;

    c.hunger = std::min(100.0, c.hunger + b.hunger_per_hour * h);
    double frate = fatigue_rate(a);
    if (a == Activity::Heavy && is_raining_at(c.local_time)) frate += b.rain_extra_fatigue_per_hour;
    c.fatigue = std::clamp(c.fatigue + frate * h, 0.0, 100.0);

    // Warmth: perceived temperature = surroundings + clothes (or the bed) + heat of hard work.
    double perceived = environment_temperature(c);
    if (a == Activity::SleepHome)
        perceived += b.bed_bonus;
    else if (c.clothes == "warm_clothes")
        perceived += b.warm_clothes_bonus;
    else if (c.clothes == "clothes")
        perceived += b.clothes_bonus;
    if (a == Activity::Heavy) perceived += b.work_heat_bonus;
    if (perceived >= b.comfort_temperature)
        c.warmth = std::min(100.0, c.warmth + b.warmth_recover_per_hour * h);
    else
        c.warmth = std::max(0.0, c.warmth - (b.comfort_temperature - perceived) * b.warmth_loss_per_degree_hour * h);

    double damage = 0;
    bool from_hunger = false, from_fatigue = false, from_cold = false;
    if (c.hunger >= 100.0 - kEps) {
        damage += b.hunger_full_damage_per_hour;
        from_hunger = true;
    } else if (c.hunger >= b.hunger_damage_threshold) {
        damage += b.hunger_damage_per_hour;
        from_hunger = true;
    }
    if (c.fatigue >= 100.0 - kEps) {
        damage += b.fatigue_full_damage_per_hour;
        from_fatigue = true;
    }
    if (c.warmth <= kEps) {
        damage += b.warmth_zero_damage_per_hour;
        from_cold = true;
    } else if (c.warmth < b.warmth_damage_threshold) {
        damage += b.warmth_damage_per_hour;
        from_cold = true;
    }
    double regen = (c.hunger < b.health_regen_max_hunger && c.fatigue < b.health_regen_max_fatigue &&
                    c.warmth >= b.health_regen_min_warmth)
                       ? b.health_regen_per_hour
                       : 0.0;
    c.health = std::min(100.0, c.health + (regen - damage) * h);
    c.local_time += seconds;

    if (a != Activity::Idle) {
        c.last_action_time = c.local_time;
        c.idle_warned = false;
    } else if (!c.idle_warned && c.local_time - c.last_action_time >= b.idle_log_hours * 3600.0 - kEps) {
        c.idle_warned = true;
        log(c.obj, LogKind::Warning, quote(c.name) + ": уже " + fmt_num(b.idle_log_hours) + " год без жодної дії");
    }

    check_thresholds(c);
    if (c.health <= 0) {
        c.health = 0;
        std::vector<std::string> causes;
        if (from_hunger) causes.push_back("голод");
        if (from_fatigue) causes.push_back("виснаження");
        if (from_cold) causes.push_back("холод");
        std::string cause;
        for (size_t i = 0; i < causes.size(); ++i) cause += (i ? " і " : "") + causes[i];
        die(c, cause.empty() ? "виснаження" : cause);
    }
}

void World::check_thresholds(Character& c) {
    const Balance& b = balance_;
    if (!c.hunger_warned && c.hunger >= b.hunger_damage_threshold) {
        c.hunger_warned = true;
        log(c.obj, LogKind::Warning, quote(c.name) + ": сильний голод (" + fmt_num(std::round(c.hunger)) + ")");
    } else if (c.hunger_warned && c.hunger < b.hunger_damage_threshold - 10) {
        c.hunger_warned = false;
    }
    if (!c.fatigue_warned && c.fatigue >= b.fatigue_slow_threshold) {
        c.fatigue_warned = true;
        log(c.obj, LogKind::Warning, quote(c.name) + ": сильна втома (" + fmt_num(std::round(c.fatigue)) + ")");
    } else if (c.fatigue_warned && c.fatigue < b.fatigue_slow_threshold - 10) {
        c.fatigue_warned = false;
    }
    if (!c.cold_warned && c.warmth < b.warmth_damage_threshold) {
        c.cold_warned = true;
        log(c.obj, LogKind::Warning, quote(c.name) + ": мерзне (тепло " + fmt_num(std::round(c.warmth)) + ")");
    } else if (c.cold_warned && c.warmth > b.health_regen_min_warmth) {
        c.cold_warned = false;
    }
    for (int level : {75, 50, 25}) {
        if (c.health < level && c.health_warn_level > level) {
            c.health_warn_level = level;
            log(c.obj, LogKind::Warning, quote(c.name) + ": здоров'я нижче " + std::to_string(level));
        }
    }
    if (c.health >= 90) c.health_warn_level = 100;
}

void World::die(Character& c, const std::string& cause) {
    c.alive = false;
    c.death_time = c.local_time;
    c.death_cause = cause;
    log(c.obj, LogKind::Death, "Смерть персонажа " + quote(c.name) + ": " + cause);
}

// ---------------------------------------------------------------- actions

bool World::at(const Character& c, Vec2 p) const {
    const Vec2& q = objects_[c.obj].pos;
    return std::hypot(p.x - q.x, p.y - q.y) < kArriveDist;
}

// Walks toward `dest` for at most `max_t` seconds. Returns the time used.
double World::walk(Character& c, Vec2 dest, double max_t) {
    Vec2& p = objects_[c.obj].pos;
    double dx = dest.x - p.x, dy = dest.y - p.y;
    double dist = std::hypot(dx, dy);
    if (dist < kArriveDist) {
        p = dest;
        return 0;
    }
    if (max_t <= 0) return 0;
    double need = dist / walk_speed(c);
    double t = std::min(need, max_t);
    if (t >= need) {
        p = dest;
    } else {
        double k = t / need;
        p.x += dx * k;
        p.y += dy * k;
    }
    pass_time(c, t, Activity::Light);
    return t;
}

// Spends up to `max_t` on a fixed-length phase (`total` seconds); progress survives interruptions.
double World::handle(Character& c, Action& a, double total, double max_t, Activity act, bool& finished) {
    double t = std::max(0.0, std::min(total - a.progress, max_t));
    if (t > 0) pass_time(c, t, act);
    a.progress += t;
    finished = a.progress >= total - kEps;
    return t;
}

BeginResult World::begin_action(int char_obj, const std::string& kind, int target, const ActionParams& p) {
    BeginResult res;
    Character& c = character(char_obj);
    const Balance& b = balance_;
    auto fail = [&](const char* type, const std::string& reason) {
        res.error = ActionError{type, reason, 0};
        return res;
    };
    auto carried = [&](const char* resource) {
        auto it = c.carrying.find(resource);
        return it == c.carrying.end() ? 0 : it->second;
    };

    Action a;
    a.cid = char_obj;
    a.name = kind;
    a.target = target;
    a.params = p;
    Object* t = object(target);
    auto require = [&](ObjKind k) { return t != nullptr && t->kind == k; };
    auto unbuilt = [&]() { return t != nullptr && !t->built; };
    auto fail_unbuilt = [&]() {
        return fail("ActionError", quote(t->name) + " ще не збудовано — це креслення (me.deliver, me.build)");
    };
    const double now = c.local_time;

    if (kind == "go_to") {
        if (!t) return fail("NotFoundError", "Такого об'єкта не існує");
        a.kind = ActionKind::GoTo;
    } else if (kind == "go_home") {
        if (c.home < 0) return fail("NoHomeError", quote(c.name) + " не має дому");
        a.kind = ActionKind::GoTo;
        a.target = c.home;
    } else if (kind == "take") {
        if (!require(ObjKind::Storage)) return fail("ActionError", "Інструмент можна взяти лише зі складу");
        if (unbuilt()) return fail_unbuilt();
        if (!is_tool(p.tool)) return fail("ActionError", "Невідомий інструмент");
        a.kind = ActionKind::Take;
        const ItemNames names = item_names(p.tool);
        if (c.tool == p.tool) {
            a.noop = true;
        } else if (!c.tool.empty()) {
            return fail("ActionError", "Слот інструмента зайнятий: спершу поверніть інструмент (put_back)");
        } else if (t->tools[p.tool] <= 0) {
            return fail("NoItemError", "На складі " + quote(t->name) + " немає " + names.genitive);
        } else if (t->tools[p.tool] - t->tools_reserved[p.tool] <= 0) {
            return fail("ReservedError",
                        std::string("Усі ") + names.plural + " на складі " + quote(t->name) + " зарезервували інші персонажі");
        } else {
            t->tools_reserved[p.tool]++;
            a.reserved = true;
        }
    } else if (kind == "put_back") {
        if (!require(ObjKind::Storage)) return fail("ActionError", "Інструмент можна повернути лише на склад");
        if (unbuilt()) return fail_unbuilt();
        if (c.tool.empty()) return fail("NoItemError", quote(c.name) + " не тримає інструмента");
        a.kind = ActionKind::PutBack;
    } else if (kind == "chop_wood") {
        if (!require(ObjKind::Forest)) return fail("ActionError", "Рубати дрова можна лише в лісі");
        if (c.tool != "axe")
            return fail("NoItemError", "Щоб рубати дрова, потрібна сокира в руках (me.take(Axe, склад))");
        if (carry_weight(c) + b.wood_kg > b.weight_limit_kg + kEps)
            return fail("TooHeavyError", "Руки повні: більше дров не піднести");
        if (forest_units_left(*t) <= 0) return fail("NoItemError", "У лісі " + quote(t->name) + " не лишилося дерев");
        a.kind = ActionKind::ChopWood;
    } else if (kind == "drop") {
        if (!require(ObjKind::Storage)) return fail("ActionError", "Покласти речі можна лише на склад");
        if (unbuilt()) return fail_unbuilt();
        a.kind = ActionKind::Drop;
    } else if (kind == "pick_up") {
        if (!require(ObjKind::Storage)) return fail("ActionError", "Брати ресурси можна лише зі складу");
        if (unbuilt()) return fail_unbuilt();
        if (!is_resource(p.resource)) return fail("ActionError", "Невідомий ресурс " + quote(p.resource));
        if (p.count <= 0) return fail("ActionError", "Кількість має бути більшою за нуль");
        a.kind = ActionKind::PickUp;
    } else if (kind == "eat") {
        if (!is_resource(p.resource)) return fail("ActionError", "Невідомий ресурс " + quote(p.resource));
        if (!is_edible(p.resource)) return fail("ActionError", quote(p.resource) + " не можна з'їсти");
        if (carried(p.resource.c_str()) < 1)
            return fail("NoItemError", quote(c.name) + " не несе " + quote(p.resource) + " — спершу візьміть зі складу (pick_up)");
        a.kind = ActionKind::Eat;
    } else if (kind == "sleep") {
        a.kind = ActionKind::Sleep;
    } else if (kind == "rest") {
        a.kind = ActionKind::Rest;
    } else if (kind == "wait") {
        a.kind = ActionKind::Wait;
        if (p.until) {
            a.wait_target = resolve_time_of_day(*p.until, now);
            a.wait_returns_bool = true;
        } else if (p.seconds) {
            a.wait_target = now + std::max(0.0, *p.seconds);
        } else {
            return fail("ActionError", "Не вказано, чого чекати");
        }
        if (p.timeout) a.wait_timeout = now + std::max(0.0, *p.timeout);
    } else if (kind == "heat") {
        if (!require(ObjKind::House)) return fail("ActionError", "Опалювати можна лише будинок");
        if (unbuilt()) return fail_unbuilt();
        if (carried("wood") < 1)
            return fail("NoItemError", "Щоб опалити будинок, принесіть дрова (me.pick_up(склад, \"wood\", N))");
        if (p.amount && *p.amount <= 0) return fail("ActionError", "amount= має бути більшим за нуль");
        a.kind = ActionKind::Heat;
    } else if (kind == "wear") {
        if (p.item != "clothes" && p.item != "warm_clothes") return fail("ActionError", "Невідомий одяг");
        if (c.home < 0) return fail("NoHomeError", "Переодягтися можна лише вдома, а " + quote(c.name) + " не має дому");
        a.kind = ActionKind::Wear;
        a.target = c.home;
        a.noop = c.clothes == p.item;
    } else if (kind == "sow") {
        if (!require(ObjKind::Field)) return fail("ActionError", "Сіяти можна лише на полі");
        if (unbuilt()) return fail_unbuilt();
        if (season_at(now) == Season::Winter) return fail("ActionError", "Узимку земля замерзла — сіяти не можна");
        if (t->field_state == FieldState::Sown) return fail("ActionError", "Поле " + quote(t->name) + " уже засіяне");
        if (t->field_state == FieldState::Ripe)
            return fail("ActionError", "Поле " + quote(t->name) + " дозріло — спершу зберіть урожай (harvest)");
        if (!t->seeded && carried("grain") < static_cast<int>(std::lround(b.field_seed_grain)))
            return fail("NoItemError", "Для посіву потрібно " + fmt_num(b.field_seed_grain) + " зерна в руках");
        a.kind = ActionKind::Sow;
    } else if (kind == "harvest") {
        if (!require(ObjKind::Field)) return fail("ActionError", "Жати можна лише на полі");
        if (unbuilt()) return fail_unbuilt();
        if (c.tool != "sickle")
            return fail("NoItemError", "Для жнив потрібен серп у руках (me.take(Sickle, склад))");
        if (t->field_state != FieldState::Ripe || t->grain_left <= 0)
            return fail("NoItemError", "На полі " + quote(t->name) + " немає стиглого врожаю");
        if (carry_weight(c) + b.grain_kg > b.weight_limit_kg + kEps)
            return fail("TooHeavyError", "Руки повні: більше зерна не піднести");
        a.kind = ActionKind::Harvest;
    } else if (kind == "grind") {
        if (!require(ObjKind::House)) return fail("ActionError", "Молоти зерно можна лише в будинку (там жорна)");
        if (unbuilt()) return fail_unbuilt();
        if (carried("grain") < 1) return fail("NoItemError", "Немає зерна в руках — візьміть його зі складу");
        if (t->mill_user >= 0 && t->mill_user != char_obj)
            return fail("WorkplaceBusyError", "Жорна в будинку " + quote(t->name) + " зайняті: там меле " +
                                                  quote(objects_[t->mill_user].name));
        t->mill_user = char_obj;
        a.kind = ActionKind::Grind;
    } else if (kind == "bake") {
        if (!require(ObjKind::House)) return fail("ActionError", "Пекти хліб можна лише в печі будинку");
        if (unbuilt()) return fail_unbuilt();
        if (carried("flour") < 1) return fail("NoItemError", "Немає борошна в руках");
        if (t->firewood <= 0)
            return fail("NoItemError", "Піч у будинку " + quote(t->name) + " холодна — спершу опаліть його (me.heat)");
        if (t->oven_user >= 0 && t->oven_user != char_obj)
            return fail("WorkplaceBusyError", "Піч у будинку " + quote(t->name) + " зайнята: там пече " +
                                                  quote(objects_[t->oven_user].name));
        t->oven_user = char_obj;
        a.kind = ActionKind::Bake;
    } else if (kind == "deliver") {
        if (!t || !is_building(t->kind)) return fail("ActionError", "Матеріали можна нести лише на будову (креслення)");
        if (t->built) return fail("ActionError", quote(t->name) + " уже збудовано");
        const auto missing = materials_missing(*t);
        if (missing.empty()) return fail("ActionError", "На будову " + quote(t->name) + " уже принесли всі матеріали");
        bool has = false;
        for (const auto& [res, n] : missing) has = has || carried(res.c_str()) > 0;
        if (!has) return fail("NoItemError", "У руках немає матеріалів для будови: потрібно " + describe(missing));
        a.kind = ActionKind::Deliver;
    } else if (kind == "build") {
        if (!t || !is_building(t->kind)) return fail("ActionError", "Будувати можна лише за кресленням");
        if (t->built) return fail("ActionError", quote(t->name) + " уже збудовано");
        const auto missing = materials_missing(*t);
        if (!missing.empty())
            return fail("NoItemError", "На будові " + quote(t->name) + " бракує матеріалів: " + describe(missing));
        a.kind = ActionKind::Build;
    } else if (kind == "move_into") {
        if (!require(ObjKind::House)) return fail("ActionError", "Заселитися можна лише в будинок");
        if (unbuilt()) return fail_unbuilt();
        a.kind = ActionKind::MoveInto;
        if (c.home == target)
            a.noop = true;
        else if (residents_count(target) >= t->capacity)
            return fail("ActionError", "У будинку " + quote(t->name) + " немає вільних місць");
    } else {
        return fail("ActionError", "Невідома дія " + quote(kind));
    }

    switch (a.kind) {
        case ActionKind::ChopWood:
        case ActionKind::Harvest:
        case ActionKind::Grind:
        case ActionKind::Bake:
        case ActionKind::Sow:
        case ActionKind::Build:
        case ActionKind::Sleep:
        case ActionKind::Rest:
            if (p.until) a.deadline = std::min(a.deadline, resolve_time_of_day(*p.until, now));
            if (p.duration) a.deadline = std::min(a.deadline, now + *p.duration);
            a.has_limits = p.amount.has_value() || p.until.has_value() || p.duration.has_value();
            break;
        default:
            break;
    }

    a.id = next_action_id_++;
    actions_[a.id] = a;
    c.action_stack.push_back(a.id);
    res.action_id = a.id;
    return res;
}

StepResult World::step_action(int action_id, double budget) {
    Action& a = actions_.at(action_id);
    Character& c = character(a.cid);
    const Balance& b = balance_;

    switch (a.kind) {
        case ActionKind::GoTo: {
            Vec2 dest = objects_[a.target].pos;
            double used = walk(c, dest, budget);
            return at(c, dest) ? done(used) : running(used);
        }
        case ActionKind::Take: {
            if (a.noop) return done(0);
            Object& s = objects_[a.target];
            double used = walk(c, s.pos, budget);
            if (!at(c, s.pos)) return running(used);
            bool fin = false;
            used += handle(c, a, b.tool_take_minutes * 60.0, budget - used, Activity::Light, fin);
            if (!fin) return running(used);
            s.tools[a.params.tool]--;
            s.tools_reserved[a.params.tool]--;
            a.reserved = false;
            c.tool = a.params.tool;
            return done(used);
        }
        case ActionKind::PutBack: {
            Object& s = objects_[a.target];
            double used = walk(c, s.pos, budget);
            if (!at(c, s.pos)) return running(used);
            bool fin = false;
            used += handle(c, a, b.tool_take_minutes * 60.0, budget - used, Activity::Light, fin);
            if (!fin) return running(used);
            if (c.tool.empty()) return failed(used, "NoItemError", quote(c.name) + " не тримає інструмента");
            s.tools[c.tool]++;
            c.tool.clear();
            return done(used);
        }
        case ActionKind::ChopWood:
        case ActionKind::Harvest:
            return step_gather(a, c, budget);
        case ActionKind::Grind:
        case ActionKind::Bake:
            return step_convert(a, c, budget);
        case ActionKind::Sow:
            return step_sow(a, c, budget);
        case ActionKind::Build:
            return step_build(a, c, budget);
        case ActionKind::Deliver: {
            Object& site = objects_[a.target];
            double used = walk(c, site.pos, budget);
            if (!at(c, site.pos)) return running(used);
            if (!a.transferred) {
                if (site.built) return failed(used, "ActionError", quote(site.name) + " уже збудовано");
                int moved = 0;
                for (const auto& [res, n] : materials_missing(site)) {
                    auto it = c.carrying.find(res);
                    if (it == c.carrying.end()) continue;
                    const int k = std::min(n, it->second);
                    it->second -= k;
                    if (it->second == 0) c.carrying.erase(it);
                    site.materials_delivered[res] += k;
                    moved += k;
                }
                if (moved == 0) return failed(used, "NoItemError", "У руках немає потрібних для будови матеріалів");
                a.done = moved;
                a.transferred = true;
                a.handle_total = (b.transfer_base_minutes + b.transfer_per_unit_minutes * moved) * 60.0;
            }
            bool fin = false;
            used += handle(c, a, a.handle_total, budget - used, Activity::Light, fin);
            return fin ? done_int(used, a.done) : running(used);
        }
        case ActionKind::MoveInto: {
            if (a.noop) return done(0);
            Object& house = objects_[a.target];
            double used = walk(c, house.pos, budget);
            if (!at(c, house.pos)) return running(used);
            if (!a.transferred) {
                if (residents_count(a.target) >= house.capacity)
                    return failed(used, "ActionError", "У будинку " + quote(house.name) + " немає вільних місць");
                a.transferred = true;
            }
            bool fin = false;
            used += handle(c, a, b.move_in_minutes * 60.0, budget - used, Activity::Light, fin);
            if (!fin) return running(used);
            // Personal belongings move too: the spare set of clothes from the old chest.
            if (c.home >= 0 && c.home != a.target) {
                Object& from = objects_[c.home];
                const char* spare = c.clothes == "warm_clothes" ? "clothes" : "warm_clothes";
                if (from.chest[spare] > 0) {
                    from.chest[spare]--;
                    house.chest[spare]++;
                }
            }
            c.home = a.target;
            log(c.obj, LogKind::Info, quote(c.name) + " тепер живе в будинку " + quote(house.name));
            return done(used);
        }
        case ActionKind::Drop: {
            Object& s = objects_[a.target];
            double used = walk(c, s.pos, budget);
            if (!at(c, s.pos)) return running(used);
            if (!a.transferred) {
                int units = 0;
                for (const auto& [res, n] : c.carrying) {
                    s.resources[res] += n;
                    units += n;
                }
                c.carrying.clear();
                a.transferred = true;
                a.handle_total = (b.transfer_base_minutes + b.transfer_per_unit_minutes * units) * 60.0;
            }
            bool fin = false;
            used += handle(c, a, a.handle_total, budget - used, Activity::Light, fin);
            return fin ? done(used) : running(used);
        }
        case ActionKind::PickUp: {
            Object& s = objects_[a.target];
            double used = walk(c, s.pos, budget);
            if (!at(c, s.pos)) return running(used);
            if (!a.transferred) {
                const std::string& res = a.params.resource;
                const int count = a.params.count;
                int have = s.resources[res];
                if (have < count)
                    return failed(used, "NoItemError",
                                  "На складі " + quote(s.name) + " лише " + std::to_string(have) + " од. " + quote(res) +
                                      ", а потрібно " + std::to_string(count));
                if (carry_weight(c) + count * resource_kg(res) > b.weight_limit_kg + kEps)
                    return failed(used, "TooHeavyError",
                                  "Заважко: з " + std::to_string(count) + " од. " + quote(res) + " вантаж перевищить " +
                                      fmt_num(b.weight_limit_kg) + " кг");
                s.resources[res] -= count;
                c.carrying[res] += count;
                a.transferred = true;
                a.handle_total = (b.transfer_base_minutes + b.transfer_per_unit_minutes * count) * 60.0;
            }
            bool fin = false;
            used += handle(c, a, a.handle_total, budget - used, Activity::Light, fin);
            return fin ? done(used) : running(used);
        }
        case ActionKind::Heat: {
            Object& house = objects_[a.target];
            double used = walk(c, house.pos, budget);
            if (!at(c, house.pos)) return running(used);
            if (!a.transferred) {
                int have = c.carrying["wood"];
                int n = a.params.amount ? std::min(*a.params.amount, have) : have;
                if (n <= 0) return failed(used, "NoItemError", quote(c.name) + " уже не несе дров");
                c.carrying["wood"] -= n;
                if (c.carrying["wood"] == 0) c.carrying.erase("wood");
                if (house.firewood <= 0) log(c.obj, LogKind::Info, quote(c.name) + " розпалює піч у будинку " + quote(house.name));
                house.firewood += n;
                a.done = n;
                a.transferred = true;
                a.handle_total = (b.transfer_base_minutes + b.transfer_per_unit_minutes * n) * 60.0;
            }
            bool fin = false;
            used += handle(c, a, a.handle_total, budget - used, Activity::Light, fin);
            return fin ? done_int(used, a.done) : running(used);
        }
        case ActionKind::Wear: {
            if (a.noop) return done(0);
            Object& house = objects_[a.target];
            double used = walk(c, house.pos, budget);
            if (!at(c, house.pos)) return running(used);
            if (!a.transferred) {
                const std::string& item = a.params.item;
                if (house.chest[item] < 1)
                    return failed(used, "NoItemError",
                                  "У скрині будинку " + quote(house.name) + " немає " + item_names(item).genitive);
                house.chest[item]--;
                if (!c.clothes.empty()) house.chest[c.clothes]++;
                c.clothes = item;
                a.transferred = true;
            }
            bool fin = false;
            used += handle(c, a, b.wear_minutes * 60.0, budget - used, Activity::Light, fin);
            return fin ? done(used) : running(used);
        }
        case ActionKind::Eat: {
            bool fin = false;
            double used = handle(c, a, b.eat_minutes * 60.0, budget, Activity::Wait, fin);
            if (!fin) return running(used);
            auto it = c.carrying.find(a.params.resource);
            if (it == c.carrying.end() || it->second < 1)
                return failed(used, "NoItemError", quote(c.name) + " уже не несе " + quote(a.params.resource));
            if (--it->second == 0) c.carrying.erase(it);
            c.hunger = std::max(0.0, c.hunger - b.bread_hunger_relief);
            return done(used);
        }
        case ActionKind::Sleep: {
            double used = 0;
            if (c.home >= 0) {
                Vec2 home = objects_[c.home].pos;
                if (!at(c, home)) {
                    used = walk(c, home, std::min(budget, a.deadline - c.local_time));
                    if (!at(c, home)) return c.local_time >= a.deadline - kEps ? done(used) : running(used);
                }
            }
            bool at_home = c.home >= 0 && at(c, objects_[c.home].pos);
            return step_recover(a, c, at_home ? Activity::SleepHome : Activity::SleepGround, budget, used);
        }
        case ActionKind::Rest:
            return step_recover(a, c, Activity::Rest, budget, 0);
        case ActionKind::Wait: {
            double end = std::min(a.wait_target, a.wait_timeout);
            double t = std::max(0.0, std::min(budget, end - c.local_time));
            if (t > 0) pass_time(c, t, Activity::Wait);
            if (c.local_time >= end - kEps) {
                StepResult r = done(t);
                if (a.wait_returns_bool) {
                    r.value.kind = ValueKind::Bool;
                    r.value.b = a.wait_target <= a.wait_timeout;
                }
                return r;
            }
            return running(t);
        }
    }
    return failed(0, "ActionError", "Невідома дія");
}

// Chopping wood in a forest or harvesting a ripe field: units go into the hands one by one.
StepResult World::step_gather(Action& a, Character& c, double budget) {
    const Balance& b = balance_;
    const bool chop = a.kind == ActionKind::ChopWood;
    Object& src = objects_[a.target];
    const char* resource = chop ? "wood" : "grain";
    const double unit = chop ? b.chop_minutes_per_unit * 60.0
                             : b.field_harvest_hours * 3600.0 / std::max(1.0, b.field_yield_grain);
    double used = 0;
    for (;;) {
        if (!c.alive) return running(budget);
        if (a.params.amount && a.done >= *a.params.amount) return done_int(used, a.done);
        if (c.local_time >= a.deadline - kEps) return done_int(used, a.done);
        double left = budget - used;
        if (left <= kEps) return running(used);
        if (!at(c, src.pos)) {
            used += walk(c, src.pos, std::min(left, a.deadline - c.local_time));
            continue;
        }
        const int units_left = chop ? forest_units_left(src) : (src.field_state == FieldState::Ripe ? src.grain_left : 0);
        if (units_left <= 0) {
            if (chop) return failed(used, "NoItemError", "У лісі " + quote(src.name) + " не лишилося дерев", a.done);
            // The field is harvested completely (by this character or by others): a natural end.
            if (a.done > 0) return done_int(used, a.done);
            return failed(used, "NoItemError", "На полі " + quote(src.name) + " немає стиглого врожаю", 0);
        }
        if (carry_weight(c) + resource_kg(resource) > b.weight_limit_kg + kEps) {
            if (!a.has_limits && a.done > 0) return done_int(used, a.done);
            return failed(used, "TooHeavyError",
                          std::string("Руки повні: зібрано ") + std::to_string(a.done) + ", більше не піднести", a.done);
        }

        double factor = 1.0;
        if (is_raining_at(c.local_time)) factor *= b.rain_work_factor;
        if (c.fatigue >= b.fatigue_slow_threshold) factor *= b.fatigue_slow_factor;
        double& partial = c.work_partial[src.id];
        double need = (1.0 - partial) * unit / factor;
        double t = std::min({left, a.deadline - c.local_time, need});
        pass_time(c, t, Activity::Heavy);
        used += t;
        if (t >= need - kEps) {
            partial = 0;
            c.carrying[resource] += 1;
            a.done += 1;
            if (chop) {
                src.wood_taken += 1;
            } else if (--src.grain_left <= 0) {
                src.grain_left = 0;
                src.field_state = FieldState::Empty;
                src.growth = 0;
                log(c.obj, LogKind::Info, "Урожай з поля " + quote(src.name) + " зібрано");
            }
        } else {
            partial = std::min(1.0, partial + t * factor / unit);
        }
    }
}

// Grinding grain into flour (hand mill) or baking flour into bread (stove) inside a house.
StepResult World::step_convert(Action& a, Character& c, double budget) {
    const Balance& b = balance_;
    const bool grind = a.kind == ActionKind::Grind;
    Object& house = objects_[a.target];
    const char* input = grind ? "grain" : "flour";
    const char* output = grind ? "flour" : "bread";
    const double unit = (grind ? b.grind_minutes_per_unit : b.bake_minutes_per_unit) * 60.0;
    double used = walk(c, house.pos, std::min(budget, a.deadline - c.local_time));
    if (!at(c, house.pos)) return c.local_time >= a.deadline - kEps ? done_int(used, a.done) : running(used);
    for (;;) {
        if (!c.alive) return running(budget);
        if (a.params.amount && a.done >= *a.params.amount) return done_int(used, a.done);
        if (c.local_time >= a.deadline - kEps) return done_int(used, a.done);
        double left = budget - used;
        if (left <= kEps) return running(used);
        auto it = c.carrying.find(input);
        if (it == c.carrying.end() || it->second < 1) {
            if (a.done > 0) return done_int(used, a.done);
            return failed(used, "NoItemError", grind ? "Немає зерна в руках" : "Немає борошна в руках", 0);
        }
        if (!grind && house.firewood <= 0)
            return failed(used, "NoItemError", "Піч у будинку " + quote(house.name) + " згасла", a.done);
        double t = std::min({left, a.deadline - c.local_time, unit - a.progress});
        pass_time(c, t, grind ? Activity::Heavy : Activity::Light);
        used += t;
        a.progress += t;
        if (a.progress >= unit - kEps) {
            a.progress = 0;
            if (--it->second == 0) c.carrying.erase(it);
            c.carrying[output] += 1;
            a.done += 1;
        }
    }
}

// Sowing: the seed grain goes into the ground first, then hours of work (shared by everyone sowing).
StepResult World::step_sow(Action& a, Character& c, double budget) {
    const Balance& b = balance_;
    Object& field = objects_[a.target];
    const double total = b.field_sow_hours * 3600.0;
    double used = 0;
    for (;;) {
        if (!c.alive) return running(budget);
        if (c.local_time >= a.deadline - kEps) return done(used);
        double left = budget - used;
        if (left <= kEps) return running(used);
        if (!at(c, field.pos)) {
            used += walk(c, field.pos, std::min(left, a.deadline - c.local_time));
            continue;
        }
        if (field.field_state != FieldState::Empty) return done(used);  // finished by someone else
        if (!field.seeded) {
            const int seed = static_cast<int>(std::lround(b.field_seed_grain));
            auto it = c.carrying.find("grain");
            if (it == c.carrying.end() || it->second < seed)
                return failed(used, "NoItemError", "Для посіву потрібно " + std::to_string(seed) + " зерна в руках");
            it->second -= seed;
            if (it->second == 0) c.carrying.erase(it);
            field.seeded = true;
        }
        double factor = 1.0;
        if (is_raining_at(c.local_time)) factor *= b.rain_work_factor;
        if (c.fatigue >= b.fatigue_slow_threshold) factor *= b.fatigue_slow_factor;
        double t = std::min({left, a.deadline - c.local_time, (total - field.sow_done) / factor});
        pass_time(c, t, Activity::Heavy);
        used += t;
        field.sow_done += t * factor;
        if (field.sow_done >= total - kEps) {
            field.field_state = FieldState::Sown;
            field.sow_done = 0;
            field.seeded = false;
            field.growth = 0;
            log(c.obj, LogKind::Info, "Поле " + quote(field.name) + " засіяно");
            return done(used);
        }
    }
}

// Construction work on a blueprint (all materials delivered); progress is shared by all builders.
StepResult World::step_build(Action& a, Character& c, double budget) {
    const Balance& b = balance_;
    Object& site = objects_[a.target];
    double used = 0;
    for (;;) {
        if (!c.alive) return running(budget);
        if (site.built) return done(used);  // finished (possibly by others)
        if (c.local_time >= a.deadline - kEps) return done(used);
        double left = budget - used;
        if (left <= kEps) return running(used);
        if (!at(c, site.pos)) {
            used += walk(c, site.pos, std::min(left, a.deadline - c.local_time));
            continue;
        }
        double factor = 1.0;
        if (is_raining_at(c.local_time)) factor *= b.rain_work_factor;
        if (c.fatigue >= b.fatigue_slow_threshold) factor *= b.fatigue_slow_factor;
        double t = std::min({left, a.deadline - c.local_time, (site.build_total - site.build_done) / factor});
        pass_time(c, t, Activity::Heavy);
        used += t;
        site.build_done += t * factor;
        if (site.build_done >= site.build_total - kEps) {
            complete_building(site, c.obj);
            return done(used);
        }
    }
}

void World::complete_building(Object& o, int builder) {
    o.built = true;
    o.build_done = o.build_total;
    if (o.kind == ObjKind::House) o.temperature = unheated_house_temperature(now_);
    if (o.kind == ObjKind::Field) o.field_state = FieldState::Empty;
    log(builder, LogKind::Info, quote(o.name) + " збудовано");
}

// Sleep / rest: until the deadline, or (without limits) until fatigue reaches 0.
StepResult World::step_recover(Action& a, Character& c, Activity act, double budget, double used) {
    const double rate = fatigue_rate(act);
    for (;;) {
        if (!c.alive) return running(budget);
        if (c.local_time >= a.deadline - kEps) return done(used);
        if (!a.has_limits && (c.fatigue <= kEps || rate >= 0)) return done(used);
        double left = budget - used;
        if (left <= kEps) return running(used);
        double t = std::min(left, a.deadline - c.local_time);
        if (!a.has_limits) t = std::min(t, c.fatigue / -rate * 3600.0);
        pass_time(c, t, act);
        used += t;
    }
}

const Action* World::current_action(int char_obj) {
    const auto& stack = character(char_obj).action_stack;
    if (stack.empty()) return nullptr;
    auto it = actions_.find(stack.back());
    return it == actions_.end() ? nullptr : &it->second;
}

void World::end_action(int action_id) {
    auto it = actions_.find(action_id);
    if (it == actions_.end()) return;
    Action& a = it->second;
    if (a.reserved) {
        if (Object* s = object(a.target)) s->tools_reserved[a.params.tool]--;
        a.reserved = false;
    }
    if (a.kind == ActionKind::Grind || a.kind == ActionKind::Bake) {
        if (Object* h = object(a.target)) {
            int& user = a.kind == ActionKind::Grind ? h->mill_user : h->oven_user;
            if (user == a.cid) user = -1;  // the workplace is free again
        }
    }
    auto& stack = character(a.cid).action_stack;
    stack.erase(std::remove(stack.begin(), stack.end(), action_id), stack.end());
    actions_.erase(it);
}

void World::cancel_actions(int char_obj) {
    std::vector<int> stack = character(char_obj).action_stack;
    for (int id : stack) end_action(id);
}

// ---------------------------------------------------------------- log

void World::log(int obj, LogKind kind, const std::string& text, const std::string& detail, double time) {
    LogEntry e;
    e.object_id = obj;
    e.kind = kind;
    e.text = text;
    e.detail = detail;
    if (time >= 0)
        e.time = time;
    else if (obj >= 0 && objects_[obj].kind == ObjKind::Person)
        e.time = character(obj).local_time;
    else
        e.time = now_;
    log_.push_back(e);
    if (on_log) on_log(log_.back());
}

}  // namespace kindle
