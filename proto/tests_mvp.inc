// MVP scenarios: seasons, warmth and clothes, heating, the food chain, survival of the first winter.
// Included into tests.cpp (inside its anonymous namespace): uses Ctx, Opts, at(), find_log() etc.

void t_seasons(Ctx& ctx) {
    Simulation& s = ctx.sim({});
    World& w = s.world();
    s.runtime().set_code(w.char_obj_id(0), R"PY(
assert world.season == Season.WARM, world.season
assert world.season_days_left == 10, world.season_days_left
assert 5 <= world.temperature <= 25, world.temperature
notify(f"тепла пора: {world.temperature:.1f}")
)PY");
    s.run_until(at(1, 7));
    ctx.check(find_log(w, "тепла пора").has_value(), "у теплу пору world.season == WARM, температура плюсова");
    ctx.check(w.season_at(at(10, 23)) == Season::Warm && w.season_at(at(11, 0)) == Season::Winter, "зима з 11-го дня");
    ctx.check(w.season_at(at(16, 0)) == Season::Warm, "після 5 днів зими знову тепла пора");
    const double summer_day = w.outdoor_temperature(at(2, 14));
    const double winter_day = w.outdoor_temperature(at(12, 14));
    const double winter_night = w.outdoor_temperature(at(12, 2));
    char buf[160];
    std::snprintf(buf, sizeof buf, "температура: літо вдень %.1f, зима вдень %.1f, зима вночі %.1f", summer_day,
                  winter_day, winter_night);
    ctx.info(buf);
    ctx.check(summer_day > 15 && winter_day < 0 && winter_night < winter_day, "зима холодна, вночі холодніше");
    s.run_until(at(11, 1), 600);
    ctx.check(find_log(w, "Почалася зима").has_value(), "у журналі «Почалася зима»");
}

void t_cold_and_clothes(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    o.axes = 2;
    o.start_day = 11;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const std::string work = R"PY(
storage = get("Склад_1")
forest = get("Ліс_1")
if WARM:
    me.wear(WarmClothes)
    assert me.clothes == WarmClothes
me.take(Axe, storage)
while True:
    me.chop_wood(forest, hours=1)
    me.drop(storage)
)PY";
    s.runtime().set_code(w.char_obj_id(0), "WARM = False\n" + work);
    s.runtime().set_code(w.char_obj_id(1), "WARM = True\n" + work);
    s.run_until(at(11, 14));
    Character& cold = w.character(w.char_obj_id(0));
    Character& warm = w.character(w.char_obj_id(1));
    char buf[160];
    std::snprintf(buf, sizeof buf, "тепло о 14:00: звичайний одяг %.0f (здоров'я %.0f), теплий одяг %.0f", cold.warmth,
                  cold.health, warm.warmth);
    ctx.info(buf);
    ctx.check(cold.warmth < 40, "у звичайному одязі взимку персонаж мерзне");
    ctx.check(find_log(w, "мерзне").has_value(), "попередження «мерзне» в журналі");
    ctx.check(warm.warmth > 80 && warm.clothes == "warm_clothes", "у теплому одязі тепло тримається");
    Object& house = obj(w, "Будинок_1");
    ctx.check(house.chest["warm_clothes"] == 1 && house.chest["clothes"] == 1, "одяг поміняли через скриню будинку");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

