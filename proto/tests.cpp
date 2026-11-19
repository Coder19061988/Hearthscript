// Scenario tests of the technical prototype (MVP §7) and tempo checks ("Баланс_прототипу.md" §6).
// Run all: kindle_proto test; one: kindle_proto test rain_interrupt. CTest registers each one separately.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "proto.h"
#include "save.h"

using namespace kindle;

namespace {

double at(int day, int h, int m = 0) { return (day - 1) * kDay + h * 3600.0 + m * 60.0; }

struct Opts {
    int chars = 1;
    int axes = 1;
    int sickles = 0;
    int scythes = 0;
    int pickaxes = 0;
    int bread = 10;
    int grain = 0;
    int flax = 0;
    int wood = 0;
    int trees = 200;
    int start_day = 1;
    double start_hour = 5.5;
    bool random_rain = false;
    uint64_t seed = 1;
};

// The start of the game (godot/scripts/main.gd uses the same numbers).
Opts mvp_opts() {
    Opts o;
    o.chars = 4;
    o.axes = 3;  // two woodcutters + a spare: axes wear out and can't be repaired yet
    o.sickles = 2;
    o.scythes = 1;
    o.pickaxes = 1;
    o.bread = 40;
    o.grain = 30;
    o.flax = 20;
    o.random_rain = true;
    o.seed = 3;
    return o;
}

const Balance& default_balance() {
    static Balance balance = proto::load_balance(proto::source_path("config/balance.json"), false);
    return balance;
}

class Ctx {
public:
    bool ok = true;

    // `custom`: a balance of the test's own (e.g. chances raised to certainty); default — config/balance.json.
    Simulation& sim(const Opts& o, const Balance* custom = nullptr) {
        sims_.push_back(std::make_unique<Simulation>(custom ? *custom : default_balance(), o.seed));
        Simulation& s = *sims_.back();
        s.world().set_random_rain(o.random_rain);
        StartScene scene;
        scene.characters = o.chars;
        scene.axes = o.axes;
        scene.sickles = o.sickles;
        scene.scythes = o.scythes;
        scene.pickaxes = o.pickaxes;
        scene.bread = o.bread;
        scene.grain = o.grain;
        scene.flax = o.flax;
        scene.trees = o.trees;
        scene.start_time = (o.start_day - 1) * kDay + o.start_hour * 3600.0;
        build_start_scene(s.world(), scene);
        s.world().object(s.world().find("Склад_1")->id)->resources["wood"] = o.wood;
        return s;
    }

    void check(bool cond, const std::string& what) {
        if (cond) return;
        ok = false;
        std::printf("    ПРОВАЛ: %s\n", what.c_str());
    }
    void info(const std::string& text) { std::printf("    %s\n", text.c_str()); }

    void dump_logs() {
        for (auto& s : sims_) {
            std::printf("    --- журнал подій ---\n");
            for (const LogEntry& e : s->world().log_entries())
                std::printf("    %s\n", proto::format_entry(s->world(), e).c_str());
        }
    }

private:
    std::vector<std::unique_ptr<Simulation>> sims_;
};

std::optional<LogEntry> find_log(World& w, const std::string& needle) {
    for (const LogEntry& e : w.log_entries())
        if (e.text.find(needle) != std::string::npos) return e;
    return std::nullopt;
}

int count_log(World& w, const std::string& needle) {
    int n = 0;
    for (const LogEntry& e : w.log_entries()) n += e.text.find(needle) != std::string::npos ? 1 : 0;
    return n;
}

int count_kind(World& w, LogKind kind) {
    int n = 0;
    for (const LogEntry& e : w.log_entries()) n += e.kind == kind ? 1 : 0;
    return n;
}

bool time_in(const std::optional<LogEntry>& e, double from, double to) { return e && e->time >= from && e->time < to; }

std::string hours_str(double seconds) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f год", seconds / 3600.0);
    return buf;
}

Object& obj(World& w, const char* name) { return *w.object(w.find(name)->id); }

// Tools of a kind lying in a storage; `reserved_only` counts those promised to someone on the way.
int tools_of(const Object& s, const std::string& kind, bool reserved_only = false) {
    int n = 0;
    for (const ToolItem& t : s.tools) n += t.kind == kind && (!reserved_only || t.reserved_by >= 0) ? 1 : 0;
    return n;
}

