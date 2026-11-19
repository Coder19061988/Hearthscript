// MVP scenarios: seasons, warmth and clothes, heating, the food chain, survival of the first winter.
// Included into tests.cpp (inside its anonymous namespace): uses Ctx, Opts, at(), find_log() etc.

// The reference settlement: shared code + one role line of own code per character (scripts/mvp/shared.py).
bool set_settlement_code(Ctx& ctx, Simulation& s, std::string* shared_out = nullptr) {
    std::string shared;
    if (!load_script(ctx, "scripts/mvp/shared.py", shared)) return false;
    s.runtime().set_shared_code(shared, false);
    const std::map<std::string, std::string> roles = {
        {"Остап", "work = woodcutter\n"},
        {"Богдан", "work = woodcutter\n"},
        {"Марта", "work = farmer\n"},
        {"Ярина", "work = baker\n\ndef evening():\n    if world.season == Season.WINTER:\n        keep_fire(NIGHT_FIREWOOD)\n"},
    };
    for (Character& c : s.world().characters()) {
        auto it = roles.find(c.name);
        s.runtime().set_code(c.obj, it == roles.end() ? "" : it->second);
    }
    if (shared_out) *shared_out = shared;
    return true;
}

// Log entries of one character that contain `needle`.
std::vector<LogEntry> logs_of(World& w, int obj, const std::string& needle) {
    std::vector<LogEntry> out;
    for (const LogEntry& e : w.log_entries())
        if (e.object_id == obj && e.text.find(needle) != std::string::npos) out.push_back(e);
    return out;
}

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
    ctx.check(warm.warmth > 80 && warm.clothes.kind == "warm_clothes", "у теплому одязі тепло тримається");
    Object& house = obj(w, "Будинок_1");
    ctx.check(garment_counts(house.chest)["warm_clothes"] == 1 && garment_counts(house.chest)["clothes"] == 1,
              "одяг поміняли через скриню будинку");
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
assert field.state == "unknown", field.state  # обмежене знання: поля ще не бачив
me.pick_up(storage, "grain", 10)
me.sow(field)
assert field.state == "sown", field.state

def eat():
    if me.hunger > 40:
        me.pick_up(storage, "bread", 1)
        me.eat("bread")

while not field.is_ripe:
    eat()
    me.go_to(field)  # стиглість видно лише на полі
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
assert isinstance(me.tool, Sickle)
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
    Opts o = mvp_opts();
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    if (!set_settlement_code(ctx, s)) return;

    double min_health = 100, min_warmth = 100, min_mood = 100;
    std::string winter_start;
    while (w.now() < at(16, 6)) {
        s.tick(60);
        for (Character& c : w.characters()) {
            min_health = std::min(min_health, c.health);
            min_mood = std::min(min_mood, c.mood);
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
                  "взимку %.0f, мін. настрій %.0f",
                  alive, st.resources["bread"], st.resources["grain"], st.resources["wood"], count_log(w, "дозріло"),
                  min_health, min_warmth, min_mood);
    ctx.info(buf);
    ctx.check(alive == 4, "усі четверо пережили першу зиму");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
    ctx.check(min_health > 50, "здоров'я не падало нижче 50");
    ctx.check(min_warmth > 30, "ніхто не мерз узимку");
    auto outcome = find_log(w, "Першу зиму пережито");
    ctx.check(w.winters_survived() == 1 && outcome && outcome->kind == LogKind::Outcome &&
                  outcome->text.find("Вижило 4 з 4") != std::string::npos && outcome->time == at(16, 0),
              "гра оголосила: першу зиму пережито (о 00:00 першого теплого дня)");
}

// ------------------------------------------------------------------ limited knowledge, notice board

// A character knows an object only as it saw it last, standing at it; at the start — only its house.
void t_knowledge(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    std::string err;
    ctx.check(w.add_blueprint(ObjKind::House, "Будинок_2", {200, 100}, err) >= 0, "креслення: " + err);
    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
house = get("Будинок_1")
assert house.known_at is not None and house.known_ago == minutes(0), house.known_at  # свій будинок — зі старту
assert get("Марта").known_at is not None  # сусідку по будинку теж бачить
assert storage.known_at is None and storage.known_ago is None and storage.amount("bread") == 0
assert get("Поле_1").state == "unknown" and get("Ліс_1").trees_left == 0
site = get("Будинок_2")
assert site.needs == {"wood": 40} and site.known_at is not None  # креслення гравця знають усі
assert storage.is_built  # збудованість — публічна

me.go_to(storage)
assert storage.amount("bread") == 10 and storage.known_ago == minutes(0)
seen = storage.known_at
assert isinstance(seen, Moment) and seen.day == 1 and seen <= world.now
me.go_to(house)
me.wait_until(time(9, 0))  # о 08:00 Марта бере хліб, але звідси цього не видно
assert storage.amount("bread") == 10, "пам'ятає старе"
# Останній погляд — коли пішов від складу (персонаж оглядається на початку й наприкінці кожної дії).
assert storage.known_at - seen < minutes(1) and storage.known_ago >= hours(3), storage.known_ago
me.go_to(storage)
assert storage.amount("bread") == 9
notify("знання оновлено")
assert me.wait_until(lambda: storage.amount("bread") < 9, timeout=hours(3))  # біля складу бачить зміни наживо
notify("бачив, як забрали хліб")
)PY");
    s.runtime().set_code(w.char_obj_id(1), R"PY(
storage = get("Склад_1")
me.wait_until(time(8, 0))
me.pick_up(storage, "bread", 1)
me.eat("bread")
me.wait_until(time(10, 0))
me.pick_up(storage, "bread", 1)
me.eat("bread")
)PY");
    s.run_until(at(1, 12));
    ctx.check(find_log(w, "знання оновлено").has_value(), "знання старіють, доки персонаж не подивиться знову");
    ctx.check(find_log(w, "бачив, як забрали хліб").has_value(), "біля об'єкта персонаж бачить зміни одразу");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// Characters share what they saw through the notice board; a note never overwrites fresher knowledge.
void t_board(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    s.runtime().set_code(w.char_obj_id(0), R"PY(
board = get("Дошка_1")
field = get("Поле_1")
assert board in buildings(Board) and board.is_built
me.go_to(field)
assert field.state == "empty"
assert me.post(board, field) == 1
assert me.post(board, field) == 0  # новішого нічого немає
notify("записав поле")
me.go_to(field)
assert me.read(board) == 0 and field.known_ago < minutes(10)  # свіже знання старим записом не затирається
assert me.post(board) >= 2  # без об'єктів — усе, що знає: поле (новіше), будинок, сусідку
notify("записав усе")
)PY");
    s.runtime().set_code(w.char_obj_id(1), R"PY(
board = get("Дошка_1")
field = get("Поле_1")
me.wait_until(time(8, 0))
assert field.state == "unknown"
assert me.read(board) == 1  # нове для неї лише поле: будинок вона бачить сама, а запис про неї саму не потрібен
assert field.state == "empty" and field.known_ago > hours(1), field.known_ago  # знає те, що бачив Остап
notify("прочитала дошку")
)PY");
    s.run_until(at(1, 10));
    ctx.check(find_log(w, "записав поле").has_value() && find_log(w, "записав усе").has_value(),
              "me.post записує знання на дошку");
    ctx.check(find_log(w, "прочитала дошку").has_value(), "me.read передає знання іншому персонажу");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");

    const std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "знання й записи на дошці зберігаються: " + err);
    if (loaded) {
        Character& marta = loaded->world().character(w.char_obj_id(1));
        ctx.check(marta.known.count(w.find("Поле_1")->id) == 1, "після завантаження Марта пам'ятає поле");
    }
}

// ------------------------------------------------------------------ tools wear out

// An axe wears with work (~40 h), warns when worn, breaks with ToolBrokenError (e.done = units of the
// interrupted action); a broken tool stays broken in the storage. tools(...) lists what the character knows.
void t_tool_wear(Ctx& ctx) {
    Opts o;
    o.axes = 2;
    o.bread = 40;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
forest = get("Ліс_1")
assert tools(Axe) == []  # склад ще не бачив
me.go_to(storage)
axes = tools(Axe, free=True)
assert len(axes) == 2, axes
for a in axes:
    assert a.quality == 1 and a.condition == 100 and not a.is_broken and a.where == storage, a
first = axes[0]
me.take(first)
assert isinstance(me.tool, Axe) and me.tool == first, me.tool
assert len(tools(Axe, free=True)) == 1 and len(tools(Axe)) == 2  # своя в руках теж у списку

def eat():
    if me.hunger > 40:
        me.pick_up(storage, "bread", 1)
        me.eat("bread")

broke = None
while broke is None:
    if not (time(6, 0) <= world.time < time(20, 0)):
        me.sleep(until=time(6, 0))
        continue
    eat()
    try:
        me.chop_wood(forest)  # до повних рук
    except ToolBrokenError as e:
        broke = e.done
        assert 0 <= e.done < 10, e.done
    me.drop(storage)
notify(f"сокира зламалася, в останньому рейсі нарубано {broke}")

assert me.tool.is_broken and me.tool.condition == 0, me.tool
try:
    me.chop_wood(forest)
    assert False, "зламаною не рубають"
except ToolBrokenError:
    pass
try:
    me.take(Axe, storage)
    assert False, "руки зайняті зламаною"
except ActionError:
    pass
me.put_back(storage)
assert len(tools(Axe, free=True)) == 1 and len([a for a in tools(Axe) if a.is_broken]) == 1
me.take(first)  # зламану теж можна взяти — щоб розібрати
assert me.tool == first and me.tool.is_broken, me.tool
assert me.dismantle() == 2 and me.tool is None
assert me.carrying.get("scrap") == 2 and me.carrying.get("wood") == 1, me.carrying
me.drop(storage)
assert [a for a in tools(Axe) if a.is_broken] == []  # розібрана зникла
me.take(Axe, storage)  # найкраща ціла
assert me.tool.condition == 100 and me.tool != first
me.put_back(storage)
try:
    me.dismantle()
    assert False, "у руках нічого немає"
except NoItemError:
    pass
notify("знос ok")
)PY");
    s.run_until(at(7, 12), 120);
    auto broke = find_log(w, "сокира зламалася");
    if (broke) ctx.info(broke->text + " — " + w.format_time(broke->time));
    ctx.check(find_log(w, "майже зношений").has_value(), "журнал попереджає, що сокира майже зношена");
    ctx.check(find_log(w, "зламався інструмент").has_value(), "журнал записує поломку");
    ctx.check(find_log(w, "знос ok").has_value(), "зламаний інструмент: ToolBrokenError, розбирання, take іншого");
    ctx.check(obj(w, "Склад_1").resources["scrap"] == 2, "брухт розібраної сокири — на складі");
    const int wood = obj(w, "Склад_1").resources["wood"];
    ctx.info("нарубано дров однією сокирою: " + std::to_string(wood));
    ctx.check(wood >= 100 && wood <= 140, "сокира живе ~40 год рубки (~120 дров)");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");

    const std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "стан інструментів зберігається: " + err);
    if (loaded) {
        const Object& st = *loaded->world().object(w.find("Склад_1")->id);
        ctx.check(st.tools.size() == 1 && !st.tools[0].broken(), "після завантаження на складі — ціла сокира");
    }
}