void t_house_heating(Ctx& ctx) {
    const std::string code = R"PY(
house = get("Будинок_1")
storage = get("Склад_1")
if HEAT:
    me.pick_up(storage, "wood", 10)
    assert me.heat(house) == 10
    assert house.is_heated
me.sleep(until=time(6, 0))
notify(f"ранок: тепло {me.warmth:.0f}")
me.pick_up(storage, "grain", 5)
assert me.grind(house) == 5
try:
    n = me.bake(house)
    notify(f"спечено {n}")
except NoItemError as e:
    notify("піч холодна: " + e.reason)
)PY";
    double temps[2] = {0, 0};
    for (int heat = 1; heat >= 0; --heat) {
        Opts o;
        o.start_day = 11;
        o.start_hour = 18.0;
        o.wood = 10;
        o.grain = 5;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        int hero = w.char_obj_id(0);
        s.runtime().set_code(hero, std::string(heat ? "HEAT = True\n" : "HEAT = False\n") + code);
        s.run_until(at(11, 23));
        temps[heat] = obj(w, "Будинок_1").temperature;
        s.run_until(at(12, 8));
        Character& c = w.character(hero);
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s: о 23:00 у будинку %.1f°C; зранку тепло %.0f, здоров'я %.0f",
                      heat ? "з опаленням" : "без опалення", temps[heat], c.warmth, c.health);
        ctx.info(buf);
        if (heat) {
            ctx.check(temps[1] > 15, "натоплений будинок прогрівся");
            ctx.check(c.warmth > 80 && c.health >= 95, "у теплому будинку ніч минула без шкоди");
            ctx.check(find_log(w, "спечено 5").has_value(), "у розпаленій печі спекли хліб");
            ctx.check(w.character(hero).carrying["bread"] == 5, "у руках 5 хлібин");
        } else {
            ctx.check(temps[0] < 0, "без опалення в будинку мороз");
            ctx.check(c.warmth < 30 && c.health < 100, "у холодному будинку персонаж мерзне вночі");
            ctx.check(find_log(w, "піч холодна").has_value(), "у холодній печі хліб не спекти (NoItemError)");
        }
    }
}

void t_field_cycle(Ctx& ctx) {
    Opts o;
    o.sickles = 1;
    o.grain = 20;
    o.wood = 10;
    o.bread = 30;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
field = get("Поле_1")
house = get("Будинок_1")
assert field.state == "empty"
me.pick_up(storage, "grain", 10)
me.sow(field)
assert field.state == "sown", field.state

def eat():
    if me.hunger > 40:
        me.pick_up(storage, "bread", 1)
        me.eat("bread")

while not field.is_ripe:
    eat()
    me.rest(hours=2)
assert field.grain_left == 80, field.grain_left
me.take(Sickle, storage)
total = 0
while field.is_ripe:
    total += me.harvest(field)
    me.drop(storage)
me.put_back(storage)
notify(f"зібрано {total}")
me.pick_up(storage, "wood", 5)
me.heat(house)
me.pick_up(storage, "grain", 20)
assert me.grind(house) == 20
assert me.bake(house) == 20
assert me.carrying == {"bread": 20}, me.carrying
notify("хліб готовий")
)PY");
    s.run_until(at(6, 12), 120);
    auto sown = find_log(w, "засіяно"), ripe = find_log(w, "дозріло");
    ctx.check(sown.has_value(), "поле засіяно");
    if (sown && ripe) ctx.info("засіяно: " + w.format_time(sown->time) + "; дозріло: " + w.format_time(ripe->time));
    ctx.check(sown && ripe && std::fabs(ripe->time - sown->time - 4 * kDay) < 600, "поле дозріває за 4 дні");
    ctx.check(find_log(w, "зібрано 80").has_value(), "зібрано 80 зерна");
    ctx.check(find_log(w, "хліб готовий").has_value(), "зерно → борошно → хліб");
    Object& st = obj(w, "Склад_1");
    ctx.check(st.resources["grain"] == 70, "на складі 20 − 10 (посів) + 80 (урожай) − 20 (на хліб) = 70 зерна");
    ctx.check(obj(w, "Поле_1").field_state == FieldState::Empty, "після жнив поле знову порожнє");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

