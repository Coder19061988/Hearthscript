#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace kindle {

namespace {

constexpr double kArriveDist = 0.01;  // meters

std::string quote(const std::string& s) { return "«" + s + "»"; }

std::string fmt_num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
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
        case ObjKind::Person: return "person";
    }
    return "?";
}

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

int World::add_storage(const std::string& name, Vec2 pos, int axes, const std::map<std::string, int>& resources) {
    int id = add_object(ObjKind::Storage, name, pos);
    objects_[id].axes = axes;
    objects_[id].resources = resources;
    return id;
}

int World::add_house(const std::string& name, Vec2 pos) { return add_object(ObjKind::House, name, pos); }

int World::add_forest(const std::string& name, Vec2 pos, int trees) {
    int id = add_object(ObjKind::Forest, name, pos);
    objects_[id].trees_total = trees;
    return id;
}

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
}

void World::add_rain(double start, double end) { rain_.push_back({start, end}); }

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

double World::resource_kg(const std::string& resource) const {
    if (resource == "wood") return balance_.wood_kg;
    if (resource == "bread") return balance_.bread_kg;
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
        if (random01() >= b.rain_chance_per_day) continue;
        double start_h = b.rain_earliest_start_hour + random01() * (b.rain_latest_start_hour - b.rain_earliest_start_hour);
        double len_h = b.rain_min_hours + random01() * (b.rain_max_hours - b.rain_min_hours);
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
    return events;
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

    double damage = 0;
    bool from_hunger = false, from_fatigue = false;
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
    double regen = (c.hunger < b.health_regen_max_hunger && c.fatigue < b.health_regen_max_fatigue)
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
        const char* cause = from_hunger && from_fatigue ? "голод і виснаження"
                            : from_hunger               ? "голод"
                                                        : "виснаження";
        die(c, cause);
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
    auto fail = [&](const char* type, const std::string& reason) {
        res.error = ActionError{type, reason, 0};
        return res;
    };

    Action a;
    a.cid = char_obj;
    a.target = target;
    a.params = p;
    Object* t = object(target);
    auto require = [&](ObjKind k) { return t != nullptr && t->kind == k; };

    if (kind == "go_to") {
        if (!t) return fail("NotFoundError", "Такого об'єкта не існує");
        a.kind = ActionKind::GoTo;
    } else if (kind == "go_home") {
        if (c.home < 0) return fail("NoHomeError", quote(c.name) + " не має дому");
        a.kind = ActionKind::GoTo;
        a.target = c.home;
    } else if (kind == "take") {
        if (!require(ObjKind::Storage)) return fail("ActionError", "Інструмент можна взяти лише зі складу");
        if (p.tool != "axe") return fail("ActionError", "Невідомий інструмент");
        a.kind = ActionKind::Take;
        if (c.tool == p.tool) {
            a.noop = true;
        } else if (!c.tool.empty()) {
            return fail("ActionError", "Слот інструмента зайнятий: спершу поверніть інструмент (put_back)");
        } else if (t->axes <= 0) {
            return fail("NoItemError", "На складі " + quote(t->name) + " немає сокири");
        } else if (t->axes - t->axes_reserved <= 0) {
            return fail("ReservedError", "Усі сокири на складі " + quote(t->name) + " зарезервували інші персонажі");
        } else {
            t->axes_reserved++;
            a.reserved = true;
        }
    } else if (kind == "put_back") {
        if (!require(ObjKind::Storage)) return fail("ActionError", "Інструмент можна повернути лише на склад");
        if (c.tool.empty()) return fail("NoItemError", quote(c.name) + " не тримає інструмента");
        a.kind = ActionKind::PutBack;
    } else if (kind == "chop_wood") {
        if (!require(ObjKind::Forest)) return fail("ActionError", "Рубати дрова можна лише в лісі");
        if (c.tool != "axe")
            return fail("NoItemError", "Щоб рубати дрова, потрібна сокира в руках (me.take(Axe, склад))");
        if (carry_weight(c) + balance_.wood_kg > balance_.weight_limit_kg + kEps)
            return fail("TooHeavyError", "Руки повні: більше дров не піднести");
        if (forest_units_left(*t) <= 0) return fail("NoItemError", "У лісі " + quote(t->name) + " не лишилося дерев");
        a.kind = ActionKind::ChopWood;
    } else if (kind == "drop") {
        if (!require(ObjKind::Storage)) return fail("ActionError", "Покласти речі можна лише на склад");
        a.kind = ActionKind::Drop;
    } else if (kind == "pick_up") {
        if (!require(ObjKind::Storage)) return fail("ActionError", "Брати ресурси можна лише зі складу");
        if (!is_resource(p.resource)) return fail("ActionError", "Невідомий ресурс " + quote(p.resource));
        if (p.count <= 0) return fail("ActionError", "Кількість має бути більшою за нуль");
        a.kind = ActionKind::PickUp;
    } else if (kind == "eat") {
        if (!is_resource(p.resource)) return fail("ActionError", "Невідомий ресурс " + quote(p.resource));
        if (!is_edible(p.resource)) return fail("ActionError", quote(p.resource) + " не можна з'їсти");
        auto it = c.carrying.find(p.resource);
        if (it == c.carrying.end() || it->second < 1)
            return fail("NoItemError", quote(c.name) + " не несе " + quote(p.resource) + " — спершу візьміть зі складу (pick_up)");
        a.kind = ActionKind::Eat;
    } else if (kind == "sleep") {
        a.kind = ActionKind::Sleep;
    } else if (kind == "rest") {
        a.kind = ActionKind::Rest;
    } else if (kind == "wait") {
        a.kind = ActionKind::Wait;
        const double now = c.local_time;
        if (p.until) {
            a.wait_target = resolve_time_of_day(*p.until, now);
            a.wait_returns_bool = true;
        } else if (p.seconds) {
            a.wait_target = now + std::max(0.0, *p.seconds);
        } else {
            return fail("ActionError", "Не вказано, чого чекати");
        }
        if (p.timeout) a.wait_timeout = now + std::max(0.0, *p.timeout);
    } else {
        return fail("ActionError", "Невідома дія " + quote(kind));
    }

    if (a.kind == ActionKind::ChopWood || a.kind == ActionKind::Sleep || a.kind == ActionKind::Rest) {
        const double now = c.local_time;
        if (p.until) a.deadline = std::min(a.deadline, resolve_time_of_day(*p.until, now));
        if (p.duration) a.deadline = std::min(a.deadline, now + *p.duration);
        a.has_limits = p.amount.has_value() || p.until.has_value() || p.duration.has_value();
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
            s.axes--;
            s.axes_reserved--;
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
            s.axes++;
            c.tool.clear();
            return done(used);
        }
        case ActionKind::ChopWood:
            return step_chop(a, c, budget);
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

StepResult World::step_chop(Action& a, Character& c, double budget) {
    const Balance& b = balance_;
    Object& f = objects_[a.target];
    const double unit = b.chop_minutes_per_unit * 60.0;
    double used = 0;
    for (;;) {
        if (!c.alive) return running(budget);
        if (a.params.amount && a.done >= *a.params.amount) return done_int(used, a.done);
        if (c.local_time >= a.deadline - kEps) return done_int(used, a.done);
        double left = budget - used;
        if (left <= kEps) return running(used);
        if (!at(c, f.pos)) {
            used += walk(c, f.pos, std::min(left, a.deadline - c.local_time));
            continue;
        }
        if (carry_weight(c) + b.wood_kg > b.weight_limit_kg + kEps) {
            if (!a.has_limits && a.done > 0) return done_int(used, a.done);
            return failed(used, "TooHeavyError", "Руки повні: нарубано " + std::to_string(a.done) + ", більше не піднести",
                          a.done);
        }
        if (forest_units_left(f) <= 0)
            return failed(used, "NoItemError", "У лісі " + quote(f.name) + " не лишилося дерев", a.done);

        double factor = 1.0;
        if (is_raining_at(c.local_time)) factor *= b.rain_work_factor;
        if (c.fatigue >= b.fatigue_slow_threshold) factor *= b.fatigue_slow_factor;
        double& partial = c.chop_partial[f.id];
        double need = (1.0 - partial) * unit / factor;
        double t = std::min({left, a.deadline - c.local_time, need});
        pass_time(c, t, Activity::Heavy);
        used += t;
        if (t >= need - kEps) {
            partial = 0;
            c.carrying["wood"] += 1;
            a.done += 1;
            f.wood_taken += 1;
        } else {
            partial = std::min(1.0, partial + t * factor / unit);
        }
    }
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

void World::end_action(int action_id) {
    auto it = actions_.find(action_id);
    if (it == actions_.end()) return;
    Action& a = it->second;
    if (a.reserved) {
        if (Object* s = object(a.target)) s->axes_reserved--;
        a.reserved = false;
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