// ------------------------------------------------------------------ animals

void t_animals(Ctx& ctx) {
    {
        // Warm season: animals graze; hay, milk, eggs, meat. Animals live only in a barn.
        Opts o;
        o.scythes = 1;
        o.bread = 20;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        std::string err;
        const int barn = w.add_barn("Хлів_1", {-60, 60});
        for (const char* species : {"cow", "chicken", "chicken"})
            ctx.check(w.add_animal(species, barn, err) >= 0, std::string("тварина в хліві: ") + err);
        ctx.check(w.add_animal("cow", w.find("Склад_1")->id, err) < 0, "тварина живе лише в хліві: " + err);
        s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
meadow = get("Луг_1")
barn = get("Хлів_1")
cow = get("Корова_1")
assert isinstance(cow, Cow) and cow in animals(Cow) and len(animals(Chicken)) == 2 and len(animals()) == 3
assert cow.home == barn and len(barn.animals) == 3 and barn.free_places == 5, barn.animals
assert cow.known_at is None and cow.milk == 0  # у хліві ще не був
me.go_to(barn)
assert cow.known_at is not None and cow.is_alive and cow.hunger == 0 and cow.health == 100
try:
    me.milk(animals(Chicken)[0])
    assert False, "курку не доять"
except ActionError:
    pass

me.take(Scythe, storage)
assert me.mow(meadow, amount=10) == 10 and meadow.hay_left == 50, meadow.hay_left
me.drop(storage)
me.put_back(storage)
me.pick_up(storage, "bread", 1)
me.eat("bread")
me.sleep(until=time(6, 0))

me.go_to(barn)
milk = me.milk(cow)
assert 3 <= milk <= 5, milk  # 4 на добу
assert me.milk(cow) == 0  # уже видоєно
eggs = 0
for c in animals(Chicken):
    eggs += me.collect_eggs(c)
assert eggs == 2, eggs  # по яйцю на добу
hunger = me.hunger
me.eat("milk")
me.eat("eggs")
assert me.hunger < hunger
chicken = animals(Chicken)[0]
assert me.slaughter(chicken) == 2 and me.carrying.get("meat") == 2, me.carrying
assert chicken not in animals() and len(barn.animals) == 2
me.eat("meat")
notify("тварини ok")
)PY");
        s.run_until(at(2, 12));
        ctx.check(find_log(w, "тварини ok").has_value(), "косіння, молоко, яйця й забій працюють");
        ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
        ctx.check(obj(w, "Склад_1").resources["hay"] == 10, "накошене сіно — на складі");
    }
    {
        // Winter: a fed cow is fine; a starving one dies of hunger.
        Opts o;
        o.start_day = 11;  // зима
        o.start_hour = 6;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        std::string err;
        const int barn = w.add_barn("Хлів_1", {-60, 60});
        const int fed = w.add_animal("cow", barn, err);
        const int starving = w.add_animal("cow", barn, err);
        w.object(starving)->hunger = 95;
        w.object(starving)->health = 5;
        obj(w, "Склад_1").resources["hay"] = 20;
        s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
cow = get("Корова_1")
me.wait_until(time(18, 0))
me.pick_up(storage, "hay", 6)
me.go_to(cow)
assert 25 <= cow.hunger <= 35, cow.hunger  # 12 год зими: +2,5 за годину
assert me.feed(cow) == 3 and cow.hunger < 1, cow.hunger  # з'їла стільки, скільки треба
assert me.carrying == {"hay": 3}, me.carrying
assert me.feed(cow) == 0  # уже не голодна
dead = get("Корова_2")
assert not dead.is_alive
try:
    me.feed(dead)
    assert False, "мертву не годують"
except ActionError:
    pass
notify("нагодовано")
)PY");
        s.run_until(at(12, 6));
        ctx.check(find_log(w, "нагодовано").has_value(), "me.feed годує сіном із рук");
        ctx.check(find_log(w, "Корова_2» голодує").has_value(), "журнал попереджає, що тварина голодує");
        auto death = find_log(w, "Смерть тварини «Корова_2»: голод");
        if (death) ctx.info(death->text + " — " + w.format_time(death->time));
        ctx.check(death.has_value(), "некормлена тварина гине від голоду");
        const Object& kept = *w.object(fed);
        ctx.info("Корова_1: здоров'я " + std::to_string(int(kept.health)) + ", голод " + std::to_string(int(kept.hunger)));
        ctx.check(kept.alive && kept.health > 99 && kept.hunger > 25 && kept.hunger < 35, "нагодована тварина здорова");
        ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");

        const std::string text = save_game(s);
        auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
        ctx.check(loaded && save_game(*loaded) == text, "стан тварин зберігається: " + err);
    }
}

// ------------------------------------------------------------------ trade

void t_trade(Ctx& ctx) {
    {
        // Goods for sale on the post, orders; the merchant trades when he leaves at 20:00.
        Opts o;
        o.wood = 30;
        o.bread = 20;
        o.start_day = 5;  // the merchant comes today at 08:00
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        w.add_trading_post("Торговий_пост_1", {-80, -80});
        w.add_barn("Хлів_1", {-60, 60});
        obj(w, "Управа_1").coins = 200;
        const Merchant visit = w.merchant();  // prices of today's visit
        s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
post = get("Торговий_пост_1")
hall = get("Управа_1")
assert isinstance(post, TradingPost) and isinstance(hall, TownHall)
assert merchant.days_until == 0 and not merchant.is_here  # сьогодні о 08:00
assert merchant.price("grain") is not None and merchant.price(Cow) is not None and merchant.price("wood") is None
assert merchant.offer("wood") >= 1

@on(Event.MERCHANT_ARRIVED)
def arrived(me):
    notify("купець приїхав")

me.pick_up(storage, "wood", 10)
me.drop(post)
assert post.for_sale == {"wood": 10}, post.for_sale
for item, n in (("grain", 5), (Chicken, 1), (Axe, 1), (WarmClothes, 1), (Cow, 1)):
    me.order(post, item, n)
assert post.orders == [("grain", 5), ("chicken", 1), ("axe", 1), ("warm_clothes", 1), ("cow", 1)], post.orders
try:
    me.order(post, "wood")
    assert False, "дров купець не продає"
except ActionError:
    pass
me.wait_until(time(20, 30))

me.go_to(post)
assert post.for_sale == {} and post.orders == [], (post.for_sale, post.orders)
assert post.amount("grain") == 5 and post.has_tool(Axe) and post.amount("warm_clothes") == 1
me.pick_up(post, "grain", 5)
me.drop(storage)
me.take(Axe, post)
me.put_back(storage)
me.pick_up(post, "warm_clothes", 1)
me.drop(get("Будинок_1"))  # одяг — у скриню
assert get("Будинок_1").has(WarmClothes)
assert len(animals(Chicken)) == 1 and animals(Chicken)[0].home == get("Хлів_1")
assert animals(Cow) == []  # на корову не вистачило монет
me.go_to(hall)
notify(f"в управі монет: {hall.coins}")
)PY");
        s.run_until(at(5, 23));
        ctx.check(find_log(w, "купець приїхав").has_value(), "@on(Event.MERCHANT_ARRIVED) спрацьовує");
        ctx.check(find_log(w, "не вистачило").has_value(), "замовлення, на яке бракує монет, скасовано із записом");
        ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
        const int expected = 200 + 10 * visit.offers.at("wood") -
                             (5 * visit.prices.at("grain") + visit.prices.at("chicken") + visit.prices.at("axe") +
                              visit.prices.at("warm_clothes"));
        auto coins = find_log(w, "в управі монет:");
        if (coins) ctx.info(coins->text + ", очікувано " + std::to_string(expected));
        ctx.check(coins && coins->text == "в управі монет: " + std::to_string(expected),
                  "управа отримала виручку й заплатила за замовлення");
        ctx.check(obj(w, "Склад_1").resources["grain"] == 5, "куплене зерно перенесено на склад");
        ctx.check(w.merchant().next_arrival > at(10, 0) && w.merchant().next_arrival < at(10, 12),
                  "наступний приїзд — за 5 днів");

        const std::string text = save_game(s);
        std::string err;
        auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
        ctx.check(loaded && save_game(*loaded) == text, "торгівля зберігається: " + err);
    }
    {
        Opts o;
        o.start_day = 5;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        s.run_until(at(5, 12));
        ctx.check(find_log(w, "проїхав повз").has_value(), "без торгового поста купець проїжджає повз");
    }
}

// ------------------------------------------------------------------ statistics for the town hall charts