double dist(World& w, int a, int b) {
    Vec2 p = w.objects()[a].pos, q = w.objects()[b].pos;
    return std::hypot(p.x - q.x, p.y - q.y);
}

bool load_script(Ctx& ctx, const char* relative, std::string& out) {
    bool ok = proto::read_file(proto::source_path(relative), out);
    ctx.check(ok, std::string("прочитати ") + relative);
    return ok;
}

// ------------------------------------------------------------------ MVP §7, items 1-3

void t_smoke_chop_sleep(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    World& w = s.world();
    int hero = w.char_obj_id(0);
    s.runtime().set_code(hero, R"PY(
storage = get("Склад_1")
forest = get("Ліс_1")
me.take(Axe, storage)
n = me.chop_wood(forest, amount=3)
assert n == 3, n
assert me.carrying == {"wood": 3}, me.carrying
me.drop(storage)
me.put_back(storage)
f = me.fatigue
me.sleep(hours=2)
assert me.fatigue < f, (me.fatigue, f)
notify("готово")
)PY");
    s.run_until(at(1, 12));
    auto fin = find_log(w, "готово");
    ctx.check(fin.has_value(), "скрипт дійшов до кінця");
    ctx.check(s.runtime().state(hero) == AgentState::Finished, "стан коду — «завершився»");
    Object& st = obj(w, "Склад_1");
    ctx.check(st.resources["wood"] == 3, "на складі 3 дрова");
    ctx.check(tools_of(st, "axe") == 1 && tools_of(st, "axe", true) == 0, "сокира повернулась на склад");
    if (fin) {
        ctx.info("кінець: " + w.format_time(fin->time));
        ctx.check(time_in(fin, at(1, 8, 30), at(1, 10)), "тривалість правдоподібна (кінець між 08:30 і 10:00)");
    }
}

void t_time_flow(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    s.runtime().set_code(s.world().char_obj_id(0), R"PY(
t0 = world.time
d0 = world.day
me.rest(hours=1)
d = world.time - t0
assert hours(1) <= d < hours(1) + minutes(1), d
assert time(5, 30) <= t0 < time(5, 31), t0
assert (time(23, 0) + hours(2)) == time(1, 0)
assert world.time < time(24, 0)
me.wait_until(time(1, 0))
assert world.day == d0 + 1, world.day
assert world.is_night
me.wait_until(time(12, 0))
assert not world.is_night
notify(f"час іде: {t0} -> {world.time}, день {world.day}")
)PY");
    s.run_until(at(2, 13));
    ctx.check(find_log(s.world(), "час іде").has_value(), "усі перевірки часу в скрипті пройшли");
}

void t_action_limits(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    int hero = s.world().char_obj_id(0);
    s.runtime().set_code(hero, R"PY(
storage = get("Склад_1")
forest = get("Ліс_1")
me.take(Axe, storage)
me.go_to(forest)
assert me.chop_wood(forest, amount=2) == 2
start = world.time
n = me.chop_wood(forest, until=start + minutes(30))
assert n == 1, n
t = world.time
n = me.chop_wood(forest, hours=1)
assert n == 3, n
assert hours(1) <= world.time - t < hours(1) + minutes(1), world.time - t
assert me.carrying["wood"] == 6, me.carrying
notify("обмеження працюють")
)PY");
    s.run_until(at(1, 12));
    ctx.check(find_log(s.world(), "обмеження працюють").has_value(), "amount=, until=, hours= і незавершений прогрес");
    double partial = s.world().character(hero).work_partial[s.world().find("Ліс_1")->id];
    ctx.info("незавершена одиниця дров: " + std::to_string(partial));
    ctx.check(partial > 0.3 && partial < 0.7, "прогрес незавершеної одиниці збережено (~0.5)");
}

// ------------------------------------------------------------------ MVP §7, item 4: @on interrupts