void t_mvp_rules(Ctx& ctx) {
    const std::string helpers = R"PY(
storage = get("Склад_1")
field = get("Поле_1")
house = get("Будинок_1")

def expect(fn, exc, label):
    try:
        fn()
        notify("НЕ ВПАЛО: " + label)
    except exc:
        notify("ok: " + label)
)PY";
    {
        Opts o;
        o.sickles = 1;
        o.grain = 10;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        s.runtime().set_code(w.char_obj_id(0), helpers + R"PY(
expect(lambda: me.harvest(field), NoItemError, "жнива без серпа")
expect(lambda: me.sow(field), NoItemError, "посів без зерна в руках")
expect(lambda: me.grind(house), NoItemError, "жорна без зерна")
expect(lambda: me.heat(house), NoItemError, "опалення без дров")
me.pick_up(storage, "grain", 10)
assert me.grind(house, amount=2) == 2
expect(lambda: me.bake(house), NoItemError, "хліб у холодній печі")
me.take(Sickle, storage)
assert me.tool == Sickle
expect(lambda: me.harvest(field), NoItemError, "жнива недозрілого поля")
assert house.has(WarmClothes)
assert buildings(Field) == [field]
notify("правила ok")
)PY");
        s.run_until(at(1, 12));
        ctx.check(find_log(w, "правила ok").has_value(), "перевірки дійшли до кінця");
        ctx.check(!find_log(w, "НЕ ВПАЛО").has_value(), "кожна недозволена дія викидає виняток");
        ctx.check(count_log(w, "ok: ") == 6, "усі 6 очікуваних винятків");
    }
    {
        Opts o;
        o.grain = 10;
        o.start_day = 11;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        s.runtime().set_code(w.char_obj_id(0), helpers + R"PY(
assert world.season == Season.WINTER
me.pick_up(storage, "grain", 10)
expect(lambda: me.sow(field), ActionError, "посів узимку")
)PY");
        s.run_until(at(11, 8));
        ctx.check(find_log(w, "ok: посів узимку").has_value(), "узимку сіяти не можна");
    }
}

// MVP success criterion: good code survives the first winter.
void t_mvp_winter_survival(Ctx& ctx) {
    std::string source;
    if (!load_script(ctx, "scripts/mvp/settlement.py", source)) return;
    Opts o = mvp_opts();
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    for (Character& c : w.characters()) s.runtime().set_code(c.obj, source);

    double min_health = 100, min_warmth = 100;
    std::string winter_start;
    while (w.now() < at(16, 6)) {
        s.tick(60);
        for (Character& c : w.characters()) {
            min_health = std::min(min_health, c.health);
            if (w.season_at(w.now()) == Season::Winter) min_warmth = std::min(min_warmth, c.warmth);
        }
        if (winter_start.empty() && w.season_at(w.now()) == Season::Winter) {
            Object& st = obj(w, "Склад_1");
            char buf[200];
            std::snprintf(buf, sizeof buf, "на початок зими: хліб %d, зерно %d, дрова %d", st.resources["bread"],
                          st.resources["grain"], st.resources["wood"]);
            winter_start = buf;
        }
    }
    Object& st = obj(w, "Склад_1");
    int alive = 0;
    for (Character& c : w.characters()) alive += c.alive ? 1 : 0;
    ctx.info(winter_start);
    char buf[240];
    std::snprintf(buf, sizeof buf,
                  "після зими: живих %d/4, хліб %d, зерно %d, дрова %d; урожаїв %d; мін. здоров'я %.0f, мін. тепло "
                  "взимку %.0f",
                  alive, st.resources["bread"], st.resources["grain"], st.resources["wood"], count_log(w, "дозріло"),
                  min_health, min_warmth);
    ctx.info(buf);
    ctx.check(alive == 4, "усі четверо пережили першу зиму");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
    ctx.check(min_health > 50, "здоров'я не падало нижче 50");
    ctx.check(min_warmth > 30, "ніхто не мерз узимку");
}