void t_stats(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    o.bread = 30;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    std::string woodcutter;
    if (!load_script(ctx, "scripts/woodcutter.py", woodcutter)) return;
    s.runtime().set_code(w.char_obj_id(0), woodcutter);
    s.run_until(at(3, 5, 30));  // two days from the start at 05:30
    const auto& st = w.stats();
    ctx.info("знімків: " + std::to_string(st.size()));
    ctx.check(st.size() == 49, "знімок щогодини: за дві доби — 48 і стартовий");
    if (st.size() < 2) return;
    ctx.check(st[0].time == at(1, 5, 30) && st[1].time == at(1, 6) && st.back().time == at(3, 5),
              "стартовий знімок, далі — на початку кожної години");
    ctx.info("дров: " + std::to_string(st.front().stock.at("wood")) + " → " + std::to_string(st.back().stock.at("wood")));
    ctx.check(st.back().stock.at("wood") > st.front().stock.at("wood"), "запас дров на складах росте");
    ctx.check(st.back().stock.at("bread") < st.front().stock.at("bread"), "хліб з'їдають");
    ctx.check(st.back().people == 2 && st.back().coins == 50 && st.back().animals == 0, "люди, монети, тварини");
    ctx.check(st.back().health > 0 && st.back().health <= 100 && st.back().hunger >= 0, "середні показники людей");

    const std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && loaded->world().stats().size() == st.size() && save_game(*loaded) == text,
              "історія зберігається: " + err);
}

// ------------------------------------------------------------------ workshops and recipes

void t_craft(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    o.bread = 20;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    w.add_workshop("smithy", "Кузня_1", {-60, -60});
    obj(w, "Склад_1").resources["scrap"] = 5;
    obj(w, "Склад_1").resources["wood"] = 10;
    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
smithy = get("Кузня_1")
assert isinstance(smithy, Smithy) and smithy in buildings(Smithy) and smithy in buildings(Workshop)
try:
    me.craft(Axe, smithy)
    assert False, "у руках немає матеріалів"
except NoItemError:
    pass
try:
    me.craft(Axe, get("Будинок_1"))
    assert False, "сокиру кують у кузні"
except ActionError:
    pass
me.pick_up(storage, "scrap", 3)
me.pick_up(storage, "wood", 3)
start = world.now
assert me.craft(Axe, smithy) == 1
took = world.now - start
assert hours(2) <= took < hours(2) + minutes(30), took
assert isinstance(me.tool, Axe) and me.tool.condition == 100 and me.tool.quality == 1, me.tool
assert me.carrying == {"scrap": 1, "wood": 1}, me.carrying
try:
    me.craft(Sickle, smithy)
    assert False, "руки зайняті сокирою"
except ActionError:
    pass
me.put_back(storage)
assert me.craft(Sickle, smithy) == 1 and isinstance(me.tool, Sickle)
me.put_back(storage)
notify("кузня ok")
)PY");
    s.runtime().set_code(w.char_obj_id(1), R"PY(
storage = get("Склад_1")
me.pick_up(storage, "scrap", 2)
me.pick_up(storage, "wood", 2)
me.wait_until(time(7, 0))
try:
    me.craft(Scythe, get("Кузня_1"))
    assert False, "горн зайнятий"
except WorkplaceBusyError:
    notify("горн зайнятий")
)PY");
    s.run_until(at(1, 12));
    ctx.check(find_log(w, "кузня ok").has_value(), "у кузні з брухту й дров виходять сокира й серп");
    ctx.check(find_log(w, "горн зайнятий").has_value(), "горн — одне робоче місце (WorkplaceBusyError)");
    const Object& st = obj(w, "Склад_1");
    ctx.check(tools_of(st, "axe") == 2 && tools_of(st, "sickle") == 1, "нові інструменти — на складі");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// The clothes chain: flax on the field → cloth and clothes in the tailor's shop → the house chest;
// clothes wear while worn and turn into rags.
void t_clothes(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    o.sickles = 1;
    o.flax = 20;
    o.bread = 20;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const Balance& b = w.balance();
    w.add_workshop("tailor", "Кравецька_1", {-60, -60});
    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
field = get("Поле_1")
me.pick_up(storage, "flax", 10)
try:
    me.sow(field, crop="oats")
    assert False, "такої культури немає"
except ActionError:
    pass
me.sow(field, crop="flax")
assert field.state == "sown" and field.crop == "flax", (field.state, field.crop)
assert "flax" not in me.carrying, me.carrying
notify("льон посіяно")
)PY");
    s.run_until(at(1, 12));
    ctx.check(find_log(w, "льон посіяно").has_value(), "льон сіють, як зерно: me.sow(поле, crop=\"flax\")");
    obj(w, "Поле_1").growth = 0.999;  // growing itself is the grain field's test
    s.run_until(at(1, 13));
    const Object& field = obj(w, "Поле_1");
    ctx.check(field.field_state == FieldState::Ripe && field.crop == "flax" &&
                  field.grain_left == static_cast<int>(b.flax_yield),
              "дозріле поле льону дає flax_yield льону");

    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
field = get("Поле_1")
tailor = get("Кравецька_1")
assert isinstance(tailor, Tailor) and tailor in buildings(Workshop)
me.take(Sickle, storage)
assert me.harvest(field, amount=10) == 10
assert me.carrying == {"flax": 10}, me.carrying
me.put_back(storage)
try:
    me.craft(Clothes, tailor)
    assert False, "без тканини"
except NoItemError:
    pass