void t_rain_interrupt(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    World& w = s.world();
    w.add_rain(at(1, 7), at(1, 8));
    int hero = w.char_obj_id(0);
    s.runtime().set_code(hero, R"PY(
@on(Event.RAIN, priority=1)
def hide(me):
    notify("дощ: іду додому")
    me.go_home()
    me.wait_until(lambda: not world.is_raining, timeout=hours(3))
    notify("дощ скінчився")

storage = get("Склад_1")
forest = get("Ліс_1")
me.take(Axe, storage)
n = me.chop_wood(forest, amount=6)
notify(f"нарубано {n}")
)PY");
    s.run_until(at(1, 7, 30));
    ctx.check(dist(w, hero, w.find("Будинок_1")->id) < 1.0, "о 07:30 персонаж удома (рубку перервано)");
    s.run_until(at(1, 12));
    auto start = find_log(w, "дощ: іду додому");
    auto stop = find_log(w, "дощ скінчився");
    auto fin = find_log(w, "нарубано 6");
    ctx.check(time_in(start, at(1, 7), at(1, 7, 1)), "обробник запустився о 07:00");
    ctx.check(time_in(stop, at(1, 8), at(1, 8, 2)), "обробник дочекався кінця дощу (08:00)");
    ctx.check(fin.has_value(), "після обробника рубка продовжилась і завершилась");
    // 6 units = 120 min of chopping; 83 min were done before the rain, so ~37 min + walk remain.
    if (fin) ctx.info("рубка завершена: " + w.format_time(fin->time));
    ctx.check(time_in(fin, at(1, 8, 30), at(1, 9)), "прогрес не втрачено: завершення між 08:30 і 09:00");
    ctx.check(w.character(hero).carrying["wood"] == 6, "у руках 6 дров");
}

void t_handler_priority(Ctx& ctx) {
    const char* code = R"PY(
@on(Event.RAIN, priority=2)
def on_rain(me):
    notify("A: дощ, відпочиваю")
    me.rest(hours=2)
    notify("A: кінець")

@on(Event.RAIN_STOPPED, priority=PRIORITY)
def on_stop(me, event):
    assert event.kind == Event.RAIN_STOPPED
    notify("B: дощ скінчився")

me.wait_until(time(12, 0))
notify("main: кінець")
)PY";
    // Higher priority (smaller number) interrupts the running handler.
    {
        Simulation& s = ctx.sim({});
        World& w = s.world();
        w.add_rain(at(1, 7), at(1, 7, 30));
        std::string src = std::string("PRIORITY = 1\n") + code;
        s.runtime().set_code(w.char_obj_id(0), src);
        s.run_until(at(1, 13));
        auto a = find_log(w, "A: дощ"), b = find_log(w, "B: дощ"), a_end = find_log(w, "A: кінець"),
             main_end = find_log(w, "main: кінець");
        ctx.check(time_in(a, at(1, 7), at(1, 7, 1)), "A почався о 07:00");
        ctx.check(time_in(b, at(1, 7, 30), at(1, 7, 31)), "B (пріоритет 1) перервав A о 07:30");
        ctx.check(time_in(a_end, at(1, 9), at(1, 9, 1)), "A завершився о 09:00 (після B)");
        ctx.check(time_in(main_end, at(1, 12), at(1, 12, 1)), "основний код продовжився до 12:00");
    }
    // Equal priority waits until the running handler finishes.
    {
        Simulation& s = ctx.sim({});
        World& w = s.world();
        w.add_rain(at(1, 7), at(1, 7, 30));
        std::string src = std::string("PRIORITY = 2\n") + code;
        s.runtime().set_code(w.char_obj_id(0), src);
        s.run_until(at(1, 13));
        auto b = find_log(w, "B: дощ"), a_end = find_log(w, "A: кінець");
        ctx.check(a_end && b && b->time >= a_end->time, "B з рівним пріоритетом чекав, доки A завершиться");
    }
}

void t_handler_exception(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    World& w = s.world();
    w.add_rain(at(1, 7), at(1, 7, 10));
    int hero = w.char_obj_id(0);
    s.runtime().set_code(hero, R"PY(
@on(Event.RAIN)
def bad(me):
    raise ValueError("зламаний обробник")

try:
    me.wait_until(time(12, 0))
except Exception:
    notify("перехоплено в основному коді")
notify("після")
)PY");
    s.run_until(at(1, 12));
    auto err = find_log(w, "зламаний обробник");
    ctx.check(err && err->kind == LogKind::Error, "помилка обробника записана в журнал");
    if (err) ctx.info(err->text);
    ctx.check(!find_log(w, "перехоплено").has_value(), "основний код не перехоплює виняток обробника");
    ctx.check(!find_log(w, "після").has_value(), "код персонажа зупинився");
    ctx.check(s.runtime().state(hero) == AgentState::Crashed, "стан — «зупинений помилкою»");
}