// The other side of the criterion: code that ignores food and winter loses people, and the log says why.
void t_mvp_bad_code(Ctx& ctx) {
    std::string source;
    if (!load_script(ctx, "scripts/woodcutter.py", source)) return;
    Opts o = mvp_opts();
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    for (Character& c : w.characters()) s.runtime().set_code(c.obj, source);
    s.run_until(at(16, 6), 120);
    int dead = 0;
    for (Character& c : w.characters()) {
        if (c.alive) continue;
        ++dead;
        ctx.info(c.name + ": смерть " + w.format_time(c.death_time) + ", причина — " + c.death_cause);
    }
    ctx.check(dead > 0, "лише лісоруби без їжі й підготовки до зими — є загиблі");
    ctx.check(count_kind(w, LogKind::Death) == dead, "кожна смерть записана в журнал із причиною");
    ctx.check(find_log(w, "Хліб закінчився").has_value(), "журнал попереджає, що хліб закінчився");
}

// ------------------------------------------------------------------ MVP: construction, moving in, globals

void t_construction(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    o.wood = 40;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    std::string err;
    const int site = w.add_blueprint(ObjKind::House, "Будинок_2", {60, 100}, err);
    ctx.check(site >= 0, "креслення будинку поставлено: " + err);
    ctx.check(w.add_blueprint(ObjKind::Storage, "Будинок_2", {300, 0}, err) < 0, "назва креслення має бути унікальною");
    ctx.check(w.add_blueprint(ObjKind::Storage, "Склад_9", {60, 10}, err) < 0 && err.find("близько") != std::string::npos,
              "креслення не можна ставити впритул до будівлі");
    ctx.check(w.add_blueprint(ObjKind::Field, "Поле_9", {0, 260}, err) < 0, "креслення не можна ставити в лісі");
    const std::string code = R"PY(
storage = get("Склад_1")
site = get("Будинок_2")
assert not site.is_built
while site.needs:
    try:
        me.pick_up(storage, "wood", min(site.needs.get("wood", 0), 10))
    except NoItemError:
        break
    try:
        me.deliver(site)
    except ActionError:
        me.drop(storage)
me.build(site)
assert site.is_built
me.move_into(site)
assert me.home == site
notify(f"живу в {site.name}: {len(site.residents)}/{site.capacity}")
)PY";
    for (Character& c : w.characters()) s.runtime().set_code(c.obj, code);
    s.run_until(at(1, 20));
    auto built = find_log(w, "«Будинок_2» збудовано");
    ctx.check(built.has_value(), "будинок збудовано");
    if (built) ctx.info("збудовано: " + w.format_time(built->time) + " (16 год роботи на двох)");
    ctx.check(built && built->time > at(1, 12) && built->time < at(1, 16), "двоє будують 16-годинний будинок за ~8 год");
    ctx.check(find_log(w, "/4").has_value() && find_log(w, ": 2/4").has_value(), "обоє заселилися (2 з 4 місць)");
    ctx.check(obj(w, "Будинок_1").chest["warm_clothes"] == 0 && obj(w, "Будинок_2").chest["warm_clothes"] == 2,
              "теплий одяг переїхав разом із мешканцями");
    ctx.check(obj(w, "Склад_1").resources["wood"] == 0, "усі 40 дров пішли на будинок");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