assert me.craft("cloth", tailor, amount=5) == 5
assert me.carrying == {"cloth": 5}, me.carrying
assert me.craft(Clothes, tailor) == 1
assert me.carrying == {"cloth": 2, "clothes": 1}, me.carrying
me.drop(me.home)
assert me.carrying == {"cloth": 2}, me.carrying
me.drop(storage)
notify("кравецька ok")
)PY");
    s.run_until(at(2, 2));
    ctx.check(find_log(w, "кравецька ok").has_value(), "льон → тканина → одяг у кравецькій, одяг — у скриню");
    ctx.check(obj(w, "Склад_1").resources["cloth"] == 2, "решта тканини — на складі");

    Character& c = w.character(w.char_obj_id(1));
    c.clothes.condition = b.clothes_worn_warning + 0.05;
    s.run_until(at(2, 3));
    ctx.check(find_log(w, "майже зношений").has_value(), "журнал попереджає, що одяг майже зношений");
    c.clothes.condition = 0.1;
    s.run_until(at(2, 4));
    ctx.check(c.clothes.empty() && c.carrying["rags"] == 1 && find_log(w, "ганчір'я").has_value(),
              "зношений одяг стає ганчір'ям у руках, персонаж без одягу");

    obj(w, "Будинок_1").chest.push_back(Garment{"clothes", 50});
    s.runtime().set_code(w.char_obj_id(1), R"PY(
assert me.clothes is None and me.clothes_condition is None
me.wear(Clothes)
assert me.clothes == Clothes and me.clothes_condition > 99, me.clothes_condition  # новий комплект
me.wear(WarmClothes)
assert me.clothes == WarmClothes
notify("переодягнено")
)PY");
    s.run_until(at(2, 5));
    ctx.check(find_log(w, "переодягнено").has_value(), "зі скрині береться комплект у найкращому стані");
    int fresh = 0, worn = 0;
    for (const Garment& g : obj(w, "Будинок_1").chest) {
        fresh += g.kind == "clothes" && g.condition > 99 ? 1 : 0;
        worn += g.kind == "clothes" && g.condition == 50 ? 1 : 0;
    }
    ctx.check(fresh == 1 && worn == 1, "знятий одяг повертається в скриню зі своїм станом");

    std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "стан одягу й культура поля зберігаються: " + err);
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// The wood chain: wood → planks at the sawmill → (planks are a building material) → a bed at the carpentry →
// the house; a bed makes sleep restore faster.
void t_wood(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    o.bread = 20;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const Balance& b = w.balance();
    w.add_workshop("sawmill", "Лісопилка_1", {-60, -60});
    w.add_workshop("carpentry", "Столярня_1", {-60, 60});
    std::string err;
    ctx.check(w.add_blueprint(ObjKind::Workshop, "Столярня_2", {-120, 0}, err, "carpentry") >= 0, "креслення столярні: " + err);
    obj(w, "Склад_1").resources["wood"] = 10;
    obj(w, "Склад_1").resources["hay"] = 3;
    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
sawmill = get("Лісопилка_1")
carpentry = get("Столярня_1")
assert isinstance(sawmill, Sawmill) and isinstance(carpentry, Carpentry)
me.pick_up(storage, "wood", 6)
n = me.craft("planks", sawmill)
assert n == 6, (n, me.carrying)
assert me.carrying == {"planks": 6}, me.carrying
assert me.deliver(get("Столярня_2")) == 6  # дошки — матеріал будови
assert get("Столярня_2").needs == {"wood": 10, "planks": 14}, get("Столярня_2").needs
me.pick_up(storage, "wood", 4)
assert me.craft("planks", sawmill, amount=4) == 4
me.pick_up(storage, "hay", 3)
assert me.craft("bed", carpentry) == 1
assert me.carrying == {"bed": 1}, me.carrying
house = me.home
assert house.beds == 0
me.drop(house)
assert house.beds == 1 and me.carrying == {}, (house.beds, me.carrying)
notify("ліжко в будинку")
)PY");
    s.run_until(at(1, 14));
    ctx.check(find_log(w, "ліжко в будинку").has_value(), "дрова → дошки → ліжко → будинок");
    ctx.check(obj(w, "Будинок_1").beds == 1, "у будинку одне ліжко");

    // Both sleep from the same fatigue: the first resident has the bed.
    Character& in_bed = w.character(w.char_obj_id(0));
    Character& on_floor = w.character(w.char_obj_id(1));
    ctx.check(w.has_bed(in_bed) && !w.has_bed(on_floor), "ліжко дістається першому мешканцеві");
    for (int i = 0; i < 2; ++i) {
        w.character(w.char_obj_id(i)).fatigue = 80;
        s.runtime().set_code(w.char_obj_id(i), "me.sleep(hours=2)\n");
    }
    s.run_until(at(1, 17));
    const double gain = (b.fatigue_sleep_home_per_hour - b.fatigue_sleep_bed_per_hour) * 2;
    ctx.check(std::abs(on_floor.fatigue - in_bed.fatigue - gain) < 1.5,
              "у ліжку сон знімає втому швидше: " + std::to_string(in_bed.fatigue) + " проти " + std::to_string(on_floor.fatigue));

    std::string text = save_game(s);
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "ліжка зберігаються: " + err);
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// Stone and metal: a pickaxe mines stone in the quarry and ore in the mine; the kiln burns wood into charcoal;
// the smithy smelts iron from ore and charcoal and forges tools from iron (the second variant of their recipes).
void t_mining(Ctx& ctx) {
    Opts o;
    o.pickaxes = 1;
    o.bread = 20;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const Balance& b = w.balance();
    w.add_workshop("kiln", "Вуглярня_1", {-80, -20});
    w.add_workshop("smithy", "Кузня_1", {-60, -60});
    obj(w, "Склад_1").resources["wood"] = 8;
    s.runtime().set_code(w.char_obj_id(0), "QUARRY = " + std::to_string(static_cast<int>(b.quarry_stone)) + R"PY(
storage = get("Склад_1")
quarry = get("Кар'єр_1")
ore_pit = get("Рудня_1")
assert isinstance(quarry, Deposit) and quarry.resource == "stone" and ore_pit.resource == "ore"
assert quarry.left == 0  # ще не бачив
try:
    me.mine(quarry)
    assert False, "без кайла"
except NoItemError:
    pass
me.take(Pickaxe, storage)
assert me.mine(quarry, amount=3) == 3 and me.carrying == {"stone": 3}, me.carrying
assert quarry.left == QUARRY - 3, quarry.left
me.drop(storage)
assert me.mine(ore_pit) == 6  # до повних рук: 6 × 8 кг
me.put_back(storage)
me.drop(storage)
kiln = get("Вуглярня_1")
me.pick_up(storage, "wood", 6)
assert me.craft("charcoal", kiln) == 2 and me.carrying == {"charcoal": 2}, me.carrying
me.pick_up(storage, "ore", 4)
smithy = get("Кузня_1")
assert me.craft("iron", smithy) == 2 and me.carrying == {"iron": 2}, me.carrying
me.pick_up(storage, "wood", 2)
assert me.craft(Axe, smithy) == 1 and isinstance(me.tool, Axe) and me.carrying == {}, me.carrying
me.put_back(storage)
notify("залізна сокира")
)PY");
    s.run_until(at(2, 2));
    ctx.check(find_log(w, "залізна сокира").has_value(), "кайло → камінь і руда; дрова → вугілля; руда + вугілля → залізо → сокира");
    const Object& st = obj(w, "Склад_1");
    ctx.check(st.resources.at("stone") == 3 && st.resources.at("ore") == 2 && tools_of(st, "axe") == 2,
              "на складі камінь, решта руди й нова сокира");
    ctx.check(obj(w, "Рудня_1").deposit_left == static_cast<int>(b.mine_ore) - 6, "рудня вичерпується");

    std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "родовища й кайло зберігаються: " + err);
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// Water: buckets from the well (a recipe without inputs, until the hands are full); a watered field grows
// field_water_growth_bonus faster for a day; rain waters sown fields too.
void t_water(Ctx& ctx) {
    Opts o;
    o.bread = 20;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const Balance& b = w.balance();
    w.add_workshop("well", "Колодязь_1", {60, -60});
    w.add_field("Поле_2", {200, -120});
    for (const char* name : {"Поле_1", "Поле_2"}) {
        Object& f = obj(w, name);
        f.field_state = FieldState::Sown;
        f.crop = "grain";
    }
    s.runtime().set_code(w.char_obj_id(0), R"PY(
well = get("Колодязь_1")
field = get("Поле_1")
assert isinstance(well, Well)
try:
    me.water(field)
    assert False, "без води"
except NoItemError:
    pass
assert me.draw_water(well) == 5 and me.carrying == {"water": 5}, me.carrying
me.water(field)
assert field.is_watered and me.carrying == {}, (field.is_watered, me.carrying)
assert not get("Поле_2").is_watered  # не бачив
notify("полито")
)PY");
    s.run_until(at(1, 10));
    ctx.check(find_log(w, "полито").has_value(), "вода з колодязя (до повних рук) — на поле");
    const double g1 = obj(w, "Поле_1").growth, g2 = obj(w, "Поле_2").growth;
    s.run_until(at(1, 20));
    const double d1 = obj(w, "Поле_1").growth - g1, d2 = obj(w, "Поле_2").growth - g2;
    ctx.check(d2 > 0 && std::abs(d1 / d2 - (1.0 + b.field_water_growth_bonus)) < 0.01,
              "полите поле росте швидше: " + std::to_string(d1) + " проти " + std::to_string(d2));

    w.add_rain(at(2, 6), at(2, 7));
    s.run_until(at(2, 8));
    ctx.check(obj(w, "Поле_2").watered_until >= at(2, 7) + b.field_rain_soak_hours * 3600.0 - 60,
              "дощ поливає посіяне поле ще на field_rain_soak_hours після себе");

    std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "полив зберігається: " + err);
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// Who falls ill and who gets hurt. The chances are per hour, so the test raises them to certainty.
void t_ailments(Ctx& ctx) {
    Balance b = default_balance();
    b.illness_cold_per_hour = 100;
    b.injury_dark_per_hour = 100;
    Opts o;
    o.chars = 3;
    o.start_hour = 22;  // night
    Simulation& s = ctx.sim(o, &b);
    World& w = s.world();
    Character& worker = w.character(w.char_obj_id(0));
    Character& frozen = w.character(w.char_obj_id(1));
    Character& sleeper = w.character(w.char_obj_id(2));
    frozen.warmth = 10;
    s.runtime().set_code(w.char_obj_id(0), R"PY(
me.take(Axe, get("Склад_1"))
assert me.chop_wood(get("Ліс_1"), amount=2) == 2
assert me.is_injured and me.needs_treatment and not me.is_sick
notify("травма вночі")
)PY");
    s.runtime().set_code(w.char_obj_id(2), "me.sleep(until=time(6, 0))\n");
    s.run_until(at(2, 1));
    ctx.check(find_log(w, "травма вночі").has_value() && worker.injury_left > 0 &&
                  find_log(w, "травма на роботі").has_value(),
              "важка робота вночі — травма");
    ctx.check(frozen.illness_left > 0 && find_log(w, "хвороба. Лежати").has_value(), "змерзлий хворіє");
    ctx.check(frozen.illness_left > b.illness_hours - 4 && frozen.health < 100, "хвороба триває й шкодить здоров'ю");
    ctx.check(sleeper.illness_left == 0 && sleeper.injury_left == 0, "хто спить у теплі — здоровий");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");

    // The reference settlement: the sick one lies at home until the illness passes, and survives it.
    Simulation& ms = ctx.sim(mvp_opts());
    World& mw = ms.world();
    if (!set_settlement_code(ctx, ms)) return;
    Character& ill = mw.character(mw.char_obj_id(0));
    ill.illness_left = 30;
    ms.run_until(at(2, 18));
    ctx.check(find_log(mw, "лежу вдома").has_value(), "еталонне поселення: хворий лягає вдома");
    ctx.check(ill.alive && ill.illness_left == 0 && ill.health > 60 && count_kind(mw, LogKind::Error) == 0,
              "хвороба минула, здоров'я " + std::to_string(ill.health));
}

// Treatment: herbs from the meadow and a bandage from rags (the tailor's), the hospital, me.treat;
// an injury halves the work speed; there are no herbs in winter.
void t_treatment(Ctx& ctx) {
    Opts o;
    o.chars = 3;
    o.bread = 20;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const Balance& b = w.balance();
    w.add_workshop("hospital", "Лікарня_1", {40, 60});
    w.add_workshop("tailor", "Кравецька_1", {-60, -60});
    obj(w, "Склад_1").resources["rags"] = 2;
    Character& patient = w.character(w.char_obj_id(1));
    patient.illness_left = b.illness_hours;
    patient.injury_left = b.injury_hours;
    s.runtime().set_code(w.char_obj_id(1), R"PY(
assert me.is_sick and me.is_injured and me.needs_treatment
hospital = get("Лікарня_1")
assert isinstance(hospital, Hospital)
me.go_to(hospital)
me.rest(hours=8)
)PY");
    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
hospital = get("Лікарня_1")
patient = get("Марта")
try:
    me.treat(patient)
    assert False, "без трав і бинта"
except NoItemError:
    pass
me.pick_up(storage, "rags", 2)
assert me.craft("bandage", get("Кравецька_1")) == 1
assert me.gather_herbs(get("Луг_1"), amount=2) == 2
assert me.carrying == {"bandage": 1, "herbs": 2}, me.carrying
assert hospital.patients == []  # видно лише з лікарні
me.go_to(hospital)
assert hospital.patients == [patient] and patient.needs_treatment, hospital.patients
me.treat(patient)
assert me.carrying == {} and not patient.needs_treatment and patient.is_sick, me.carrying
try:
    me.treat(patient)
    assert False, "уже лікували"
except ActionError:
    pass
notify("вилікувано")
)PY");
    w.character(w.char_obj_id(2)).injury_left = b.injury_hours;
    s.runtime().set_code(w.char_obj_id(2), "CHOP = " + std::to_string(static_cast<int>(b.chop_minutes_per_unit)) + R"PY(
forest = get("Ліс_1")
me.take(Axe, get("Склад_1"))
me.go_to(forest)
start = world.now
me.chop_wood(forest, amount=2)
took = world.now - start
assert took >= minutes(4 * CHOP - 1), took
notify("травмований рубає вдвічі повільніше")
)PY");
    s.run_until(at(1, 12));
    ctx.check(find_log(w, "вилікувано").has_value(), "трави з луки й бинт із ганчір'я — лікування в лікарні");
    ctx.check(patient.illness_treated && patient.illness_left <= b.illness_treated_hours && patient.injury_treated &&
                  patient.injury_left <= b.injury_treated_hours,
              "лікування скорочує хворобу й травму");
    ctx.check(find_log(w, "вдвічі повільніше").has_value(), "травма сповільнює роботу");

    std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "хвороби, травми й лікування зберігаються: " + err);

    s.run_until(at(2, 2));
    ctx.check(patient.illness_left == 0 && find_log(w, "хвороба минула").has_value(),
              "вилікувана хвороба минає за illness_treated_hours");
    ctx.check(patient.injury_left > 0, "травма гоїться довше");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");

    Opts ow;
    ow.start_day = 12;  // winter
    Simulation& sw = ctx.sim(ow);
    sw.runtime().set_code(sw.world().char_obj_id(0), R"PY(
try:
    me.gather_herbs(get("Луг_1"))
    assert False, "узимку трав немає"
except ActionError:
    notify("узимку трав немає")
)PY");
    sw.run_until(at(12, 7));
    ctx.check(find_log(sw.world(), "узимку трав немає").has_value(), "трави збирають лише в теплу пору");
}