// ------------------------------------------------------------------ MVP §7, item 5: exceptions

void t_exceptions_handled(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    World& w = s.world();
    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
forest = get("Ліс_1")
try:
    me.chop_wood(forest, amount=1)
except NoItemError as e:
    assert isinstance(e, ActionError)
    notify(f"без сокири: {e.reason}")
try:
    get("Склад_99")
except NotFoundError as e:
    notify("немає складу")
try:
    me.pick_up(storage, "bread", 1000)
except NoItemError as e:
    notify(f"мало хліба: {e.done}")
try:
    me.go_home()
except NoHomeError:
    notify("бездомний")
me.take(Axe, storage)
try:
    me.chop_wood(forest, amount=15)
except TooHeavyError as e:
    notify(f"повні руки: {e.done}")
    assert e.done == 10, e.done
assert storage.has_tool(Axe) == False
notify("винятки ok")
)PY");
    s.run_until(at(1, 12));
    for (const char* expected : {"без сокири:", "немає складу", "мало хліба: 0", "повні руки: 10", "винятки ok"})
        ctx.check(find_log(w, expected).has_value(), std::string("є запис «") + expected + "»");
    ctx.check(!find_log(w, "бездомний").has_value(), "go_home() у персонажа з домом не падає");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

void t_unhandled_exception(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    World& w = s.world();
    int hero = w.char_obj_id(0);
    s.runtime().set_code(hero,
                         "notify(\"старт\")\n"
                         "me.chop_wood(get(\"Ліс_1\"), amount=1)\n"
                         "notify(\"сюди не дійде\")\n");
    s.run_until(at(1, 9));
    auto err = find_log(w, "NoItemError");
    ctx.check(err && err->kind == LogKind::Error, "необроблений NoItemError у журналі");
    if (err) ctx.info(err->text);
    ctx.check(err && err->text.find("(рядок 2)") != std::string::npos, "у записі вказано рядок 2");
    ctx.check(!find_log(w, "сюди не дійде").has_value(), "код зупинився на помилці");
    ctx.check(s.runtime().state(hero) == AgentState::Crashed, "стан — «зупинений помилкою»");
    Character& c = w.character(hero);
    ctx.check(std::fabs(c.local_time - w.now()) < 1.0, "персонаж стоїть, але час для нього йде");
    ctx.check(c.hunger > 40, "голод зростає, поки персонаж стоїть без діла");
}

void t_reserved_tool(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const char* code = R"PY(
storage = get("Склад_1")
try:
    me.take(Axe, storage)
    notify("сокира в руках")
except ReservedError as e:
    notify("зарезервовано: " + e.reason)
except NoItemError as e:
    notify("немає: " + e.reason)
)PY";
    for (Character& c : w.characters()) s.runtime().set_code(c.obj, code);
    s.run_until(at(1, 7));
    ctx.check(count_log(w, "сокира в руках") == 1, "сокиру отримав рівно один персонаж");
    ctx.check(count_log(w, "зарезервовано:") == 1, "другий отримав ReservedError");
}

// ------------------------------------------------------------------ MVP §7, item 6: code change