void t_construction_rules(Ctx& ctx) {
    Opts o;
    o.grain = 10;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    std::string err;
    w.add_blueprint(ObjKind::Storage, "Склад_2", {-100, 0}, err);
    w.add_blueprint(ObjKind::Field, "Поле_2", {200, 0}, err);
    w.add_blueprint(ObjKind::House, "Будинок_2", {60, 100}, err);
    s.runtime().set_code(w.char_obj_id(0), R"PY(
def expect(fn, exc, label):
    try:
        fn()
        notify("НЕ ВПАЛО: " + label)
    except exc:
        notify("ok: " + label)

expect(lambda: me.build(get("Склад_2")), NoItemError, "будувати без матеріалів")
expect(lambda: me.deliver(get("Склад_2")), NoItemError, "нести на будову з порожніми руками")
expect(lambda: me.pick_up(get("Склад_2"), "wood", 1), ActionError, "брати з недобудованого складу")
expect(lambda: me.move_into(get("Будинок_2")), ActionError, "заселитися в креслення")
expect(lambda: me.sow(get("Поле_2")), ActionError, "сіяти на незораному полі")
expect(lambda: me.build(get("Ліс_1")), TypeError, "будувати ліс")
f = get("Поле_2")
assert f.needs == {} and f.progress == 0
me.build(f, hours=1)
assert 0.2 < f.progress < 0.3, f.progress
me.build(f)
assert f.is_built and f.state == "empty"
me.pick_up(get("Склад_1"), "grain", 10)
me.sow(f)
assert f.state == "sown"
assert get("Будинок_1").capacity == 4 and get("Будинок_1").free_places == 3
assert buildings(Storage) == [get("Склад_1"), get("Склад_2")]
notify("правила будівництва ok")
)PY");
    s.run_until(at(1, 14));
    ctx.check(find_log(w, "правила будівництва ok").has_value(), "перевірки дійшли до кінця");
    ctx.check(!find_log(w, "НЕ ВПАЛО").has_value() && count_log(w, "ok: ") == 6, "усі 6 очікуваних винятків");
    ctx.check(find_log(w, "«Поле_2» збудовано").has_value(), "поле за кресленням зорано за 4 год, без матеріалів");
}

void t_globals(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    std::string err;
    ctx.check(!s.runtime().set_global("stop", true, err) && !s.runtime().set_global("me", true, err) &&
                  !s.runtime().set_global("1X", true, err),
              "назви глобальних змінних — лише ВЕЛИКІ літери");
    ctx.check(s.runtime().set_global("STOP", false, err) && s.runtime().set_global("TARGET", 7LL, err),
              "глобальні змінні задано");
    s.runtime().set_code(w.char_obj_id(0), R"PY(
notify(f"ціль {TARGET}")
me.wait_until(lambda: STOP)
notify(f"наказ STOP о {world.time}, ціль {TARGET}")
)PY");
    s.runtime().set_code(w.char_obj_id(1), R"PY(
STOP = True
me.wait_until(time(9, 0))
notify(f"власна змінна STOP={STOP}")
)PY");
    s.run_until(at(1, 8));
    ctx.check(find_log(w, "ціль 7").has_value(), "код бачить глобальну змінну від самого старту");
    ctx.check(!find_log(w, "наказ STOP").has_value(),
              "присвоєння STOP = True в коді іншого персонажа не змінює наказ для всіх");
    s.runtime().set_global("STOP", true, err);
    s.runtime().set_global("TARGET", 9LL, err);
    s.run_until(at(1, 9, 30));
    auto order = find_log(w, "наказ STOP");
    ctx.check(time_in(order, at(1, 8), at(1, 8, 2)) && order->text.find("ціль 9") != std::string::npos,
              "новий наказ гравця дійшов до коду протягом хвилини");
    ctx.check(find_log(w, "власна змінна STOP=True").has_value(), "персонаж має власну копію змінної");
    ctx.check(s.runtime().remove_global("TARGET") && !s.runtime().remove_global("TARGET"), "змінну можна видалити");
}

void t_workplace_busy(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    o.grain = 20;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const char* code = R"PY(
me.pick_up(get("Склад_1"), "grain", 5)
try:
    n = me.grind(get("Будинок_1"))
    notify(f"змолото {n}")
except WorkplaceBusyError as e:
    notify("жорна зайняті: " + e.reason)
    me.wait_until(world.time + minutes(20))
    notify(f"змолото після очікування {me.grind(get('Будинок_1'))}")
)PY";
    for (Character& c : w.characters()) s.runtime().set_code(c.obj, code);
    s.run_until(at(1, 8));
    ctx.check(count_log(w, "змолото 5") == 1, "жорна дісталися одному персонажу");
    ctx.check(count_log(w, "жорна зайняті:") == 1, "другий отримав WorkplaceBusyError");
    ctx.check(find_log(w, "змолото після очікування 5").has_value(), "коли жорна звільнилися, другий теж змолов");
}