// Mood: heavy work wears it down, free time lifts it; a low mood slows the work and is logged.
void t_mood(Ctx& ctx) {
    Opts o;
    o.chars = 3;
    o.axes = 2;
    o.bread = 20;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const Balance& b = w.balance();
    Character& worker = w.character(w.char_obj_id(0));
    Character& rester = w.character(w.char_obj_id(1));
    Character& gloomy = w.character(w.char_obj_id(2));
    rester.mood = 50;
    gloomy.mood = b.mood_low_threshold - 10;
    s.runtime().set_code(w.char_obj_id(0), R"PY(
me.take(Axe, get("Склад_1"))
forest = get("Ліс_1")
me.go_to(forest)
start = me.mood
me.chop_wood(forest, amount=8)
notify(f"настрій після рубки {start - me.mood:.1f}")
)PY");
    s.runtime().set_code(w.char_obj_id(1), "me.rest(hours=4)\nassert me.mood > 60, me.mood\nnotify('відпочинок')\n");
    s.runtime().set_code(w.char_obj_id(2), "CHOP = " + std::to_string(static_cast<int>(b.chop_minutes_per_unit)) + R"PY(
assert me.mood < 30
forest = get("Ліс_1")
me.take(Axe, get("Склад_1"))
me.go_to(forest)
start = world.now
me.chop_wood(forest, amount=3)
took = world.now - start
assert took >= minutes(3 * CHOP / 0.75 - 1), took
notify("без настрою праця повільніша")
)PY");
    const double before = worker.mood;
    s.run_until(at(1, 12));
    ctx.check(find_log(w, "настрій після рубки").has_value() && worker.mood < before,
              "важка робота знижує настрій: " + std::to_string(before) + " → " + std::to_string(worker.mood));
    ctx.check(std::abs(rester.mood - (50 + 4 * b.mood_rest_per_hour)) < 0.5 && find_log(w, "відпочинок").has_value(),
              "вільний час піднімає настрій: " + std::to_string(rester.mood));
    ctx.check(find_log(w, "без настрою праця повільніша").has_value(), "поганий настрій сповільнює роботу");

    Opts g;
    Simulation& gs = ctx.sim(g);
    gs.world().character(gs.world().char_obj_id(0)).mood = b.mood_low_threshold + 0.01;
    gs.runtime().set_code(gs.world().char_obj_id(0), "me.take(Axe, get(\"Склад_1\"))\nme.chop_wood(get(\"Ліс_1\"), hours=1)\n");
    gs.run_until(at(1, 9));
    ctx.check(find_log(gs.world(), "поганий настрій").has_value(), "журнал попереджає про поганий настрій");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// The church: a service lifts the mood of everyone there (once in service_interval_hours); the dead are buried
// at its cemetery; an unburied body decays after body_decay_hours and hurts everyone's mood.
void t_church(Ctx& ctx) {
    Balance b = default_balance();
    b.illness_body_per_hour = 0;  // the measured mood must not depend on a chance illness
    Opts o;
    o.chars = 4;
    o.bread = 20;
    Simulation& s = ctx.sim(o, &b);
    World& w = s.world();
    Character& dead = w.character(w.char_obj_id(3));
    dead.health = 0.01;
    dead.hunger = 100;
    s.runtime().set_code(w.char_obj_id(0), R"PY(
me.wait_until(lambda: bodies(), timeout=hours(1))
body = get("Ярина")
assert not body.is_alive and bodies() == [body], bodies()
try:
    me.bury(body)
    assert False, "церкви ще немає"
except ActionError:
    notify("без церкви не ховають")
)PY");
    s.run_until(at(1, 7));
    ctx.check(!dead.alive && find_log(w, "без церкви не ховають").has_value(), "без церкви не ховають");

    // Nobody buries it for a day and more: the body decays and everyone's mood falls.
    s.run_until(at(2, 5, 30));
    for (int i = 0; i < 3; ++i) {  // fed and rested: only the body changes the mood of the idle one
        Character& c = w.character(w.char_obj_id(i));
        c.hunger = c.fatigue = 0;
        c.mood = 50;
    }
    Character& idle = w.character(w.char_obj_id(2));
    const double idle_mood = idle.mood;
    s.run_until(at(2, 8, 30));
    ctx.check(std::abs(idle.mood - (idle_mood + 3 * b.mood_body_per_hour)) < 0.5,
              "непоховане тіло після доби псує настрій усім: " + std::to_string(idle_mood) + " → " + std::to_string(idle.mood));

    w.add_workshop("church", "Церква_1", {-40, 40});
    s.runtime().set_code(w.char_obj_id(0), R"PY(
church = get("Церква_1")
assert isinstance(church, Church)
for body in bodies():
    me.bury(body)
assert bodies() == [] and church.graves == 1, church.graves
me.hold_service(church)
try:
    me.hold_service(church)
    assert False, "друга служба того ж дня"
except ActionError:
    pass
notify("поховано, служба")
)PY");
    s.runtime().set_code(w.char_obj_id(1), "me.go_to(get(\"Церква_1\"))\nme.rest(hours=6)\n");
    const double rest_start = w.character(w.char_obj_id(1)).mood;
    s.run_until(at(2, 16));
    ctx.check(find_log(w, "поховано, служба").has_value() && dead.buried && obj(w, "Церква_1").graves == 1,
              "тіло поховано на кладовищі при церкві");
    ctx.check(find_log(w, "присутніх 2").has_value(), "служба для тих, хто в церкві");
    ctx.check(w.character(w.char_obj_id(1)).mood > rest_start + b.service_mood - 0.5, "служба піднімає настрій");
    ctx.check(!find_log(w, "присутніх 3"), "хто не в церкві, служби не має");

    std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "настрій, поховання й служби зберігаються: " + err);
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// Light: a lit lantern or a torch keeps heavy work at night safe; without light it injures (the test makes it
// certain). Lanterns are filled with oil and lit every evening, go out at dawn; oil and torches come from the oil press.
void t_lighting(Ctx& ctx) {
    Balance b = default_balance();
    b.injury_dark_per_hour = 100;
    Opts o;
    o.chars = 3;
    o.axes = 3;
    o.start_hour = 20.5;
    Simulation& s = ctx.sim(o, &b);
    World& w = s.world();
    w.add_lantern("Ліхтар_1", {10, 290});  // by the forest
    w.add_forest("Ліс_2", {300, 300}, 50);  // far from any light
    obj(w, "Склад_1").resources["oil"] = 3;  // 12 hours of light: the lantern outlasts the night
    obj(w, "Склад_1").resources["torch"] = 1;
    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
lantern = get("Ліхтар_1")
me.take(Axe, storage)
me.pick_up(storage, "oil", 3)
me.light(lantern)             # ще день: лише долито
assert lantern.fuel == 3 and not lantern.is_lit
me.wait_until(time(21, 5))
me.light(lantern)
assert lantern.is_lit and me.carrying == {}
assert not me.is_in_dark
me.chop_wood(get("Ліс_1"), amount=2)
assert not me.is_injured
notify("при ліхтарі без травм")
)PY");
    s.runtime().set_code(w.char_obj_id(1), R"PY(
storage = get("Склад_1")
me.take(Axe, storage)
me.pick_up(storage, "torch", 1)
forest = get("Ліс_2")
me.go_to(forest)
me.wait_until(time(21, 30))
assert me.is_in_dark
me.chop_wood(forest, amount=2)
assert not me.is_injured and "torch" not in me.carrying, me.carrying
notify("зі смолоскипом без травм")
)PY");
    s.runtime().set_code(w.char_obj_id(2), R"PY(
me.take(Axe, get("Склад_1"))
forest = get("Ліс_2")
me.go_to(forest)
me.wait_until(time(23, 30))
me.chop_wood(forest, amount=1)
assert me.is_injured
notify("у темряві травма")
)PY");
    s.run_until(at(2, 2));
    ctx.check(find_log(w, "при ліхтарі без травм").has_value(), "запалений ліхтар береже від травм уночі");
    ctx.check(find_log(w, "зі смолоскипом без травм").has_value(), "смолоскип у руках горить і береже від травм");
    ctx.check(find_log(w, "у темряві травма").has_value(), "без світла вночі — травма");
    const Object& lantern = obj(w, "Ліхтар_1");
    ctx.check(lantern.lit && lantern.fuel < 2 && lantern.fuel > 1.5, "ліхтар горить, олія витрачається: " +
              std::to_string(lantern.fuel));
    s.run_until(at(2, 7));
    ctx.check(!lantern.lit && lantern.fuel > 0.5, "на світанку ліхтар гасне, решта олії лишається");

    std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "ліхтарі й смолоскипи зберігаються: " + err);
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");

    // The oil press: flax → oil; oil + wood → a torch.
    Opts p;
    p.flax = 3;
    Simulation& ps = ctx.sim(p);
    ps.world().add_workshop("oil_press", "Олійня_1", {-60, -60});
    ps.runtime().set_code(ps.world().char_obj_id(0), R"PY(
storage = get("Склад_1")
press = get("Олійня_1")
assert isinstance(press, OilPress)
me.pick_up(storage, "flax", 3)
assert me.craft("oil", press) == 1
me.pick_up(storage, "wood", 1)
assert me.craft("torch", press) == 1 and me.carrying == {"torch": 1}, me.carrying
notify("олія й смолоскип")
)PY");
    obj(ps.world(), "Склад_1").resources["wood"] = 1;
    ps.run_until(at(1, 10));
    ctx.check(find_log(ps.world(), "олія й смолоскип").has_value(), "олійня: льон → олія → смолоскип");
}