void t_restart_on_code_change(Ctx& ctx) {
    {
        Simulation& s = ctx.sim({});
        World& w = s.world();
        int hero = w.char_obj_id(0);
        int forest = w.find("Ліс_1")->id;
        s.runtime().set_code(hero, R"PY(
me.take(Axe, get("Склад_1"))
me.chop_wood(get("Ліс_1"), amount=10)
)PY");
        s.run_until(at(1, 6, 30));
        double partial = w.character(hero).work_partial[forest];
        ctx.info("до зміни коду: дров " + std::to_string(w.character(hero).carrying["wood"]) + ", незавершена одиниця " +
                 std::to_string(partial));
        ctx.check(partial > 0.1 && partial < 0.95, "зміна коду посеред одиниці дров");
        s.runtime().set_code(hero, R"PY(
notify("B старт: дрова " + str(me.carrying.get("wood", 0)) + ", інструмент " + str(me.tool))
me.chop_wood(get("Ліс_1"), amount=1)
notify("B: одиниця готова")
)PY");
        s.run_until(at(1, 8));
        ctx.check(find_log(w, "виконання почалося спочатку").has_value(), "перезапуск записано в журнал");
        auto b0 = find_log(w, "B старт"), b1 = find_log(w, "B: одиниця готова");
        ctx.check(b0 && b0->text.find("дрова 2") != std::string::npos && b0->text.find("Axe") != std::string::npos,
                  "новий код бачить нарубані дрова й сокиру");
        if (b0 && b1) {
            double took = b1->time - b0->time;
            double expected = (1.0 - partial) * s.world().balance().chop_minutes_per_unit * 60.0;
            ctx.info("остання одиниця зайняла " + std::to_string(took / 60.0) + " хв (очікувано ~" +
                     std::to_string(expected / 60.0) + ")");
            ctx.check(std::fabs(took - expected) < 60.0, "незавершений прогрес рубки збережено");
        }
        ctx.check(w.character(hero).carrying["wood"] == 3, "у руках 3 дрова");
    }
    {
        // Restart in the middle of take(): the axe reservation must be released.
        Simulation& s = ctx.sim({});
        World& w = s.world();
        int hero = w.char_obj_id(0);
        s.runtime().set_code(hero, "me.take(Axe, get(\"Склад_1\"))\n");
        s.run_for(30, 30);
        Object& st = obj(w, "Склад_1");
        ctx.check(tools_of(st, "axe", true) == 1, "під час take() сокира зарезервована");
        s.runtime().set_code(hero, "pass\n");
        ctx.check(tools_of(st, "axe", true) == 0 && tools_of(st, "axe") == 1, "після зміни коду резерв знято");
    }
}

void t_syntax_error(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    World& w = s.world();
    int hero = w.char_obj_id(0);
    s.runtime().set_code(hero, "while True\n    pass\n");
    s.run_until(at(1, 7));
    ctx.check(count_kind(w, LogKind::Error) == 1, "синтаксична помилка записана в журнал");
    for (const LogEntry& e : w.log_entries())
        if (e.kind == LogKind::Error) ctx.info(e.text);
    ctx.check(s.runtime().state(hero) == AgentState::Crashed, "код не запущено");
    ctx.check(std::fabs(w.character(hero).local_time - w.now()) < 1.0, "час для персонажа йде");
}

// ------------------------------------------------------------------ MVP §7, item 7: empty loops

void t_empty_loop(Ctx& ctx) {
    Opts o;
    o.chars = 3;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    s.runtime().set_code(w.char_obj_id(0), "while True: pass\n");
    s.runtime().set_code(w.char_obj_id(1), "x = 0\nwhile True:\n    x += 1\n");
    s.runtime().set_code(w.char_obj_id(2), "exec(\"while True: pass\")\n");
    auto t0 = std::chrono::steady_clock::now();
    s.run_until(at(1, 11, 30));
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    ctx.info("6 ігрових годин порожніх циклів: " + std::to_string(wall) + " с реального часу");
    ctx.check(wall < 20.0, "гра не зависає");
    for (Character& c : w.characters()) {
        ctx.check(std::fabs(c.local_time - w.now()) < 5.0, c.name + ": час іде");
        ctx.check(c.hunger > 50, c.name + ": голод зростає");
    }
    ctx.check(count_log(w, "без жодної дії") == 3, "кожен порожній цикл записано як бездіяльність");
    auto idle = find_log(w, "без жодної дії");
    ctx.check(time_in(idle, at(1, 9, 30), at(1, 9, 32)), "запис про бездіяльність через 4 години");
}

// ------------------------------------------------------------------ sandbox

void t_sandbox(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    World& w = s.world();
    int hero = w.char_obj_id(0);
    s.runtime().set_code(hero, R"PY(
holes = []
attempts = ["import os", "import time", "import sys", "__import__('os')", "open('x.txt', 'w')",
            "exit()", "input()", "exec('import os')", "eval(\"__import__('os')\")"]
for src in attempts:
    try:
        exec(src)
        holes.append(src)
    except Exception:
        pass
assert not holes, holes
try:
    import math
    holes.append("math")
except ImportError as e:
    notify("import заблоковано: " + str(e))
print("привіт із print")
notify("пісочниця ok")
)PY");
    s.run_until(at(1, 7));
    ctx.check(find_log(w, "пісочниця ok").has_value(), "жодна заборонена операція не вдалася");
    ctx.check(find_log(w, "import заблоковано").has_value(), "import дає ImportError");
    auto printed = find_log(w, "привіт із print");
    ctx.check(printed && printed->kind == LogKind::Print, "print() пише в журнал подій");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

void t_wait_until(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    World& w = s.world();
    s.runtime().set_code(w.char_obj_id(0), R"PY(
assert me.wait_until(time(6, 0)) == True
assert time(6, 0) <= world.time < time(6, 1), world.time
r = me.wait_until(lambda: False, timeout=hours(1))
assert r == False
assert time(7, 0) <= world.time < time(7, 2), world.time
r = me.wait_until(time(23, 0), timeout=minutes(30))
assert r == False, r
assert time(7, 30) <= world.time < time(7, 33), world.time
t = world.time
assert me.wait_until(time(7, 25)) == True
assert world.time - t < minutes(1), world.time
r = me.wait_until(lambda: world.time >= time(8, 0))
assert r == True
assert time(8, 0) <= world.time < time(8, 2), world.time
notify("очікування ok")
)PY");
    s.run_until(at(1, 9));
    ctx.check(find_log(w, "очікування ok").has_value(), "час доби, умова, тайм-аут, щойно минулий час");
}

// ------------------------------------------------------------------ tempo ("Баланс_прототипу.md" §6)

void t_tempo_starvation(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    World& w = s.world();
    int hero = w.char_obj_id(0);
    s.runtime().set_code(hero, "while True:\n    me.rest(hours=1)\n");
    s.run_until(at(7, 0), 300);
    Character& c = w.character(hero);
    ctx.check(!c.alive, "персонаж без їжі помирає");
    if (!c.alive) {
        double days = (c.death_time - at(1, 5, 30)) / kDay;
        ctx.info("смерть від голоду через " + std::to_string(days) + " дн. (причина: " + c.death_cause + ")");
        ctx.check(days >= 3.0 && days <= 3.5, "орієнтир: 3–3,5 ігрові дні");
    }
}

void t_tempo_no_sleep(Ctx& ctx) {
    Opts o;
    o.bread = 30;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    int hero = w.char_obj_id(0);
    s.runtime().set_code(hero, R"PY(
storage = get("Склад_1")
forest = get("Ліс_1")
me.take(Axe, storage)
while True:
    me.chop_wood(forest)
    me.drop(storage)
    if me.hunger > 40:
        me.pick_up(storage, "bread", 1)
        me.eat("bread")
)PY");
    double reached = -1;
    while (w.now() < at(4, 0)) {
        s.tick(60);
        if (w.character(hero).fatigue >= 99.9) {
            reached = w.now();
            break;
        }
    }
    ctx.check(reached > 0, "без сну втома доходить до 100");
    if (reached > 0) {
        double span = reached - at(1, 5, 30);
        ctx.info("втома 100 через " + hours_str(span));
        ctx.check(span >= 12 * 3600.0 && span <= 30 * 3600.0, "орієнтир: приблизно доба (12–30 год)");
    }
}

void t_tempo_workday(Ctx& ctx) {
    std::string source;
    if (!load_script(ctx, "scripts/woodcutter.py", source)) return;
    Opts o;
    o.bread = 30;
    o.random_rain = true;
    o.seed = 7;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    int hero = w.char_obj_id(0);
    s.runtime().set_code(hero, source);
    Object& st = obj(w, "Склад_1");

    std::vector<int> wood_by_day;
    std::vector<double> peak_by_day;
    std::vector<double> morning_by_day;
    int prev_wood = 0;
    for (int day = 1; day <= 3; ++day) {
        double peak = 0;
        while (w.now() < at(day + 1, 5, 59)) {
            s.tick(60);
            peak = std::max(peak, w.character(hero).fatigue);
        }
        morning_by_day.push_back(w.character(hero).fatigue);
        s.run_until(at(day + 1, 6));
        wood_by_day.push_back(st.resources["wood"] - prev_wood);
        prev_wood = st.resources["wood"];
        peak_by_day.push_back(peak);
    }
    double avg = prev_wood / 3.0;
    for (int d = 0; d < 3; ++d) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "день %d: дров %d, найбільша втома %.0f, втома зранку після сну %.0f", d + 1,
                      wood_by_day[d], peak_by_day[d], morning_by_day[d]);
        ctx.info(buf);
    }
    ctx.info("у середньому " + std::to_string(avg) + " дров за день");
    ctx.check(w.character(hero).alive, "лісоруб живий");
    ctx.check(avg >= 22 && avg <= 34, "орієнтир: 25–30 дров за день (допуск 22–34)");
    ctx.check(peak_by_day[1] >= 55 && peak_by_day[1] <= 85, "орієнтир: робочий день піднімає втому приблизно до 75");
    ctx.check(morning_by_day[1] <= 5, "орієнтир: сон удома повністю знімає втому");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// ------------------------------------------------------------------ MVP §7, item 8: load