// Food spoils in storages: a share a day by kind, slower in winter and in a cellar; smoked meat and cheese keep.
// The diet: one kind of food for a week lowers the mood, three kinds raise it.
void t_food(Ctx& ctx) {
    Opts o;
    o.chars = 2;
    o.bread = 100;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const Balance& b = w.balance();
    w.add_storage("Погріб_1", {-30, 30}, {}, {{"milk", 20}});
    obj(w, "Погріб_1").cellar = true;
    auto& st = obj(w, "Склад_1").resources;
    st["milk"] = 20;
    st["smoked_meat"] = 20;
    st["grain"] = 20;
    s.run_until(at(3, 5, 30));  // two days of the warm season
    const double expect_bread = 100 * std::exp(-2 * b.bread_spoil_per_day);
    const double expect_milk = 20 * std::exp(-2 * b.milk_spoil_per_day);
    const double expect_cellar = 20 * std::exp(-2 * b.milk_spoil_per_day * b.cellar_spoil_factor);
    ctx.check(std::abs(st["bread"] - expect_bread) <= 1.0, "хліб за дві доби: " + std::to_string(st["bread"]));
    ctx.check(std::abs(st["milk"] - expect_milk) <= 1.0, "молоко псується швидко: " + std::to_string(st["milk"]));
    ctx.check(std::abs(obj(w, "Погріб_1").resources["milk"] - expect_cellar) <= 1.0,
              "у погребі повільніше: " + std::to_string(obj(w, "Погріб_1").resources["milk"]));
    ctx.check(st["smoked_meat"] == 20 && st["grain"] == 20, "копчене м'ясо за дві доби не зіпсувалося, зерно не псується");
    ctx.check(find_log(w, "За добу зіпсувалося в «Склад_1»").has_value(), "журнал щодоби каже, що зіпсувалося");

    // Smokehouse and dairy.
    w.add_workshop("smokehouse", "Коптильня_1", {-60, -60});
    w.add_workshop("dairy", "Сироварня_1", {-60, 60});
    auto& stock = obj(w, "Склад_1").resources;  // adding objects moved them: `st` is stale
    stock["meat"] = 1;
    stock["wood"] = 1;
    stock["milk"] = std::max(stock["milk"], 3);
    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
assert isinstance(get("Погріб_1"), Cellar) and get("Погріб_1") in buildings(Storage)
me.pick_up(storage, "meat", 1)
me.pick_up(storage, "wood", 1)
assert me.craft("smoked_meat", get("Коптильня_1")) == 1
me.pick_up(storage, "milk", 3)
assert me.craft("cheese", get("Сироварня_1")) == 1
assert me.carrying == {"smoked_meat": 1, "cheese": 1}, me.carrying
me.eat("smoked_meat")
me.eat("cheese")
me.pick_up(storage, "bread", 1)
me.eat("bread")
assert sorted(me.diet) == ["bread", "cheese", "smoked_meat"], me.diet
notify("коптильня й сироварня")
)PY");
    s.run_until(at(3, 12));
    ctx.check(find_log(w, "коптильня й сироварня").has_value(), "м'ясо → копчене, молоко → сир; різноманітний раціон");

    // The diet's effect on mood: after the first week, idle at home, only bread vs three kinds.
    Opts d;
    d.chars = 2;
    d.start_day = 9;
    Simulation& ds = ctx.sim(d);
    World& dw = ds.world();
    Character& plain = dw.character(dw.char_obj_id(0));
    Character& varied = dw.character(dw.char_obj_id(1));
    plain.ate = {{"bread", at(9, 5)}};
    varied.ate = {{"bread", at(9, 5)}, {"milk", at(8, 12)}, {"eggs", at(7, 18)}};
    plain.mood = varied.mood = 50;
    plain.hunger = varied.hunger = 0;  // only the diet may change the mood
    ds.run_until(at(9, 15, 30));  // 10 hours, standing idle
    ctx.check(std::abs(plain.mood - (50 + 10 * b.diet_monotony_mood_per_hour)) < 0.3 &&
                  std::abs(varied.mood - (50 + 10 * b.diet_variety_mood_per_hour)) < 0.3,
              "одноманітна їжа знижує настрій, різноманітна — піднімає: " + std::to_string(plain.mood) + " / " +
                  std::to_string(varied.mood));

    std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "псування, погріб і раціон зберігаються: " + err);
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// Forestry: saplings from the nursery are planted where trees were cut (no more than the forest held) and grow
// into trees in tree_grow_days of the warm season.
void t_forestry(Ctx& ctx) {
    Opts o;
    o.trees = 10;
    o.bread = 20;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const Balance& b = w.balance();
    w.add_workshop("nursery", "Лісорозсадник_1", {-60, 60});
    s.runtime().set_code(w.char_obj_id(0), "TREE = " + std::to_string(static_cast<int>(b.wood_per_tree)) + R"PY(
forest = get("Ліс_1")
nursery = get("Лісорозсадник_1")
assert isinstance(nursery, Nursery)
assert me.craft("sapling", nursery, amount=2) == 2
try:
    me.plant(forest)
    assert False, "ліс повний"
except ActionError:
    pass
assert forest.capacity == 10 and forest.room == 0
storage = get("Склад_1")
me.take(Axe, storage)
assert me.chop_wood(forest, amount=TREE) == TREE  # одне дерево
assert forest.trees_left == 9 and forest.room == 1, (forest.trees_left, forest.room)
assert me.plant(forest) == 1  # місце лише для одного
assert forest.saplings == 1 and forest.room == 0 and me.carrying["sapling"] == 1, me.carrying
notify("посаджено")
)PY");
    s.run_until(at(1, 14));
    ctx.check(find_log(w, "посаджено").has_value(), "саджанці з лісорозсадника — на вирубане місце, не більше, ніж уміщав ліс");
    Object& forest = obj(w, "Ліс_1");
    ctx.check(forest.saplings.size() == 1, "один саджанець росте");
    forest.saplings[0] = 0.999;
    s.run_until(at(2, 1));
    ctx.check(forest.saplings.empty() && forest.trees_total == 11 && w.forest_trees_left(forest) == 10,
              "саджанець виріс у дерево");
    ctx.check(find_log(w, "виросло дерев: 1").has_value(), "журнал каже, скільки виросло за добу");

    std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "саджанці зберігаються: " + err);
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// Leisure: a bath in a heated bathhouse (water from the hands; fatigue, mood, cleanliness; once a day); beer from
// the brewery drunk at the tavern (mood; the third mug a day — a hangover); resting at the tavern lifts the mood more.
void t_leisure(Ctx& ctx) {
    Opts o;
    o.chars = 3;
    o.bread = 20;
    o.grain = 6;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const Balance& b = w.balance();
    w.add_workshop("bathhouse", "Лазня_1", {-40, 40});
    w.add_workshop("brewery", "Пивоварня_1", {-60, -60});
    w.add_workshop("tavern", "Корчма_1", {40, 60});
    obj(w, "Склад_1").resources["water"] = 7;
    obj(w, "Склад_1").resources["wood"] = 4;
    Character& bather = w.character(w.char_obj_id(0));
    bather.fatigue = 50;
    s.runtime().set_code(w.char_obj_id(0), R"PY(
storage = get("Склад_1")
bath = get("Лазня_1")
assert isinstance(bath, Bathhouse) and bath in buildings(HeatedWorkshop)
me.pick_up(storage, "water", 2)
try:
    me.bathe(bath)
    assert False, "лазня холодна"
except NoItemError:
    pass
me.pick_up(storage, "wood", 2)
me.heat(bath)
fatigue, mood = me.fatigue, me.mood
me.bathe(bath)
assert me.is_clean and me.fatigue < fatigue - 25 and me.mood > mood + 9, (me.fatigue, me.mood)
me.pick_up(storage, "water", 2)
try:
    me.bathe(bath)
    assert False, "раз на добу"
except ActionError:
    pass
notify("лазня")
)PY");
    s.runtime().set_code(w.char_obj_id(1), R"PY(
storage = get("Склад_1")
me.pick_up(storage, "grain", 6)
me.pick_up(storage, "water", 3)
assert me.craft("beer", get("Пивоварня_1")) == 3 and me.carrying == {"beer": 3}, me.carrying
tavern = get("Корчма_1")
mood = me.mood
me.drink(tavern)
me.drink(tavern)
assert not me.has_hangover
me.drink(tavern)
assert me.has_hangover and me.mood > mood + 15, me.mood
notify("корчма")
)PY");
    Character& rester = w.character(w.char_obj_id(2));
    s.runtime().set_code(w.char_obj_id(2), "me.go_to(get(\"Корчма_1\"))\nme.rest(hours=2)\n");
    s.run_until(at(1, 6, 30));
    const double rest_start = rester.mood;  // at the tavern by now, resting
    s.run_until(at(1, 7, 30));
    ctx.check(std::abs(rester.mood - (rest_start + b.mood_rest_per_hour + b.tavern_rest_mood_per_hour)) < 0.3,
              "відпочинок у корчмі піднімає настрій більше: " + std::to_string(rest_start) + " → " +
                  std::to_string(rester.mood));
    s.run_until(at(1, 16));
    ctx.check(find_log(w, "лазня").has_value() && bather.clean_until > w.now(), "лазня: втома, настрій, чистота");
    ctx.check(find_log(w, "корчма").has_value() && find_log(w, "забагато пива").has_value(), "пиво з пивоварні в корчмі; похмілля");

    std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "лазня, пиво й похмілля зберігаються: " + err);
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// Skills: grow with practice (twice as fast next to a mentor), speed the work up, a skilled smith forges better
// tools; a teacher's lesson at the school teaches everyone there. The real game starts with skills.
void t_skills(Ctx& ctx) {
    {
        Opts o = mvp_opts();
        o.random_rain = false;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        const Balance& b = w.balance();
        ctx.check(w.skill_of(w.character(w.char_obj_id(0)), "woodcutting") == b.start_skill_main &&
                      w.skill_of(w.character(w.char_obj_id(1)), "farming") == b.start_skill_main &&
                      w.skill_of(w.character(w.char_obj_id(3)), "crafting") == b.start_skill_main,
                  "на старті кожен уміє своє: лісоруб — рубку, фермерка — землеробство, пекарка — ремесло");
    }
    Opts o;
    o.chars = 4;
    o.axes = 3;
    o.bread = 20;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const Balance& b = w.balance();
    w.add_forest("Ліс_2", {300, 300}, 50);
    Character& master = w.character(w.char_obj_id(0));
    Character& apprentice = w.character(w.char_obj_id(1));  // next to the master
    Character& alone = w.character(w.char_obj_id(2));       // in another forest
    master.skills["woodcutting"] = 100;
    const std::string chop = R"PY(
me.take(Axe, get("Склад_1"))
forest = get(FOREST)
me.go_to(forest)
start = world.now
me.chop_wood(forest, amount=6)
notify(f"{me.name}: {(world.now - start).minutes:.0f} хв")
)PY";
    s.runtime().set_code(w.char_obj_id(0), "FOREST = 'Ліс_1'\n" + chop);
    s.runtime().set_code(w.char_obj_id(1), "FOREST = 'Ліс_1'\n" + chop);
    s.runtime().set_code(w.char_obj_id(2), "FOREST = 'Ліс_2'\n" + chop);
    s.run_until(at(1, 12));
    const double with_mentor = w.skill_of(apprentice, "woodcutting"), without = w.skill_of(alone, "woodcutting");
    ctx.check(without > 1.0 && std::abs(with_mentor / without - b.mentor_factor) < 0.35,
              "практика розвиває навичку, поруч із майстром — удвічі швидше: " + std::to_string(with_mentor) + " / " +
                  std::to_string(without));
    auto minutes = [&](const std::string& name) {
        auto e = find_log(w, name + ": ");
        return e ? std::stoi(e->text.substr(e->text.find(": ") + 2)) : 0;
    };
    const int fast = minutes("Остап"), slow = minutes("Богдан");
    ctx.check(fast > 0 && slow > 0 && std::abs(double(slow) / fast - (1 + b.skill_speed_bonus)) < 0.1,
              "майстер (100) працює на skill_speed_bonus швидше: " + std::to_string(fast) + " хв проти " +
                  std::to_string(slow));

    // Tool quality and the school.
    w.add_workshop("smithy", "Кузня_1", {-60, -60});
    w.add_workshop("school", "Школа_1", {40, 60});
    obj(w, "Склад_1").resources["scrap"] = 4;
    obj(w, "Склад_1").resources["wood"] = 4;
    Character& smith = w.character(w.char_obj_id(3));
    smith.skills["crafting"] = 80;
    s.runtime().set_code(w.char_obj_id(3), R"PY(
storage = get("Склад_1")
school = get("Школа_1")
me.pick_up(storage, "scrap", 2)
me.pick_up(storage, "wood", 2)
me.craft(Axe, get("Кузня_1"))
assert me.tool.quality == 3, me.tool.quality
me.put_back(storage)
me.teach(school, "crafting")
notify("урок")
)PY");
    s.runtime().set_code(w.char_obj_id(1), "me.go_to(get('Школа_1'))\nme.rest(hours=6)\n");
    s.runtime().set_code(w.char_obj_id(2), R"PY(
try:
    me.teach(get("Школа_1"), "crafting")
    assert False, "без навички не вчать"
except ActionError:
    notify("без навички не вчать")
)PY");
    s.run_until(at(1, 19));
    ctx.check(find_log(w, "урок").has_value() && find_log(w, "учнів 1").has_value(), "урок у школі для тих, хто там");
    ctx.check(std::abs(w.skill_of(apprentice, "crafting") - b.lesson_gain) < 0.01, "учень отримав навичку з уроку");
    ctx.check(find_log(w, "без навички не вчать").has_value(), "вчити може лише той, хто вміє");

    std::string text = save_game(s);
    std::string err;
    auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
    ctx.check(loaded && save_game(*loaded) == text, "навички зберігаються: " + err);
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}