void t_load_50(Ctx& ctx) {
    std::string source;
    if (!load_script(ctx, "scripts/woodcutter.py", source)) return;
    Opts o;
    o.chars = 50;
    o.axes = 50;
    o.bread = 50 * 10;
    o.trees = 50 * 20;
    o.random_rain = true;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    for (Character& c : w.characters()) s.runtime().set_code(c.obj, source);
    auto t0 = std::chrono::steady_clock::now();
    s.run_until(at(3, 5, 30));
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    int alive = 0;
    for (Character& c : w.characters()) alive += c.alive ? 1 : 0;
    ctx.info("50 персонажів, 2 ігрові доби: " + std::to_string(wall) + " с реального часу (" +
             std::to_string(wall / 2) + " с на добу; на x10 доба триває 72 с)");
    ctx.check(alive == 50, "усі 50 персонажів живі");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
    ctx.check(wall / 2 < 10.0, "доба моделюється швидше за 10 с");
    ctx.check(obj(w, "Склад_1").resources["wood"] > 50 * 40, "усі працюють (понад 40 дров на персонажа за 2 дні)");
}

#include "tests_mvp.inc"

struct TestCase {
    const char* name;
    const char* title;
    void (*fn)(Ctx&);
};

const TestCase kTests[] = {
    {"smoke_chop_sleep", "рубка, склад, сон (MVP §7.1)", t_smoke_chop_sleep},
    {"time_flow", "хід ігрового часу (§7.2)", t_time_flow},
    {"action_limits", "until= / amount= / hours= (§7.3)", t_action_limits},
    {"rain_interrupt", "@on перериває дію, потім повернення (§7.4)", t_rain_interrupt},
    {"handler_priority", "пріоритети обробників (§7.4)", t_handler_priority},
    {"handler_exception", "виняток в обробнику зупиняє код (§7.4–7.5)", t_handler_exception},
    {"exceptions_handled", "винятки дій і try/except (§7.5)", t_exceptions_handled},
    {"unhandled_exception", "необроблений виняток у журналі (§7.5)", t_unhandled_exception},
    {"reserved_tool", "резервування сокири (ReservedError)", t_reserved_tool},
    {"restart_on_code_change", "зміна коду на ходу (§7.6)", t_restart_on_code_change},
    {"syntax_error", "синтаксична помилка в коді", t_syntax_error},
    {"empty_loop", "порожній цикл не вішає гру (§7.7)", t_empty_loop},
    {"sandbox", "пісочниця: import, open, exit", t_sandbox},
    {"wait_until", "wait_until: час, умова, тайм-аут", t_wait_until},
    {"tempo_starvation", "темп: смерть без їжі (§6)", t_tempo_starvation},
    {"tempo_no_sleep", "темп: втома без сну (§6)", t_tempo_no_sleep},
    {"tempo_workday", "темп: робочий день лісоруба (§6)", t_tempo_workday},
    {"load_50", "навантаження: 50 персонажів (§7.8)", t_load_50},
    {"seasons", "MVP: пори року й температура", t_seasons},
    {"cold_and_clothes", "MVP: холод і теплий одяг", t_cold_and_clothes},
    {"house_heating", "MVP: опалення будинку, сон у теплі й холоді", t_house_heating},
    {"field_cycle", "MVP: поле → зерно → жорна → борошно → піч → хліб", t_field_cycle},
    {"mvp_rules", "MVP: недозволені дії викидають винятки", t_mvp_rules},
    {"mvp_winter_survival", "MVP: хороший код переживає першу зиму", t_mvp_winter_survival},
    {"mvp_bad_code", "MVP: поганий код — смерті з причинами в журналі", t_mvp_bad_code},
    {"construction", "MVP: креслення → матеріали → будівництво → заселення", t_construction},
    {"construction_rules", "MVP: правила будівництва, поле за кресленням", t_construction_rules},
    {"globals", "MVP: глобальні змінні гравця", t_globals},
    {"workplace_busy", "MVP: зайняті жорна (WorkplaceBusyError)", t_workplace_busy},
    {"save_load", "MVP: збереження й завантаження гри", t_save_load},
    {"shared_code", "спільний код + код персонажа (пріоритет) + main()", t_shared_code},
    {"rules", "правила: @on(lambda: умова), @on(time(...)), off()", t_rules},
    {"rule_priority", "правила коду персонажа мають пріоритет над спільними", t_rule_priority},
    {"outcome", "кінець гри: першу зиму пережито / усі загинули", t_outcome},
    {"knowledge", "обмежене знання: бачить лише на місці, пам'ятає старе", t_knowledge},
    {"board", "дошка оголошень: me.post / me.read", t_board},
    {"tool_wear", "знос інструментів: ToolBrokenError, tools(...), take(інструмент)", t_tool_wear},
    {"animals", "тварини: хлів, сіно, молоко, яйця, забій, зима", t_animals},
    {"trade", "торгівля: пост, замовлення, купець, управа", t_trade},
    {"stats", "статистика для діаграм управи: знімок щогодини", t_stats},
    {"craft", "майстерні й рецепти: кузня, me.craft", t_craft},
    {"clothes", "ланцюжок одягу: льон, кравецька, знос одягу", t_clothes},
    {"wood", "ланцюжок дерева: лісопилка, столярня, ліжка", t_wood},
    {"mining", "камінь і метал: кайло, вуглярня, залізо", t_mining},
    {"water", "вода: колодязь, полив поля, дощ", t_water},
    {"ailments", "хвороби й травми: хто й від чого", t_ailments},
    {"treatment", "лікарня: трави, бинти, me.treat", t_treatment},
    {"mood", "настрій: робота, вільний час, поганий настрій", t_mood},
    {"church", "церква: служби, поховання, тіла", t_church},
    {"lighting", "освітлення: ліхтарі, смолоскипи, олійня", t_lighting},
    {"food", "їжа: псування, погріб, коптильня, сироварня, раціон", t_food},
    {"forestry", "лісорозсадник: саджанці, me.plant, ріст дерев", t_forestry},
    {"leisure", "лазня, пивоварня, корчма", t_leisure},
};

}  // namespace

namespace proto {

void list_tests() {
    for (const auto& t : kTests) std::printf("  %-24s %s\n", t.name, t.title);
}

int run_tests(const std::vector<std::string>& names) {
    std::vector<const TestCase*> selected;
    if (names.empty()) {
        for (const auto& t : kTests) selected.push_back(&t);
    } else {
        for (const auto& n : names) {
            const TestCase* found = nullptr;
            for (const auto& t : kTests)
                if (n == t.name) found = &t;
            if (!found) {
                std::fprintf(stderr, "Невідомий тест: %s (див. kindle_proto list)\n", n.c_str());
                return 2;
            }
            selected.push_back(found);
        }
    }
    int passed = 0;
    for (const TestCase* t : selected) {
        std::printf("[ТЕСТ] %s — %s\n", t->name, t->title);
        std::fflush(stdout);
        Ctx ctx;
        auto t0 = std::chrono::steady_clock::now();
        t->fn(ctx);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!ctx.ok) ctx.dump_logs();
        std::printf("  %s (%.0f мс)\n", ctx.ok ? "OK" : "ПРОВАЛ", ms);
        std::fflush(stdout);
        passed += ctx.ok ? 1 : 0;
    }
    std::printf("\nПройдено %d з %zu\n", passed, selected.size());
    return passed == static_cast<int>(selected.size()) ? 0 : 1;
}

}  // namespace proto