// The end of the game: the first winter survived (once, survives save/load) or everyone dead.
void t_outcome(Ctx& ctx) {
    {
        Opts o;
        o.chars = 2;
        o.start_day = 15;  // the last day of the first winter (10 warm + 5 winter days)
        o.start_hour = 18;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        s.run_until(at(16, 1));
        auto outcome = find_log(w, "Першу зиму пережито");
        ctx.check(w.winters_survived() == 1 && outcome && outcome->kind == LogKind::Outcome &&
                      outcome->text.find("Вижило 2 з 2") != std::string::npos,
                  "кінець першої зими з живими — «Першу зиму пережито» в журналі");

        std::string text = save_game(s);
        std::string err;
        auto loaded = load_game(proto::load_balance(proto::source_path("config/balance.json"), false), text, err);
        ctx.check(loaded && loaded->world().winters_survived() == 1, "кількість пережитих зим зберігається: " + err);

        while (w.now() < at(31, 1)) {  // the second winter ends on day 31; characters without code are kept alive
            s.tick(600);
            for (Character& c : w.characters()) {
                c.hunger = c.fatigue = 0;
                c.warmth = c.health = 100;
            }
        }
        int outcomes = 0;
        for (const LogEntry& e : w.log_entries()) outcomes += e.kind == LogKind::Outcome ? 1 : 0;
        ctx.check(w.winters_survived() == 2 && outcomes == 1 && find_log(w, "Пережито зим: 2").has_value(),
                  "друга зима — звичайний запис, підсумок гри оголошено лише раз");
    }
    {
        Opts o;
        o.chars = 3;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        for (Character& c : w.characters()) {
            c.hunger = 100;
            c.health = 3;
        }
        s.run_until(at(1, 12));
        int deaths = 0, outcomes = 0;
        for (const LogEntry& e : w.log_entries()) {
            deaths += e.kind == LogKind::Death ? 1 : 0;
            outcomes += e.kind == LogKind::Outcome ? 1 : 0;
        }
        auto lost = find_log(w, "Усі персонажі загинули");
        ctx.check(deaths == 3 && outcomes == 1 && lost && lost->kind == LogKind::Outcome, "смерть останнього — «Усі персонажі загинули»");
        double last_death = 0;
        for (const Character& c : w.characters()) last_death = std::max(last_death, c.death_time);
        ctx.check(lost && lost->time == last_death, "підсумок записано в момент останньої смерті");
        ctx.check(w.alive_count() == 0 && w.winters_survived() == 0, "живих немає, зим не пережито");
    }
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
    ctx.check(garment_counts(obj(w, "Будинок_1").chest)["warm_clothes"] == 0 &&
                  garment_counts(obj(w, "Будинок_2").chest)["warm_clothes"] == 2,
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

// ------------------------------------------------------------------ MVP: saving and loading

void t_save_load(Ctx& ctx) {
    Simulation& s = ctx.sim(mvp_opts());
    World& w = s.world();
    std::string err;
    w.add_blueprint(ObjKind::House, "Будинок_2", {60, 100}, err);
    s.runtime().set_global("WINTER_MODE", true, err);
    std::string shared;
    if (!set_settlement_code(ctx, s, &shared)) return;
    s.run_until(at(3, 12));

    const std::string saved = save_game(s);
    ctx.info("розмір збереження: " + std::to_string(saved.size() / 1024) + " КБ");
    static Balance balance = proto::load_balance(proto::source_path("config/balance.json"), false);
    auto loaded = load_game(balance, saved, err);
    ctx.check(loaded != nullptr, "збереження завантажилося: " + err);
    if (!loaded) return;
    World& lw = loaded->world();
    ctx.check(save_game(*loaded) == saved, "зберегти → завантажити → зберегти дає той самий файл");
    ctx.check(lw.now() == w.now(), "час гри відновлено");
    ctx.check(obj(lw, "Склад_1").resources == obj(w, "Склад_1").resources, "склад відновлено");
    ctx.check(obj(lw, "Поле_1").field_state == obj(w, "Поле_1").field_state &&
                  obj(lw, "Поле_1").growth == obj(w, "Поле_1").growth,
              "поле відновлено");
    ctx.check(!obj(lw, "Будинок_2").built && obj(lw, "Будинок_2").materials_needed["wood"] == 40,
              "креслення відновлено");
    ctx.check(lw.globals().count("WINTER_MODE") == 1, "глобальні змінні відновлено");
    bool same_people = lw.characters().size() == w.characters().size();
    for (size_t i = 0; same_people && i < w.characters().size(); ++i) {
        const Character& a = w.characters()[i];
        const Character& b = lw.characters()[i];
        same_people = a.name == b.name && a.hunger == b.hunger && a.warmth == b.warmth && a.home == b.home &&
                      a.carrying == b.carrying && a.tool.id == b.tool.id && a.tool.condition == b.tool.condition &&lw.objects()[b.obj].pos.x == w.objects()[a.obj].pos.x;
    }
    ctx.check(same_people, "персонажі відновлені: потреби, речі, дім, позиція");
    ctx.check(loaded->runtime().shared_code() == shared &&
                  loaded->runtime().source(lw.char_obj_id(0)) == "work = woodcutter\n",
              "спільний код і код персонажів відновлено");
    ctx.check(lw.log_entries().size() == std::min<size_t>(w.log_entries().size(), 500), "журнал подій відновлено");

    // Characters start their code from the top and keep living: the colony still survives the first winter.
    loaded->run_until(at(16, 6), 120);
    int alive = 0;
    for (Character& c : lw.characters()) alive += c.alive ? 1 : 0;
    int errors = 0;
    for (const LogEntry& e : lw.log_entries()) errors += e.kind == LogKind::Error ? 1 : 0;
    ctx.info("після завантаження на 3-й день: живих " + std::to_string(alive) + "/4 на кінець зими");
    ctx.check(alive == 4 && errors == 0, "після завантаження поселення переживає зиму без помилок");

    ctx.check(!load_game(balance, "not json", err) && !err.empty(), "битий файл — зрозуміла помилка");
    ctx.check(!load_game(balance, "{\"format\": \"other\"}", err), "чужий JSON не приймається");
    std::string broken = saved;
    broken.replace(broken.find("\"objects\""), 9, "\"objectz\"");
    ctx.check(!load_game(balance, broken, err) && err.find("пошкоджене") != std::string::npos,
              "пошкоджене збереження не завантажується: " + err);
}

// ------------------------------------------------------------------ shared code, own code, rules

void t_shared_code(Ctx& ctx) {
    {
        Opts o;
        o.chars = 3;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        const int ostap = w.char_obj_id(0), marta = w.char_obj_id(1), bohdan = w.char_obj_id(2);
        s.runtime().set_shared_code(R"PY(
GREETING = "привіт зі спільного коду"

def work():
    notify("робота за замовчуванням")
    me.rest(hours=1)

def main():
    while True:
        work()
)PY", false);
        s.runtime().set_code(ostap, "");
        s.runtime().set_code(marta, "def work():\n    notify(\"своя робота\")\n    me.rest(hours=1)\n");
        s.runtime().set_code(bohdan, "notify(GREETING)\n");
        s.run_until(at(1, 8, 30));
        ctx.check(logs_of(w, ostap, "робота за замовчуванням").size() >= 2,
                  "без власного коду персонаж живе за спільним розпорядком main()");
        ctx.check(logs_of(w, marta, "своя робота").size() >= 2 && logs_of(w, marta, "за замовчуванням").empty(),
                  "власне визначення work() має пріоритет над спільним");
        ctx.check(logs_of(w, bohdan, "привіт зі спільного коду").size() == 1 &&
                      logs_of(w, bohdan, "за замовчуванням").size() >= 2,
                  "власний код бачить імена зі спільного, потім іде main()");
        ctx.check(s.runtime().current_file(ostap) == CodeFile::Shared && s.runtime().current_file(marta) == CodeFile::Own,
                  "рантайм знає, у якому коді зараз рядок");

        s.runtime().set_shared_code(R"PY(
GREETING = "привіт зі спільного коду"

def work():
    notify("нова спільна робота")
    me.rest(hours=1)

def main():
    while True:
        work()
)PY");
        const size_t marta_before = logs_of(w, marta, "своя робота").size();
        s.run_until(at(1, 10, 30));
        ctx.check(find_log(w, "Спільний код змінено").has_value(), "зміну спільного коду записано в журнал");
        ctx.check(logs_of(w, ostap, "нова спільна робота").size() >= 1, "зміна спільного коду перезапустила всіх");
        ctx.check(logs_of(w, marta, "нова спільна").empty() && logs_of(w, marta, "своя робота").size() > marta_before,
                  "власне перевизначення лишилося в силі й після зміни спільного коду");
        ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
    }
    {
        Opts o;
        o.chars = 2;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        s.runtime().set_shared_code("def boom():\n    raise ValueError(\"бум\")\n", false);
        s.runtime().set_code(w.char_obj_id(0), "notify(\"старт\")\nboom()\n");
        s.run_until(at(1, 6));
        auto err = find_log(w, "ValueError");
        if (err) ctx.info(err->text);
        ctx.check(err && err->text.find("(спільний код, рядок 2)") != std::string::npos,
                  "помилку у функції спільного коду показано з рядком спільного коду");
        s.runtime().set_shared_code("x = (\n");
        auto syntax = find_log(w, "помилка в спільному коді");
        ctx.check(syntax && syntax->text.find("спільний код, рядок 1") != std::string::npos,
                  "синтаксична помилка в спільному коді — з рядком");
        ctx.check(s.runtime().state(w.char_obj_id(1)) == AgentState::Crashed, "зі зламаним спільним кодом не стартує ніхто");
    }
}

void t_rules(Ctx& ctx) {
    {
        Opts o;
        o.chars = 3;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        const int ostap = w.char_obj_id(0), marta = w.char_obj_id(1), bohdan = w.char_obj_id(2);
        s.runtime().set_shared_code(R"PY(
@on(lambda: world.time >= time(22, 0) or world.time < time(6, 0))
def night_sleep(me):
    notify("ніч: сплю")
    me.sleep(until=time(6, 0))

@on(time(12, 0), priority=2)
def lunch(me, event):
    assert event.kind == Event.TIME
    notify("обід")

def main():
    while True:
        me.rest(hours=1)
)PY", false);
        s.runtime().set_code(ostap, "");
        s.runtime().set_code(marta, "while True:\n    me.wait_until(world.time + minutes(30))\n");
        s.runtime().set_code(bohdan, "assert off(night_sleep) == 1\n");
        s.run_until(at(2, 8));
        auto ostap_night = logs_of(w, ostap, "ніч: сплю");
        auto marta_night = logs_of(w, marta, "ніч: сплю");
        ctx.check(ostap_night.size() == 2 && marta_night.size() == 2,
                  "нічне правило спрацювало на старті (05:30) і о 22:00 — і в main(), і у власному циклі персонажа");
        ctx.check(marta_night.size() == 2 && marta_night[1].time >= at(1, 22) && marta_night[1].time < at(1, 22, 2),
                  "правило-умова спрацьовує протягом хвилини");
        ctx.check(logs_of(w, bohdan, "ніч: сплю").empty(), "off(night_sleep) відміняє правило для одного персонажа");
        int lunches = 0;
        for (int obj : {ostap, marta, bohdan})
            for (const LogEntry& e : logs_of(w, obj, "обід")) lunches += e.time >= at(1, 12) && e.time < at(1, 12, 1);
        ctx.check(lunches == 3, "@on(time(12, 0)) спрацювало в усіх рівно о 12:00");
        ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
    }
    {
        Opts o;
        o.chars = 2;
        Simulation& s = ctx.sim(o);
        World& w = s.world();
        s.runtime().set_code(w.char_obj_id(0), "@on(lambda: me.rest(hours=1))\ndef r(me):\n    pass\n\nme.rest(hours=2)\n");
        s.runtime().set_code(w.char_obj_id(1),
                             "def busy():\n    while True:\n        pass\n\n@on(lambda: busy())\ndef r(me):\n    pass\n\nme.rest(hours=2)\n");
        s.run_until(at(1, 7));
        ctx.check(find_log(w, "помилка в умові правила").has_value(), "дія в умові правила — зрозуміла помилка");
        ctx.check(find_log(w, "умова правила виконує понад").has_value(), "нескінченна умова правила не вішає гру");
        ctx.check(s.runtime().state(w.char_obj_id(0)) == AgentState::Crashed &&
                      s.runtime().state(w.char_obj_id(1)) == AgentState::Crashed,
                  "обидва коди зупинено з помилкою");
    }
}

// The character's own code has priority over shared rules: its rules outrank shared ones of equal
// priority, and a rule with the same function name replaces the shared one.
void t_rule_priority(Ctx& ctx) {
    Opts o;
    o.chars = 5;
    Simulation& s = ctx.sim(o);
    World& w = s.world();
    const int plain = w.char_obj_id(0), shift = w.char_obj_id(1), own_night = w.char_obj_id(2),
              watch = w.char_obj_id(3), late = w.char_obj_id(4);
    s.runtime().set_shared_code(R"PY(
@on(lambda: world.time >= time(22, 0) or world.time < time(6, 0))
def night_sleep(me):
    notify("ніч: сплю")
    me.sleep(until=time(6, 0))

def main():
    while True:
        me.rest(hours=1)
)PY", false);
    s.runtime().set_code(plain, "");
    s.runtime().set_code(shift, R"PY(
@on(time(21, 0))
def evening_shift(me):
    notify("вечірня зміна")
    me.wait_until(time(23, 0))
)PY");
    s.runtime().set_code(own_night, R"PY(
@on(lambda: world.time >= time(23, 0) or world.time < time(6, 0))
def night_sleep(me):
    notify("моя ніч")
    me.sleep(until=time(6, 0))
)PY");
    s.runtime().set_code(watch, R"PY(
@on(time(21, 0))
def night_watch(me):
    notify("варта")
    me.wait_until(time(7, 0))
)PY");
    s.runtime().set_code(late, R"PY(
@on(time(23, 0))
def late_check(me):
    notify("пізня перевірка")
)PY");
    s.run_until(at(2, 8));

    auto between = [&](int obj, const std::string& needle, double from, double to) {
        int n = 0;
        for (const LogEntry& e : logs_of(w, obj, needle)) n += e.time >= from && e.time < to;
        return n;
    };
    ctx.check(between(plain, "ніч: сплю", at(1, 22), at(1, 22, 2)) == 1, "без власного коду — спати о 22:00");
    ctx.check(between(shift, "вечірня зміна", at(1, 21), at(1, 21, 1)) == 1 &&
                  between(shift, "ніч: сплю", at(1, 21), at(1, 23)) == 0 &&
                  between(shift, "ніч: сплю", at(1, 23), at(1, 23, 2)) == 1,
              "правило персонажа (зміна 21:00–23:00) має пріоритет: спільне нічне правило чекає до 23:00");
    ctx.check(logs_of(w, own_night, "ніч: сплю").empty() && between(own_night, "моя ніч", at(1, 23), at(1, 23, 2)) == 1,
              "правило з тим самим іменем у коді персонажа замінює спільне");
    ctx.check(between(watch, "варта", at(1, 21), at(1, 21, 1)) == 1 &&
                  between(watch, "ніч: сплю", at(1, 21), at(2, 8)) == 0,
              "правило-умова, що дочекалося черги вже вдень, не спрацьовує (варта до 07:00 — без сну до 06:00)");
    ctx.check(between(late, "ніч: сплю", at(1, 22), at(1, 22, 2)) == 1 &&
                  between(late, "пізня перевірка", at(1, 23), at(1, 23, 1)) == 1,
              "правило персонажа перериває спільне правило, що вже виконується");
    ctx.check(count_kind(w, LogKind::Error) == 0, "немає необроблених винятків");
}
