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
    if (item == "scythe") return {"коси", "коси"};
    if (item == "pickaxe") return {"кайла", "кайла"};
    if (item == "warm_clothes") return {"теплого одягу", "теплий одяг"};
    if (item == "clothes") return {"звичайного одягу", "звичайний одяг"};
    return {"предмета", "предмети"};
}

bool is_tool(const std::string& tool) { return tool == "axe" || tool == "sickle" || tool == "scythe" || tool == "pickaxe"; }

const char* tool_ua(const std::string& tool) {
    if (tool == "axe") return "сокира";
    if (tool == "scythe") return "коса";
    if (tool == "pickaxe") return "кайло";
    return "серп";
}

const char* tool_broke_ua(const std::string& tool) {
    if (tool == "axe") return "Сокира зламалася";
    if (tool == "scythe") return "Коса зламалася";
    if (tool == "pickaxe") return "Кайло зламалося";
    return "Серп зламався";
}

const char* species_ua(const std::string& species) { return species == "cow" ? "корова" : "курка"; }

const char* skill_ua(const std::string& s) {
    if (s == "woodcutting") return "рубка";
    if (s == "farming") return "землеробство";
    if (s == "husbandry") return "тваринництво";
    if (s == "mining") return "видобуток";
    if (s == "building") return "будівництво";
    if (s == "crafting") return "ремесло";
    if (s == "medicine") return "медицина";
    return "?";
}

// Where a recipe is made, for messages: "будинок" or the workshop's name.
std::string place_ua(const Balance& b, const std::string& at) {
    if (at == "house") return "будинок";
    auto it = b.workshops.find(at);
    return it == b.workshops.end() ? at : it->second.name;
}

bool makes_here(const Recipe& r, const Object& o) {
    return (r.at == "house" && o.kind == ObjKind::House) || (o.kind == ObjKind::Workshop && o.workshop == r.at);
}

const char* workplace_ua(const std::string& workplace) {
    if (workplace == "mill") return "жорна";
    if (workplace == "oven") return "піч";
    if (workplace == "forge") return "горн";
    if (workplace == "loom") return "ткацький верстат";
    if (workplace == "table") return "кравецький стіл";
    if (workplace == "saw") return "пилорама";
    if (workplace == "bench") return "верстак";
    if (workplace == "pit") return "вугільна яма";
    if (workplace == "windlass") return "ворот";
    if (workplace == "press") return "прес";
    if (workplace == "smoke") return "коптильна камера";
    if (workplace == "vat") return "чан";
    if (workplace == "beds") return "грядки";
    if (workplace == "kettle") return "казан";
    return workplace.c_str();
}

// Crops of a field: seed to sow and the harvest.
bool is_crop(const std::string& crop) { return crop == "grain" || crop == "flax"; }
int crop_seed(const Balance& b, const std::string& crop) {
    return static_cast<int>(std::lround(crop == "flax" ? b.flax_seed : b.field_seed_grain));
}
int crop_yield(const Balance& b, const std::string& crop) {
    return static_cast<int>(std::lround(crop == "flax" ? b.flax_yield : b.field_yield_grain));
}

// What dismantling a tool gives: {scrap, wood from the handle}.
std::pair<int, int> dismantle_yield(const Balance& b, const std::string& tool) {
    if (tool == "axe") return {int(std::lround(b.axe_scrap)), int(std::lround(b.dismantle_wood))};
    if (tool == "scythe") return {int(std::lround(b.scythe_scrap)), int(std::lround(b.dismantle_wood))};
    if (tool == "pickaxe") return {int(std::lround(b.pickaxe_scrap)), int(std::lround(b.dismantle_wood))};
    return {int(std::lround(b.sickle_scrap)), 0};
}

std::string resource_ua(const std::string& r) {
    if (r == "wood") return "дрова";
    if (r == "bread") return "хліб";
    if (r == "grain") return "зерно";
    if (r == "flour") return "борошно";
    if (r == "hay") return "сіно";
    if (r == "milk") return "молоко";
    if (r == "eggs") return "яйця";
    if (r == "meat") return "м'ясо";
    if (r == "scrap") return "брухт";
    if (r == "flax") return "льон";
    if (r == "cloth") return "тканина";
    if (r == "rags") return "ганчір'я";
    if (r == "planks") return "дошки";
    if (r == "bed") return "ліжко";
    if (r == "clothes") return "звичайний одяг";
    if (r == "warm_clothes") return "теплий одяг";
    if (r == "axe") return "сокира";
    if (r == "sickle") return "серп";
    if (r == "scythe") return "коса";
    if (r == "pickaxe") return "кайло";
    if (r == "stone") return "камінь";
    if (r == "ore") return "руда";
    if (r == "charcoal") return "деревне вугілля";
    if (r == "iron") return "залізо";
    if (r == "water") return "вода";
    if (r == "herbs") return "трави";
    if (r == "bandage") return "бинт";
    if (r == "oil") return "олія";
    if (r == "torch") return "смолоскип";
    if (r == "smoked_meat") return "копчене м'ясо";
    if (r == "cheese") return "сир";
    if (r == "sapling") return "саджанець";
    if (r == "beer") return "пиво";
    if (r == "cow") return "корова";
    if (r == "chicken") return "курка";
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
        case ObjKind::Board: return "board";
        case ObjKind::Barn: return "barn";
        case ObjKind::Meadow: return "meadow";
        case ObjKind::Animal: return "animal";
        case ObjKind::TownHall: return "town_hall";
        case ObjKind::TradingPost: return "trading_post";
        case ObjKind::Workshop: return "workshop";
        case ObjKind::Deposit: return "deposit";
        case ObjKind::Lantern: return "lantern";
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

const char* log_kind_name(LogKind k) {
    switch (k) {
        case LogKind::Info: return "info";
        case LogKind::Notify: return "notify";
        case LogKind::Print: return "print";
        case LogKind::Warning: return "warning";
        case LogKind::Error: return "error";
        case LogKind::Death: return "death";
        case LogKind::Outcome: return "outcome";
    }
    return "info";
}

bool is_building(ObjKind k) {
    return k == ObjKind::Storage || k == ObjKind::House || k == ObjKind::Field || k == ObjKind::Board || k == ObjKind::Lantern ||
           k == ObjKind::Barn || k == ObjKind::TownHall || k == ObjKind::TradingPost || k == ObjKind::Workshop;
}

World::World(const Balance& balance, uint64_t seed)
    : balance_(balance), rng_(seed), trade_rng_(seed ^ 0x7ade5eedULL), health_rng_(seed ^ 0x4ea17e55ULL) {}

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
    for (const auto& [kind, n] : tools)
        for (int i = 0; i < n; ++i) objects_[id].tools.push_back(make_tool(kind));
    objects_[id].resources = resources;
    return id;
}

ToolItem World::make_tool(const std::string& kind) {
    ToolItem t;
    t.id = next_tool_id_++;
    t.kind = kind;
    return t;
}

ToolItem* World::tool_in(Object& storage, int tool_id) {
    for (ToolItem& t : storage.tools)
        if (t.id == tool_id) return &t;
    return nullptr;
}

// Clothes wear while worn; worn out they turn into rags (in the hands) and the character has no clothes.
void World::wear_clothes(Character& c, double seconds) {
    if (c.clothes.empty() || !c.alive) return;
    const Balance& b = balance_;
    const double before = c.clothes.condition;
    const double per_day = c.clothes.kind == "warm_clothes" ? b.warm_clothes_wear_per_day : b.clothes_wear_per_day;
    c.clothes.condition = std::max(0.0, c.clothes.condition - per_day * seconds / kDay);
    const std::string what = resource_ua(c.clothes.kind);
    if (c.clothes.condition <= 0) {
        log(c.obj, LogKind::Warning, quote(c.name) + ": " + what + " зносився — тепер це ганчір'я; переодягніться (me.wear)");
        c.clothes = Garment{};
        c.carrying["rags"] += 1;
    } else if (before >= b.clothes_worn_warning && c.clothes.condition < b.clothes_worn_warning) {
        log(c.obj, LogKind::Warning, quote(c.name) + ": " + what + " майже зношений (стан " + fmt_num(std::floor(c.clothes.condition)) + "%)");
    }
}

// Work wears the tool in hands: quality 1 lasts ~40 h of work (balance), better ones last longer.
bool World::wear_tool(Character& c, double seconds) {
    ToolItem& t = c.tool;
    if (t.empty() || t.broken()) return false;
    const Balance& b = balance_;
    const double before = t.condition;
    const double quality_factor = 1.0 + 0.25 * (t.quality - 1);
    t.condition = std::max(0.0, t.condition - b.tool_wear_per_hour * seconds / 3600.0 / quality_factor);
    const char* what = tool_ua(t.kind);
    if (t.condition <= 0) {
        log(c.obj, LogKind::Warning, quote(c.name) + ": зламався інструмент — " + what);
        return true;
    }
    if (before >= b.tool_worn_warning && t.condition < b.tool_worn_warning)
        log(c.obj, LogKind::Warning, quote(c.name) + ": " + what + " майже зношений (стан " + fmt_num(std::floor(t.condition)) + "%)");
    return false;
}

int World::add_house(const std::string& name, Vec2 pos, const std::map<std::string, int>& chest) {
    int id = add_object(ObjKind::House, name, pos);
    for (const auto& [kind, n] : chest)
        for (int i = 0; i < n; ++i) objects_[id].chest.push_back(Garment{kind});
    objects_[id].capacity = static_cast<int>(std::lround(balance_.house_capacity));
    objects_[id].temperature = unheated_house_temperature(now_);
    return id;
}

int World::add_forest(const std::string& name, Vec2 pos, int trees) {
    int id = add_object(ObjKind::Forest, name, pos);
    objects_[id].trees_total = trees;
    objects_[id].capacity = trees;
    return id;
}

int World::add_deposit(const std::string& name, Vec2 pos, const std::string& resource, int amount) {
    int id = add_object(ObjKind::Deposit, name, pos);
    objects_[id].deposit = resource;
    objects_[id].deposit_left = amount;
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
    c.mood = balance_.mood_start;
    c.clothes = Garment{"clothes"};
    c.local_time = now_;
    c.last_action_time = now_;
    chars_.push_back(c);
    return id;
}

int World::add_board(const std::string& name, Vec2 pos) { return add_object(ObjKind::Board, name, pos); }
int World::add_lantern(const std::string& name, Vec2 pos) { return add_object(ObjKind::Lantern, name, pos); }

int World::add_barn(const std::string& name, Vec2 pos) {
    int id = add_object(ObjKind::Barn, name, pos);
    objects_[id].capacity = static_cast<int>(std::lround(balance_.barn_capacity));
    return id;
}

int World::add_town_hall(const std::string& name, Vec2 pos) {
    int id = add_object(ObjKind::TownHall, name, pos);
    objects_[id].coins = static_cast<int>(std::lround(balance_.start_coins));
    return id;
}

int World::add_trading_post(const std::string& name, Vec2 pos) { return add_object(ObjKind::TradingPost, name, pos); }

int World::add_workshop(const std::string& type, const std::string& name, Vec2 pos) {
    if (!balance_.workshops.count(type)) throw std::invalid_argument("unknown workshop type: " + type);
    int id = add_object(ObjKind::Workshop, name, pos);
    objects_[id].workshop = type;
    objects_[id].temperature = unheated_house_temperature(now_);
    return id;
}

int World::add_meadow(const std::string& name, Vec2 pos) {
    int id = add_object(ObjKind::Meadow, name, pos);
    objects_[id].hay = balance_.meadow_hay_max;
    return id;
}

int World::add_animal(const std::string& species, int barn, std::string& error) {
    if (species != "cow" && species != "chicken") {
        error = "Невідома тварина " + quote(species) + ": є корова (cow) і курка (chicken)";
        return -1;
    }
    const Object* b = object(barn);
    if (!b || b->kind != ObjKind::Barn) {
        error = "Тварини живуть лише в хліві";
        return -1;
    }
    if (!b->built) {
        error = quote(b->name) + " ще не збудовано";
        return -1;
    }
    if (animals_count(barn) >= b->capacity) {
        error = "У хліві " + quote(b->name) + " немає вільного місця";
        return -1;
    }
    const std::string prefix = species == "cow" ? "Корова_" : "Курка_";
    int n = 1;
    while (by_name_.count(prefix + std::to_string(n))) ++n;
    const Vec2 pos = b->pos;
    const int id = add_object(ObjKind::Animal, prefix + std::to_string(n), pos);
    Object& a = objects_[id];
    a.species = species;
    a.home = barn;
    return id;
}

void World::init_knowledge() {
    for (Character& c : chars_)
        if (c.alive) observe(c);
}

void World::set_start_time(double t) {
    now_ = t;
    for (auto& c : chars_) {
        c.local_time = t;
        c.last_action_time = t;
    }
    for (auto& o : objects_)
        if (has_stove(o)) o.temperature = unheated_house_temperature(t);
    // The merchant's first visit that has not ended yet.
    const Balance& b = balance_;
    double arrival = (std::max(1.0, b.merchant_first_day) - 1) * kDay + b.merchant_arrive_hour * 3600.0;
    const double stay = (b.merchant_leave_hour - b.merchant_arrive_hour) * 3600.0;
    while (arrival + stay <= t) arrival += std::max(1.0, b.merchant_interval_days) * kDay;
    plan_visit(arrival);
}

void World::add_rain(double start, double end) { rain_.push_back({start, end}); }

int World::add_blueprint(ObjKind kind, const std::string& name, Vec2 pos, std::string& error, const std::string& workshop) {
    const Balance& b = balance_;
    if (kind == ObjKind::Workshop && !b.workshops.count(workshop)) {
        error = "Невідома майстерня " + quote(workshop);
        return -1;
    }
    if (!is_building(kind) || kind == ObjKind::TownHall) {
        error = "За кресленням можна збудувати будинок, склад, поле, хлів, майстерню, торговий пост або дошку оголошень";
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
    std::string what = "Поле";
    if (kind == ObjKind::House) {
        what = "Будинок";
        o.materials_needed["wood"] = static_cast<int>(std::lround(b.house_build_wood));
        o.build_total = b.house_build_hours * 3600.0;
        o.capacity = static_cast<int>(std::lround(b.house_capacity));
        o.chest.clear();
    } else if (kind == ObjKind::Storage && workshop == "cellar") {
        what = "Погріб";
        o.cellar = true;
        o.materials_needed["wood"] = static_cast<int>(std::lround(b.cellar_build_wood));
        o.materials_needed["stone"] = static_cast<int>(std::lround(b.cellar_build_stone));
        o.build_total = b.cellar_build_hours * 3600.0;
    } else if (kind == ObjKind::Storage) {
        what = "Склад";
        o.materials_needed["wood"] = static_cast<int>(std::lround(b.storage_build_wood));
        o.build_total = b.storage_build_hours * 3600.0;
    } else if (kind == ObjKind::Barn) {
        what = "Хлів";
        o.materials_needed["wood"] = static_cast<int>(std::lround(b.barn_build_wood));
        o.build_total = b.barn_build_hours * 3600.0;
        o.capacity = static_cast<int>(std::lround(b.barn_capacity));
    } else if (kind == ObjKind::Workshop) {
        const WorkshopType& type = b.workshops.at(workshop);
        what = type.name;
        o.workshop = workshop;
        o.materials_needed = type.materials;
        o.build_total = type.hours * 3600.0;
    } else if (kind == ObjKind::TradingPost) {
        what = "Торговий пост";
        o.materials_needed["wood"] = static_cast<int>(std::lround(b.trading_post_build_wood));
        o.build_total = b.trading_post_build_hours * 3600.0;
    } else if (kind == ObjKind::Lantern) {
        what = "Ліхтар";
        o.materials_needed["wood"] = static_cast<int>(std::lround(b.lantern_build_wood));
        o.build_total = b.lantern_build_hours * 3600.0;
    } else if (kind == ObjKind::Board) {
        what = "Дошка оголошень";
        o.materials_needed["wood"] = static_cast<int>(std::lround(b.board_build_wood));
        o.build_total = b.board_build_hours * 3600.0;
    } else {
        o.build_total = b.field_build_hours * 3600.0;
    }
    // The player's plan is announced to everyone: all know the new site as it is now.
    for (Character& c : chars_)
        if (c.alive) learn(c, o);
    std::string needs = describe(o.materials_needed);
    log(-1, LogKind::Info,
        "Нове креслення: " + what + " " + quote(name) + (needs.empty() ? "" : " (потрібно: " + needs + ")"));
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

// ---------------------------------------------------------------- limited knowledge

void World::learn(Character& c, const Object& o) {
    Knowledge& k = c.known[o.id];
    k.time = c.local_time;
    k.state = o;
    if (o.kind == ObjKind::Person) {
        const Character& p = chars_.at(o.char_index);
        k.person = PersonView{p.hunger,       p.fatigue,     p.health,          p.warmth,
                              p.alive,        p.tool,        p.clothes,         p.carrying,
                              p.illness_left, p.injury_left, p.illness_treated, p.injury_treated,
                              p.mood};
    }
}

bool World::sees(const Character& c, const Object& o) const { return o.id != c.obj && at(c, o.pos); }

void World::observe(Character& c) {
    for (const Object& o : objects_)
        if (sees(c, o)) learn(c, o);
}

const Knowledge* World::knowledge(Character& viewer, int obj_id) {
    const Object* o = object(obj_id);
    if (!o) return nullptr;
    if (sees(viewer, *o)) learn(viewer, *o);
    auto it = viewer.known.find(obj_id);
    return it == viewer.known.end() ? nullptr : &it->second;
}

const std::map<int, Knowledge>& World::board_notes(int board_id) const {
    static const std::map<int, Knowledge> kNone;
    auto it = boards_.find(board_id);
    return it == boards_.end() ? kNone : it->second;
}

bool World::is_resource(const std::string& resource) const {
    return resource == "wood" || resource == "bread" || resource == "grain" || resource == "flour" ||
           resource == "hay" || resource == "milk" || resource == "eggs" || resource == "meat" ||
           resource == "clothes" || resource == "warm_clothes" || resource == "scrap" || resource == "flax" ||
           resource == "cloth" || resource == "rags" || resource == "planks" || resource == "bed" || resource == "stone" ||
           resource == "ore" || resource == "charcoal" || resource == "iron" || resource == "water" ||
           resource == "herbs" || resource == "bandage" || resource == "oil" || resource == "torch" ||
           resource == "smoked_meat" || resource == "cheese" || resource == "sapling" ||
           resource == "beer";
}

double World::food_relief(const std::string& resource) const {
    if (resource == "bread") return balance_.bread_hunger_relief;
    if (resource == "meat") return balance_.meat_hunger_relief;
    if (resource == "milk") return balance_.milk_hunger_relief;
    if (resource == "eggs") return balance_.eggs_hunger_relief;
    if (resource == "smoked_meat") return balance_.smoked_meat_hunger_relief;
    if (resource == "cheese") return balance_.cheese_hunger_relief;
    return 0;
}

double World::resource_kg(const std::string& resource) const {
    if (resource == "wood") return balance_.wood_kg;
    if (resource == "bread") return balance_.bread_kg;
    if (resource == "grain") return balance_.grain_kg;
    if (resource == "flour") return balance_.flour_kg;
    if (resource == "hay") return balance_.hay_kg;
    if (resource == "milk") return balance_.milk_kg;
    if (resource == "eggs") return balance_.eggs_kg;
    if (resource == "meat") return balance_.meat_kg;
    if (resource == "clothes" || resource == "warm_clothes") return balance_.clothes_kg;
    if (resource == "scrap") return balance_.scrap_kg;
    if (resource == "flax") return balance_.flax_kg;
    if (resource == "cloth") return balance_.cloth_kg;
    if (resource == "rags") return balance_.rags_kg;
    if (resource == "planks") return balance_.planks_kg;
    if (resource == "bed") return balance_.bed_kg;
    if (resource == "stone") return balance_.stone_kg;
    if (resource == "ore") return balance_.ore_kg;
    if (resource == "charcoal") return balance_.charcoal_kg;
    if (resource == "iron") return balance_.iron_kg;
    if (resource == "water") return balance_.water_kg;
    if (resource == "herbs") return balance_.herbs_kg;
    if (resource == "bandage") return balance_.bandage_kg;
    if (resource == "oil") return balance_.oil_kg;
    if (resource == "torch") return balance_.torch_kg;
    if (resource == "smoked_meat") return balance_.smoked_meat_kg;
    if (resource == "cheese") return balance_.cheese_kg;
    if (resource == "sapling") return balance_.sapling_kg;
    if (resource == "beer") return balance_.beer_kg;
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

bool World::has_bed(const Character& c) const {
    if (c.home < 0) return false;
    int before = 0;  // housemates who came first get the beds first
    for (const Character& o : chars_) {
        if (&o == &c) break;
        before += o.alive && o.home == c.home ? 1 : 0;
    }
    return before < objects_[c.home].beds;
}

int World::animals_count(int barn_id) const {
    int n = 0;
    for (const Object& o : objects_) n += o.kind == ObjKind::Animal && o.alive && !o.removed && o.home == barn_id ? 1 : 0;
    return n;
}

int World::forest_trees_left(const Object& f) const {
    int used = static_cast<int>(std::floor(f.wood_taken / balance_.wood_per_tree + kEps));
    return std::max(0, f.trees_total - used);
}

int World::forest_room(const Object& f) const {
    // A tree being chopped still stands: count it until its last unit is taken.
    const int standing = static_cast<int>(std::ceil(forest_units_left(f) / balance_.wood_per_tree - kEps));
    return std::max(0, f.capacity - standing - static_cast<int>(f.saplings.size()));
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

const Object* World::shelter_at(const Character& c) const {
    for (const Object& o : objects_)
        if (o.built && has_stove(o) && at(c, o.pos)) return &o;
    return nullptr;
}

bool World::has_stove(const Object& o) const {
    if (o.kind == ObjKind::House) return true;
    if (o.kind != ObjKind::Workshop) return false;
    auto t = balance_.workshops.find(o.workshop);
    return t != balance_.workshops.end() && t->second.stove;
}

const Object* World::nearest_church(Vec2 from) const {
    const Object* best = nullptr;
    double best_d = 0;
    for (const Object& o : objects_) {
        if (o.kind != ObjKind::Workshop || o.workshop != "church" || !o.built) continue;
        const double d = std::hypot(o.pos.x - from.x, o.pos.y - from.y);
        if (!best || d < best_d) best = &o, best_d = d;
    }
    return best;
}

bool World::lit_near(const Character& c) const {
    if (!is_night_at(c.local_time)) return true;
    const Vec2 p = objects_[c.obj].pos;
    for (const Object& o : objects_)
        if (o.kind == ObjKind::Lantern && o.lit && std::hypot(o.pos.x - p.x, o.pos.y - p.y) <= balance_.lantern_radius_m)
            return true;
    return false;
}

// Heavy work at night needs light: a lit lantern nearby, or a torch in the hands (it burns while working).
bool World::works_in_dark(Character& c, double hours, Activity a) {
    if (a != Activity::Heavy || lit_near(c)) return false;
    if (c.torch_left <= 0) {
        auto it = c.carrying.find("torch");
        if (it == c.carrying.end() || it->second < 1) return true;
        if (--it->second == 0) c.carrying.erase(it);
        c.torch_left = balance_.torch_hours;
    }
    c.torch_left = std::max(0.0, c.torch_left - hours);
    return false;
}

double World::spoil_per_day(const std::string& food) const {
    const Balance& b = balance_;
    if (food == "bread") return b.bread_spoil_per_day;
    if (food == "milk") return b.milk_spoil_per_day;
    if (food == "eggs") return b.eggs_spoil_per_day;
    if (food == "meat") return b.meat_spoil_per_day;
    if (food == "smoked_meat") return b.smoked_meat_spoil_per_day;
    if (food == "cheese") return b.cheese_spoil_per_day;
    return 0;  // grain, flour, hay and the rest keep
}

// Food in a storage or at a trading post spoils: a share of the stock a day; slower in winter and in a cellar.
void World::spoil(Object& o, double t, double dt) {
    const Balance& b = balance_;
    const double factor = (season_at(t) == Season::Winter ? b.winter_spoil_factor : 1.0) * (o.cellar ? b.cellar_spoil_factor : 1.0);
    for (auto* pile : {&o.resources, &o.for_sale}) {
        for (auto& [food, n] : *pile) {
            const double rate = spoil_per_day(food);
            if (rate <= 0 || n <= 0) continue;
            double& p = o.spoil[food];
            p += n * rate * factor * dt / kDay;
            while (p >= 1.0 && n > 0) {
                p -= 1.0;
                --n;
                ++o.spoiled[food];
            }
        }
    }
}

std::vector<std::string> World::diet(const Character& c) const {
    std::vector<std::string> foods;
    for (const auto& [food, when] : c.ate)
        if (when >= c.local_time - balance_.diet_days * kDay) foods.push_back(food);
    return foods;
}

bool World::is_body(const Object& o) const {
    if (o.kind == ObjKind::Person) {
        const Character& c = chars_[o.char_index];
        return !c.alive && !c.buried;
    }
    return o.kind == ObjKind::Animal && !o.alive && !o.removed;
}

const Object* World::workshop_at(const Character& c, const std::string& type) const {
    for (const Object& o : objects_)
        if (o.kind == ObjKind::Workshop && o.workshop == type && o.built && at(c, o.pos)) return &o;
    return nullptr;
}

bool is_skill(const std::string& skill) {
    for (const char* s : kSkills)
        if (skill == s) return true;
    return false;
}

double World::skill_of(const Character& c, const std::string& skill) const {
    auto it = c.skills.find(skill);
    return it == c.skills.end() ? 0.0 : it->second;
}

double World::skill_factor(const Character& c, const std::string& skill) const {
    return 1.0 + balance_.skill_speed_bonus * skill_of(c, skill) / 100.0;
}

int World::craft_quality(const Character& c) const {
    const double s = skill_of(c, "crafting");
    return 1 + (s >= balance_.tool_quality2_skill - kEps ? 1 : 0) + (s >= balance_.tool_quality3_skill - kEps ? 1 : 0);
}

bool World::has_mentor(const Character& c, const std::string& skill) const {
    const double mine = skill_of(c, skill);
    const Vec2 p = objects_[c.obj].pos;
    for (const Character& o : chars_) {
        if (&o == &c || !o.alive || o.practicing != skill || std::abs(o.practice_until - c.local_time) > 600.0) continue;
        if (skill_of(o, skill) < mine + balance_.mentor_gap - kEps) continue;
        const Vec2 q = objects_[o.obj].pos;
        if (std::hypot(q.x - p.x, q.y - p.y) <= balance_.mentor_distance_m) return true;
    }
    return false;
}

void World::practice(Character& c, const std::string& skill, double seconds) {
    if (seconds <= 0) return;
    const Balance& b = balance_;
    double& s = c.skills[skill];
    double gain = b.skill_gain_per_hour * seconds / 3600.0 * (1.0 - s / 100.0);
    if (has_mentor(c, skill)) gain *= b.mentor_factor;
    s = std::min(100.0, s + gain);
    c.practicing = skill;
    c.practice_until = c.local_time;
}

const Object* World::hospital_at(const Character& c) const {
    for (const Object& o : objects_)
        if (o.kind == ObjKind::Workshop && o.workshop == "hospital" && o.built && at(c, o.pos)) return &o;
    return nullptr;
}

double World::work_factor(const Character& c, bool outdoors, const std::string& skill) const {
    const Balance& b = balance_;
    double f = skill_factor(c, skill);
    if (outdoors && is_raining_at(c.local_time)) f *= b.rain_work_factor;
    if (c.fatigue >= b.fatigue_slow_threshold) f *= b.fatigue_slow_factor;
    if (c.illness_left > 0 && !c.illness_treated) f *= b.illness_work_factor;
    if (c.injury_left > 0) f *= b.injury_work_factor;
    if (c.mood < b.mood_low_threshold) f *= b.mood_low_work_factor;
    if (c.hangover_until > c.local_time) f *= b.hangover_work_factor;
    return f;
}

bool World::chance(double expected) {
    return std::uniform_real_distribution<double>(0.0, 1.0)(health_rng_) < 1.0 - std::exp(-expected);
}

// The weakened fall ill (cold, hunger, exhaustion, rain without a roof); heavy work when tired, at night,
// in the mine or at the forge risks an injury. Chances are per hour; no draws while nothing threatens.
void World::catch_ailments(Character& c, double hours, Activity a, double danger) {
    const Balance& b = balance_;
    const bool dark = works_in_dark(c, hours, a);
    if (c.illness_left <= 0) {
        double rate = 0;
        if (c.warmth < b.warmth_damage_threshold) rate += b.illness_cold_per_hour;
        if (c.hunger >= b.hunger_damage_threshold) rate += b.illness_hunger_per_hour;
        if (c.fatigue >= b.fatigue_slow_threshold) rate += b.illness_exhaustion_per_hour;
        if (is_raining_at(c.local_time) && !shelter_at(c)) rate += b.illness_rain_per_hour;
        rate += b.illness_body_per_hour * decaying_bodies_;
        if (c.clean_until > c.local_time) rate *= b.bath_illness_factor;
        if (rate > 0 && chance(rate * hours)) {
            c.illness_left = b.illness_hours;
            c.illness_treated = false;
            log(c.obj, LogKind::Warning,
                quote(c.name) + ": хвороба. Лежати в теплі (me.rest), вилікувати — у лікарні травами (me.treat)");
        }
    }
    if (a == Activity::Heavy && c.injury_left <= 0) {
        double rate = danger;
        if (c.fatigue >= b.injury_fatigue_threshold) rate += b.injury_fatigue_per_hour;
        if (dark) rate += b.injury_dark_per_hour;
        if (c.hangover_until > c.local_time) rate += b.injury_hangover_per_hour;
        if (!c.practicing.empty() && c.practice_until >= c.local_time - 600.0)  // the experienced get hurt less
            rate *= 1.0 - b.skill_injury_reduction * skill_of(c, c.practicing) / 100.0;
        if (rate > 0 && chance(rate * hours)) {
            c.injury_left = b.injury_hours;
            c.injury_treated = false;
            log(c.obj, LogKind::Warning,
                quote(c.name) + ": травма на роботі — праця вдвічі повільніша; перев'язати бинтом — у лікарні (me.treat)");
        }
    }
}

double World::environment_temperature(const Character& c) const {
    const Object* shelter = shelter_at(c);
    return shelter ? shelter->temperature : outdoor_temperature(c.local_time);
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
    if (!merchant_.here && merchant_.next_arrival < t1) merchant_arrives(std::max(t0, merchant_.next_arrival), events);
    if (merchant_.here && merchant_.leaves < t1) {
        const double t = std::max(t0, merchant_.leaves);
        merchant_trades(t);
        merchant_.here = false;
        events.push_back("MERCHANT_LEFT");
        plan_visit(merchant_.next_arrival + std::max(1.0, balance_.merchant_interval_days) * kDay);
        log(-1, LogKind::Info,
            "Купець поїхав; наступного разу приїде на " + std::to_string(int(std::floor(merchant_.next_arrival / kDay)) + 1) +
                "-й день",
            {}, t);
    }
    update_environment(t0, t1);
    return events;
}

// ---------------------------------------------------------------- statistics

// Samples come at the end of a tick, when every character has been simulated up to t1.
void World::end_tick(double t1) {
    constexpr double kHour = 3600.0;
    if (stats_.empty()) record_stats(now_);
    const double hour = std::floor(t1 / kHour + kEps) * kHour;
    if (hour > now_ + kEps) record_stats(hour);
    now_ = t1;
}

void World::record_stats(double t) {
    StatSample s;
    s.time = t;
    for (const Object& o : objects_) {
        if (o.kind == ObjKind::Storage && o.built)
            for (const auto& [res, n] : o.resources) s.stock[res] += n;
        if (o.kind == ObjKind::TownHall) s.coins += o.coins;
        if (o.kind == ObjKind::Animal && o.alive && !o.removed) ++s.animals;
    }
    for (const Character& c : chars_) {
        if (!c.alive) continue;
        ++s.people;
        s.health += c.health;
        s.hunger += c.hunger;
        s.warmth += c.warmth;
        s.fatigue += c.fatigue;
        s.mood += c.mood;
    }
    if (s.people > 0) {
        s.health /= s.people;
        s.hunger /= s.people;
        s.warmth /= s.people;
        s.fatigue /= s.people;
        s.mood /= s.people;
    }
    stats_.push_back(std::move(s));
}

// ---------------------------------------------------------------- trade

Object* World::town_hall() {
    for (Object& o : objects_)
        if (o.kind == ObjKind::TownHall) return &o;
    return nullptr;
}

Object* World::barn_with_place() {
    for (Object& o : objects_)
        if (o.kind == ObjKind::Barn && o.built && animals_count(o.id) < o.capacity) return &o;
    return nullptr;
}

// Prices of the visit: base values +- spread; he pays a share of the value for what he buys.
void World::plan_visit(double arrival) {
    const Balance& b = balance_;
    merchant_.next_arrival = arrival;
    merchant_.leaves = std::floor(arrival / kDay) * kDay + b.merchant_leave_hour * 3600.0;
    merchant_.prices.clear();
    merchant_.offers.clear();
    const std::map<std::string, double> values = {
        {"wood", b.value_wood},       {"grain", b.value_grain},   {"flour", b.value_flour},
        {"bread", b.value_bread},     {"hay", b.value_hay},       {"milk", b.value_milk},
        {"eggs", b.value_eggs},       {"meat", b.value_meat},     {"clothes", b.value_clothes},
        {"warm_clothes", b.value_warm_clothes}, {"axe", b.value_axe}, {"sickle", b.value_sickle},
        {"scythe", b.value_scythe},   {"chicken", b.value_chicken}, {"cow", b.value_cow},
        {"scrap", b.value_scrap},   {"flax", b.value_flax},     {"cloth", b.value_cloth},
        {"rags", b.value_rags},     {"planks", b.value_planks}, {"bed", b.value_bed},
        {"stone", b.value_stone},   {"ore", b.value_ore},       {"charcoal", b.value_charcoal},
        {"iron", b.value_iron},     {"pickaxe", b.value_pickaxe}, {"herbs", b.value_herbs},
        {"bandage", b.value_bandage}, {"oil", b.value_oil}, {"torch", b.value_torch},
        {"smoked_meat", b.value_smoked_meat}, {"cheese", b.value_cheese}, {"sapling", b.value_sapling}, {"beer", b.value_beer},
    };
    std::uniform_real_distribution<double> spread(-b.merchant_price_spread, b.merchant_price_spread);
    for (const auto& [item, value] : values) {
        merchant_.offers[item] = std::max(1, static_cast<int>(std::lround(value * b.merchant_offer_share * (1.0 + spread(trade_rng_)))));
        if (item == "chicken" || item == "cow") merchant_.offers.erase(item);  // animals are not carried to the post
    }
    for (const char* item : {"grain", "bread", "cow", "chicken", "axe", "sickle", "scythe", "pickaxe", "warm_clothes", "scrap", "flax"})
        merchant_.prices[item] = std::max(1, static_cast<int>(std::lround(values.at(item) * (1.0 + spread(trade_rng_)))));
}

void World::merchant_arrives(double t, std::vector<std::string>& events) {
    const Object* post = nullptr;
    for (const Object& o : objects_)
        if (o.kind == ObjKind::TradingPost && o.built) {
            post = &o;
            break;
        }
    if (!post) {
        log(-1, LogKind::Info, "Купець проїхав повз: у поселенні немає торгового поста (його ставлять кресленням)", {}, t);
        plan_visit(merchant_.next_arrival + std::max(1.0, balance_.merchant_interval_days) * kDay);
        return;
    }
    merchant_.here = true;
    merchant_.post = post->id;
    events.push_back("MERCHANT_ARRIVED");
    log(-1, LogKind::Info,
        "Приїхав купець на " + quote(post->name) + "; торгує, коли від'їжджає о " +
            std::to_string(int(std::lround(balance_.merchant_leave_hour))) + ":00",
        {}, t);
}

// When he leaves: buys everything for sale, then fulfils the orders while the town hall can pay.
void World::merchant_trades(double t) {
    Object* post = object(merchant_.post);
    Object* hall = town_hall();
    if (!post || post->kind != ObjKind::TradingPost || !hall) return;
    std::map<std::string, int> sold;
    int earned = 0;
    for (const auto& [item, n] : post->for_sale) {
        if (n <= 0) continue;
        auto offer = merchant_.offers.find(item);
        earned += offer == merchant_.offers.end() ? 0 : n * offer->second;
        sold[item] += n;
    }
    for (const ToolItem& tool : post->sale_tools) {
        earned += static_cast<int>(std::floor(merchant_.offers[tool.kind] * tool.condition / 100.0));
        sold[tool.kind]++;
    }
    post->for_sale.clear();
    post->sale_tools.clear();
    hall->coins += earned;
    if (!sold.empty())
        log(-1, LogKind::Info, "Купець купив на " + quote(post->name) + ": " + describe(sold) + " — +" + std::to_string(earned) + " монет", {}, t);

    std::map<std::string, int> delivered;
    int spent = 0;
    const auto orders = post->orders;  // a copy: a bought animal grows objects_, so `post` moves
    for (const auto& [item, wanted] : orders) {
        auto price = merchant_.prices.find(item);
        if (price == merchant_.prices.end()) continue;
        int n = 0;
        const char* lacking = nullptr;
        for (; n < wanted; ++n) {
            if (hall->coins < price->second) {
                lacking = "не вистачило монет в управі";
                break;
            }
            if (item == "cow" || item == "chicken") {
                Object* barn = barn_with_place();
                std::string error;
                if (!barn || add_animal(item, barn->id, error) < 0) {
                    lacking = "не вистачило місця в хлівах";
                    break;
                }
                post = object(merchant_.post);  // add_animal grows objects_
                hall = town_hall();
            } else if (is_tool(item)) {
                post->tools.push_back(make_tool(item));
            } else {
                post->resources[item] += 1;
            }
            hall->coins -= price->second;
            spent += price->second;
        }
        if (n > 0) delivered[item] += n;
        if (lacking)
            log(-1, LogKind::Warning,
                "Замовлення «" + resource_ua(item) + "» ×" + std::to_string(wanted) + ": привезено " + std::to_string(n) + " — " + lacking,
                {}, t);
    }
    post->orders.clear();
    if (!delivered.empty())
        log(-1, LogKind::Info,
            "Купець залишив на " + quote(post->name) + ": " + describe(delivered) + " — −" + std::to_string(spent) +
                " монет (в управі " + std::to_string(hall->coins) + ")",
            {}, t);
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
        if (!winter) winter_ended(day_start);
    }

    if (day_start > t0) {  // midnight: what spoiled over the day
        for (Object& o : objects_) {
            if (o.grown > 0) {
                log(-1, LogKind::Info, "У " + quote(o.name) + " за добу виросло дерев: " + std::to_string(o.grown), {}, day_start);
                o.grown = 0;
            }
            if (o.spoiled.empty()) continue;
            log(-1, LogKind::Info, "За добу зіпсувалося в " + quote(o.name) + ": " + describe(o.spoiled), {}, day_start);
            o.spoiled.clear();
        }
    }
    decaying_bodies_ = 0;
    const double decayed = t0 - b.body_decay_hours * 3600.0;
    for (const Character& c : chars_) decaying_bodies_ += !c.alive && !c.buried && c.death_time <= decayed ? 1 : 0;
    for (const Object& o : objects_) decaying_bodies_ += is_body(o) && o.kind == ObjKind::Animal && o.death_time <= decayed ? 1 : 0;

    for (Object& o : objects_) {
        if (!o.built) continue;  // blueprints neither burn wood nor grow
        if (o.kind == ObjKind::Storage || o.kind == ObjKind::TradingPost) spoil(o, t0, dt);
        if (o.kind == ObjKind::Forest && !o.saplings.empty() && season_at(t0) == Season::Warm) {
            for (double& g : o.saplings) g += dt / (std::max(0.01, b.tree_grow_days) * kDay);
            const auto grown = std::remove_if(o.saplings.begin(), o.saplings.end(), [](double g) { return g >= 1.0; });
            const int n = static_cast<int>(o.saplings.end() - grown);
            o.saplings.erase(grown, o.saplings.end());
            o.trees_total += n;
            o.grown += n;
        }
        if (o.kind == ObjKind::Lantern && o.lit) {
            if (!is_night_at(t0)) {
                o.lit = false;  // dawn: whoever lights it has to come again in the evening
            } else if ((o.fuel -= dt / (b.lantern_oil_hours * 3600.0)) <= kEps) {
                o.fuel = 0;
                o.lit = false;
                log(-1, LogKind::Info, "Ліхтар " + quote(o.name) + " згас — скінчилася олія", {}, t1);
            }
        }
        if (has_stove(o)) {
            const bool burning = o.firewood > 0;
            if (burning) {
                o.firewood = std::max(0.0, o.firewood - dt / (b.firewood_burn_hours * 3600.0));
                if (o.firewood <= kEps) {
                    o.firewood = 0;
                    log(-1, LogKind::Info, "Згасла піч у " + quote(o.name), {}, t1);
                }
            }
            double target = burning ? b.house_heated_temperature : unheated_house_temperature(t0);
            double tau = (target > o.temperature ? b.house_warmup_hours : b.house_cooldown_hours) * 3600.0;
            o.temperature += (target - o.temperature) * (1.0 - std::exp(-dt / std::max(1.0, tau)));
        } else if (o.kind == ObjKind::Field && o.field_state == FieldState::Sown && season_at(t0) == Season::Warm) {
            if (is_raining_at(t0)) o.watered_until = std::max(o.watered_until, t1 + b.field_rain_soak_hours * 3600.0);
            const double rate = o.watered_until > t0 ? 1.0 + b.field_water_growth_bonus : 1.0;
            o.growth += rate * dt / (std::max(0.01, b.field_grow_days) * kDay);
            if (o.growth >= 1.0) {
                o.growth = 1.0;
                o.field_state = FieldState::Ripe;
                o.grain_left = crop_yield(b, o.crop);
                log(-1, LogKind::Info, "Поле " + quote(o.name) + " дозріло", {}, t1);
            }
        } else if (o.kind == ObjKind::Meadow && season_at(t0) == Season::Warm) {
            o.hay = std::min(b.meadow_hay_max,
                             o.hay + b.meadow_hay_max * dt / (std::max(0.01, b.meadow_regrow_days) * kDay));
        } else if (o.kind == ObjKind::Animal && o.alive && !o.removed) {
            update_animal(o, t0, dt);
        }
    }
}

// Warm season: animals are let out to graze and are never hungry; in winter they live on the feed
// the characters bring. Hunger hurts, being fed heals; milk and eggs come only while fed.
void World::update_animal(Object& a, double t0, double dt) {
    const Balance& b = balance_;
    const double h = dt / 3600.0;
    if (season_at(t0) == Season::Warm)
        a.hunger = std::max(0.0, a.hunger - b.animal_graze_per_hour * h);
    else
        a.hunger = std::min(100.0, a.hunger + b.animal_hunger_per_hour * h);

    if (a.hunger >= b.animal_hunger_damage_threshold) {
        a.health -= b.animal_hunger_damage_per_hour * h;
        if (!a.hunger_warned) {
            a.hunger_warned = true;
            log(-1, LogKind::Warning, "Тварина " + quote(a.name) + " голодує — нагодуйте її (me.feed)", {}, t0 + dt);
        }
    } else if (a.hunger < b.animal_produce_max_hunger) {
        a.health = std::min(100.0, a.health + b.animal_health_regen_per_hour * h);
        a.hunger_warned = false;
        const bool cow = a.species == "cow";
        const double per_day = cow ? b.cow_milk_per_day : b.chicken_eggs_per_day;
        const double max = cow ? b.cow_milk_max : b.chicken_eggs_max;
        a.produce = std::min(max, a.produce + per_day * dt / kDay);
    }
    if (a.health <= 0) {
        a.health = 0;
        a.alive = false;
        a.death_cause = "голод";
        a.death_time = t0 + dt;
        log(-1, LogKind::Death, "Смерть тварини " + quote(a.name) + ": голод", {}, t0 + dt);
    }
}

double World::fatigue_rate(Activity a) const {
    switch (a) {
        case Activity::Heavy: return balance_.fatigue_heavy_per_hour;
        case Activity::Light: return balance_.fatigue_light_per_hour;
        case Activity::Rest: return balance_.fatigue_rest_per_hour;
        case Activity::SleepBed: return balance_.fatigue_sleep_bed_per_hour;
        case Activity::SleepHome: return balance_.fatigue_sleep_home_per_hour;
        case Activity::SleepGround: return balance_.fatigue_sleep_ground_per_hour;
        case Activity::Idle:
        case Activity::Wait: return 0;
    }
    return 0;
}

void World::pass_time(Character& c, double seconds, Activity a, double danger) {
    if (seconds <= 0 || !c.alive) return;
    const Balance& b = balance_;
    const double h = seconds / 3600.0;
    const bool lying = a == Activity::Rest || a == Activity::SleepBed || a == Activity::SleepHome || a == Activity::SleepGround;
    const bool ill = c.illness_left > 0 && !c.illness_treated;

    c.hunger = std::min(100.0, c.hunger + b.hunger_per_hour * h);
    double frate = fatigue_rate(a);
    if (ill && !lying) frate += b.illness_fatigue_per_hour;
    if (a == Activity::Heavy && is_raining_at(c.local_time)) frate += b.rain_extra_fatigue_per_hour;
    c.fatigue = std::clamp(c.fatigue + frate * h, 0.0, 100.0);

    // Warmth: perceived temperature = surroundings + clothes (or the bed) + heat of hard work.
    double perceived = environment_temperature(c);
    if (a == Activity::SleepHome || a == Activity::SleepBed)
        perceived += b.bed_bonus;
    else if (c.clothes.kind == "warm_clothes")
        perceived += b.warm_clothes_bonus;
    else if (c.clothes.kind == "clothes")
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
    if (ill) damage += lying ? b.illness_rest_damage_per_hour : b.illness_damage_per_hour;
    double regen = (!ill && c.hunger < b.health_regen_max_hunger && c.fatigue < b.health_regen_max_fatigue &&
                    c.warmth >= b.health_regen_min_warmth)
                       ? b.health_regen_per_hour
                       : 0.0;
    c.health = std::min(100.0, c.health + (regen - damage) * h);

    // Mood: work wears it down, free time and comfort lift it; unmet needs and decaying bodies hurt.
    double dmood = 0;
    switch (a) {
        case Activity::Heavy: dmood = b.mood_heavy_per_hour; break;
        case Activity::Light: dmood = b.mood_light_per_hour; break;
        case Activity::Rest:
            dmood = b.mood_rest_per_hour + (workshop_at(c, "tavern") ? b.tavern_rest_mood_per_hour : 0.0);
            break;
        case Activity::SleepBed: dmood = b.mood_bed_per_hour; break;
        case Activity::SleepGround: dmood = b.mood_ground_per_hour; break;
        default: break;
    }
    if (c.hunger >= b.hunger_damage_threshold) dmood += b.mood_hunger_per_hour;
    if (c.warmth < b.warmth_damage_threshold) dmood += b.mood_cold_per_hour;
    if (c.fatigue >= b.fatigue_slow_threshold) dmood += b.mood_exhaustion_per_hour;
    if (c.illness_left > 0 || c.injury_left > 0) dmood += b.mood_ailment_per_hour;
    dmood += b.mood_body_per_hour * decaying_bodies_;
    const size_t kinds = diet(c).size();
    if (kinds >= static_cast<size_t>(std::lround(b.diet_variety_kinds)))
        dmood += b.diet_variety_mood_per_hour;
    else if (kinds <= 1 && c.local_time >= b.diet_days * kDay)
        dmood += b.diet_monotony_mood_per_hour;
    c.mood = std::clamp(c.mood + dmood * h, 0.0, 100.0);
    c.local_time += seconds;

    if (a != Activity::Idle) {
        c.last_action_time = c.local_time;
        c.idle_warned = false;
    } else if (!c.idle_warned && c.local_time - c.last_action_time >= b.idle_log_hours * 3600.0 - kEps) {
        c.idle_warned = true;
        log(c.obj, LogKind::Warning, quote(c.name) + ": уже " + fmt_num(b.idle_log_hours) + " год без жодної дії");
    }

    wear_clothes(c, seconds);
    if (c.illness_left > 0 && (c.illness_left -= h) <= 0) {
        c.illness_left = 0;
        c.illness_treated = false;
        log(c.obj, LogKind::Info, quote(c.name) + ": хвороба минула");
    }
    if (c.injury_left > 0 && (c.injury_left -= h) <= 0) {
        c.injury_left = 0;
        c.injury_treated = false;
        log(c.obj, LogKind::Info, quote(c.name) + ": травма загоїлася");
    }
    catch_ailments(c, h, a, danger);
    check_thresholds(c);
    if (c.health <= 0) {
        c.health = 0;
        std::vector<std::string> causes;
        if (ill) causes.push_back("хвороба");
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
    if (!c.mood_warned && c.mood < b.mood_low_threshold) {
        c.mood_warned = true;
        log(c.obj, LogKind::Warning,
            quote(c.name) + ": поганий настрій (" + fmt_num(std::round(c.mood)) +
                ") — праця повільніша; допоможуть відпочинок (me.rest), ліжко, служба в церкві");
    } else if (c.mood_warned && c.mood > b.mood_low_threshold + 10) {
        c.mood_warned = false;
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
    if (alive_count() == 0) log(-1, LogKind::Outcome, "Усі персонажі загинули — поселення спорожніло", {}, c.local_time);
}

int World::alive_count() const {
    int n = 0;
    for (const Character& c : chars_) n += c.alive ? 1 : 0;
    return n;
}

void World::winter_ended(double t) {
    const int alive = alive_count();
    if (alive == 0) return;
    ++winters_survived_;
    const std::string survivors = std::to_string(alive) + " з " + std::to_string(chars_.size());
    if (winters_survived_ == 1)
        log(-1, LogKind::Outcome, "Першу зиму пережито! Вижило " + survivors, {}, t);
    else
        log(-1, LogKind::Info, "Пережито зим: " + std::to_string(winters_survived_) + ", живих " + survivors, {}, t);
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
    observe(c);  // a character sees where it stands whenever it starts an action
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
        if (!require(ObjKind::Storage) && !require(ObjKind::TradingPost))
            return fail("ActionError", "Інструмент можна взяти лише зі складу чи з торгового поста");
        if (unbuilt()) return fail_unbuilt();
        if (!is_tool(p.tool)) return fail("ActionError", "Невідомий інструмент");
        a.kind = ActionKind::Take;
        const ItemNames names = item_names(p.tool);
        if (!c.tool.empty()) {
            if (c.tool.kind == p.tool && !c.tool.broken() && (p.tool_id < 0 || p.tool_id == c.tool.id)) {
                a.noop = true;
            } else if (c.tool.broken()) {
                return fail("ActionError", "У руках зламаний інструмент: спершу поверніть його на склад (put_back)");
            } else {
                return fail("ActionError", "Слот інструмента зайнятий: спершу поверніть інструмент (put_back)");
            }
        } else {
            // A particular tool, or the best whole free one: higher quality, then better condition.
            ToolItem* pick = nullptr;
            if (p.tool_id >= 0) {
                pick = tool_in(*t, p.tool_id);
                if (!pick || pick->kind != p.tool)
                    return fail("NoItemError", "Цього інструмента вже немає на складі " + quote(t->name));
                if (pick->reserved_by >= 0 && pick->reserved_by != char_obj)
                    return fail("ReservedError", "Цей інструмент уже зарезервував інший персонаж");
            } else {
                bool any = false, whole = false;
                for (ToolItem& it : t->tools) {
                    if (it.kind != p.tool) continue;
                    any = true;
                    if (it.broken()) continue;
                    whole = true;
                    if (it.reserved_by >= 0) continue;
                    if (!pick || it.quality > pick->quality || (it.quality == pick->quality && it.condition > pick->condition))
                        pick = &it;
                }
                if (!any) return fail("NoItemError", "На складі " + quote(t->name) + " немає " + names.genitive);
                if (!whole) return fail("NoItemError", "На складі " + quote(t->name) + " лише зламані " + names.plural);
                if (!pick)
                    return fail("ReservedError",
                                std::string("Усі ") + names.plural + " на складі " + quote(t->name) + " зарезервували інші персонажі");
            }
            pick->reserved_by = char_obj;
            a.reserved = true;
            a.params.tool_id = pick->id;
        }
    } else if (kind == "put_back") {
        if (!require(ObjKind::Storage) && !require(ObjKind::TradingPost))
            return fail("ActionError", "Інструмент можна покласти лише на склад або на продаж на торговий пост");
        if (unbuilt()) return fail_unbuilt();
        if (c.tool.empty()) return fail("NoItemError", quote(c.name) + " не тримає інструмента");
        a.kind = ActionKind::PutBack;
    } else if (kind == "chop_wood") {
        if (!require(ObjKind::Forest)) return fail("ActionError", "Рубати дрова можна лише в лісі");
        if (c.tool.kind != "axe")
            return fail("NoItemError", "Щоб рубати дрова, потрібна сокира в руках (me.take(Axe, склад))");
        if (c.tool.broken())
            return fail("ToolBrokenError", "Сокира в руках зламана: поверніть її на склад (put_back) і візьміть іншу");
        if (carry_weight(c) + b.wood_kg > b.weight_limit_kg + kEps)
            return fail("TooHeavyError", "Руки повні: більше дров не піднести");
        if (forest_units_left(*t) <= 0) return fail("NoItemError", "У лісі " + quote(t->name) + " не лишилося дерев");
        a.kind = ActionKind::ChopWood;
    } else if (kind == "drop") {
        if (!require(ObjKind::Storage) && !require(ObjKind::TradingPost) && !require(ObjKind::House))
            return fail("ActionError", "Покласти речі можна на склад, на продаж на торговий пост або одяг і ліжка — у будинок");
        if (unbuilt()) return fail_unbuilt();
        if (t->kind == ObjKind::House) {
            if (carried("clothes") + carried("warm_clothes") + carried("bed") < 1)
                return fail("NoItemError", "У будинок кладуть лише одяг (у скриню) і ліжка, а " + quote(c.name) + " їх не несе");
            if (carried("clothes") + carried("warm_clothes") < 1 && t->beds >= t->capacity)
                return fail("ActionError", "У будинку " + quote(t->name) + " уже є ліжка на всіх: " + std::to_string(t->beds));
        }
        a.kind = ActionKind::Drop;
    } else if (kind == "pick_up") {
        if (!require(ObjKind::Storage) && !require(ObjKind::TradingPost))
            return fail("ActionError", "Брати ресурси можна лише зі складу чи з торгового поста (куплене)");
        if (unbuilt()) return fail_unbuilt();
        if (!is_resource(p.resource)) return fail("ActionError", "Невідомий ресурс " + quote(p.resource));
        if (p.count <= 0) return fail("ActionError", "Кількість має бути більшою за нуль");
        a.kind = ActionKind::PickUp;
    } else if (kind == "eat") {
        if (!is_resource(p.resource)) return fail("ActionError", "Невідомий ресурс " + quote(p.resource));
        if (food_relief(p.resource) <= 0) return fail("ActionError", quote(p.resource) + " не можна з'їсти");
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
        if (!t || !has_stove(*t)) return fail("ActionError", "Опалювати можна будинок, лікарню, лазню чи корчму");
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
        a.noop = c.clothes.kind == p.item;
    } else if (kind == "sow") {
        if (!require(ObjKind::Field)) return fail("ActionError", "Сіяти можна лише на полі");
        if (unbuilt()) return fail_unbuilt();
        if (season_at(now) == Season::Winter) return fail("ActionError", "Узимку земля замерзла — сіяти не можна");
        if (t->field_state == FieldState::Sown) return fail("ActionError", "Поле " + quote(t->name) + " уже засіяне");
        if (t->field_state == FieldState::Ripe)
            return fail("ActionError", "Поле " + quote(t->name) + " дозріло — спершу зберіть урожай (harvest)");
        if (!is_crop(p.resource)) return fail("ActionError", "Посіяти можна зерно (\"grain\") або льон (\"flax\")");
        if (t->seeded && t->crop != p.resource)
            return fail("ActionError", "Поле " + quote(t->name) + " уже засівають: " + resource_ua(t->crop));
        if (!t->seeded && carried(p.resource.c_str()) < crop_seed(b, p.resource))
            return fail("NoItemError", "Для посіву потрібно в руках: " + resource_ua(p.resource) + " " +
                                           std::to_string(crop_seed(b, p.resource)));
        a.kind = ActionKind::Sow;
    } else if (kind == "harvest") {
        if (!require(ObjKind::Field)) return fail("ActionError", "Жати можна лише на полі");
        if (unbuilt()) return fail_unbuilt();
        if (c.tool.kind != "sickle")
            return fail("NoItemError", "Для жнив потрібен серп у руках (me.take(Sickle, склад))");
        if (c.tool.broken())
            return fail("ToolBrokenError", "Серп у руках зламаний: поверніть його на склад (put_back) і візьміть інший");
        if (t->field_state != FieldState::Ripe || t->grain_left <= 0)
            return fail("NoItemError", "На полі " + quote(t->name) + " немає стиглого врожаю");
        if (carry_weight(c) + resource_kg(t->crop) > b.weight_limit_kg + kEps)
            return fail("TooHeavyError", "Руки повні: більше " + resource_ua(t->crop) + " не піднести");
        a.kind = ActionKind::Harvest;
    } else if (kind == "craft") {
        auto found = b.recipes.find(p.item);
        if (found == b.recipes.end()) return fail("ActionError", "Невідомий рецепт " + quote(resource_ua(p.item)));
        // The first recipe of this item made here whose inputs are all in the hands.
        int variant = -1;
        bool here = false;
        std::string places, needs;
        for (size_t i = 0; i < found->second.size(); ++i) {
            const Recipe& v = found->second[i];
            places += (places.empty() ? "" : ", ") + place_ua(b, v.at);
            if (!t || !makes_here(v, *t)) continue;
            here = true;
            needs += (needs.empty() ? "" : " або ") + describe(v.inputs);
            bool all = true;
            for (const auto& [res, n] : v.inputs) all = all && carried(res.c_str()) >= n;
            if (all && variant < 0) variant = static_cast<int>(i);
        }
        if (!here) return fail("ActionError", "«" + resource_ua(p.item) + "» роблять тут: " + places);
        if (unbuilt()) return fail_unbuilt();
        if (variant < 0) return fail("NoItemError", "Для «" + resource_ua(p.item) + "» потрібно в руках: " + needs);
        a.params.variant = variant;
        const Recipe& r = found->second[variant];
        if (is_tool(p.item) && !c.tool.empty())
            return fail("ActionError", "Руки зайняті інструментом: покладіть його (put_back), щоб узяти новий");
        if (r.fire && t->firewood <= 0)
            return fail("NoItemError", "Піч у будинку " + quote(t->name) + " холодна — спершу опаліть його (me.heat)");
        auto user = t->workplace_users.find(r.workplace);
        if (user != t->workplace_users.end() && user->second != char_obj)
            return fail("WorkplaceBusyError", std::string("Робоче місце «") + workplace_ua(r.workplace) + "» у " + quote(t->name) +
                                                  " зайняте: там працює " + quote(objects_[user->second].name));
        t->workplace_users[r.workplace] = char_obj;
        a.kind = ActionKind::Craft;
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
    } else if (kind == "mine") {
        if (!require(ObjKind::Deposit)) return fail("ActionError", "Добувати можна лише в кар'єрі чи рудні");
        if (c.tool.kind != "pickaxe")
            return fail("NoItemError", "Щоб добувати " + resource_ua(t->deposit) + ", потрібне кайло в руках (me.take(Pickaxe, склад))");
        if (c.tool.broken())
            return fail("ToolBrokenError", "Кайло в руках зламане: поверніть його на склад (put_back) і візьміть інше");
        if (carry_weight(c) + resource_kg(t->deposit) > b.weight_limit_kg + kEps)
            return fail("TooHeavyError", "Руки повні: більше " + resource_ua(t->deposit) + " не піднести");
        if (t->deposit_left <= 0) return fail("NoItemError", quote(t->name) + " вичерпано");
        a.kind = ActionKind::Mine;
    } else if (kind == "gather_herbs") {
        if (!require(ObjKind::Meadow)) return fail("ActionError", "Трави збирають на лузі");
        if (season_at(now) == Season::Winter) return fail("ActionError", "Узимку трав немає — запасіть їх у теплу пору");
        if (carry_weight(c) + b.herbs_kg > b.weight_limit_kg + kEps)
            return fail("TooHeavyError", "Руки повні: більше трав не піднести");
        a.kind = ActionKind::GatherHerbs;
    } else if (kind == "treat") {
        if (!require(ObjKind::Person)) return fail("ActionError", "Лікувати можна лише людей");
        const Character& p = character(target);
        if (!p.alive) return fail("ActionError", "Смерть персонажа " + quote(p.name) + " — лікувати пізно");
        const bool ill = p.illness_left > 0 && !p.illness_treated, hurt = p.injury_left > 0 && !p.injury_treated;
        if (!ill && !hurt) return fail("ActionError", quote(p.name) + " не потребує лікування");
        const int herbs = static_cast<int>(std::lround(b.treat_herbs));
        if (!(ill && carried("herbs") >= herbs) && !(hurt && carried("bandage") >= 1)) {
            std::string need = ill ? "трави " + std::to_string(herbs) : "";
            if (hurt) need += std::string(ill ? " і " : "") + "бинт 1";
            return fail("NoItemError", "Щоб лікувати " + quote(p.name) + ", потрібно в руках: " + need);
        }
        a.kind = ActionKind::Treat;
    } else if (kind == "teach") {
        if (!t || t->kind != ObjKind::Workshop || t->workshop != "school") return fail("ActionError", "Уроки проводять у школі");
        if (unbuilt()) return fail_unbuilt();
        if (!is_skill(p.item)) return fail("ActionError", "Невідома навичка " + quote(p.item));
        if (skill_of(c, p.item) < b.teach_min_skill - kEps)
            return fail("ActionError", quote(c.name) + " не може навчати: для уроку «" + skill_ua(p.item) + "» потрібна навичка від " +
                                           fmt_num(b.teach_min_skill));
        a.kind = ActionKind::Teach;
    } else if (kind == "bathe") {
        if (!t || t->kind != ObjKind::Workshop || t->workshop != "bathhouse") return fail("ActionError", "Митися можна в лазні");
        if (unbuilt()) return fail_unbuilt();
        if (c.last_bath >= 0 && now < c.last_bath + b.bath_interval_hours * 3600.0 - kEps)
            return fail("ActionError", quote(c.name) + " уже милися сьогодні");
        const int water = static_cast<int>(std::lround(b.bath_water));
        if (carried("water") < water)
            return fail("NoItemError", "Для лазні потрібно " + std::to_string(water) + " відра води в руках (me.draw_water(колодязь))");
        if (t->firewood <= 0) return fail("NoItemError", "Лазня " + quote(t->name) + " холодна — спершу натопіть її (me.heat)");
        a.kind = ActionKind::Bathe;
    } else if (kind == "drink") {
        if (!t || t->kind != ObjKind::Workshop || t->workshop != "tavern") return fail("ActionError", "Пиво п'ють у корчмі");
        if (unbuilt()) return fail_unbuilt();
        if (carried("beer") < 1) return fail("NoItemError", "У руках немає пива — його варять у пивоварні");
        a.kind = ActionKind::Drink;
    } else if (kind == "plant") {
        if (!require(ObjKind::Forest)) return fail("ActionError", "Саджанці садять у лісі");
        if (season_at(now) == Season::Winter) return fail("ActionError", "Узимку земля замерзла — садити не можна");
        if (carried("sapling") < 1)
            return fail("NoItemError", "У руках немає саджанців — їх вирощують у лісорозсаднику (me.craft(\"sapling\", ...))");
        if (forest_room(*t) <= 0)
            return fail("ActionError", "У лісі " + quote(t->name) + " немає вільного місця: спершу вирубайте дерева");
        a.kind = ActionKind::Plant;
    } else if (kind == "light") {
        if (!require(ObjKind::Lantern)) return fail("ActionError", "Запалити можна ліхтар");
        if (unbuilt()) return fail_unbuilt();
        if (p.amount && *p.amount <= 0) return fail("ActionError", "amount= має бути більшим за нуль");
        if (carried("oil") < 1 && t->fuel <= kEps)
            return fail("NoItemError", "У ліхтарі " + quote(t->name) + " немає олії — принесіть її (me.pick_up(склад, \"oil\", N))");
        a.kind = ActionKind::Light;
    } else if (kind == "hold_service") {
        if (!t || t->kind != ObjKind::Workshop || t->workshop != "church") return fail("ActionError", "Служби правлять у церкві");
        if (unbuilt()) return fail_unbuilt();
        const double next = t->last_service + b.service_interval_hours * 3600.0;
        if (t->last_service >= 0 && now < next - kEps)
            return fail("ActionError", "У " + quote(t->name) + " службу вже правили — наступну можна через " +
                                           fmt_num(std::ceil((next - now) / 3600.0)) + " год");
        a.kind = ActionKind::HoldService;
    } else if (kind == "bury") {
        if (!t || (t->kind != ObjKind::Person && t->kind != ObjKind::Animal))
            return fail("ActionError", "Ховати можна померлих людей і тварин");
        if (!is_body(*t)) {
            const bool alive = t->kind == ObjKind::Person ? character(target).alive : t->alive;
            return fail("ActionError", quote(t->name) + (alive ? " живе — ховати не можна" : " уже поховано"));
        }
        const Object* church = nearest_church(t->pos);
        if (!church) return fail("ActionError", "Ховають на кладовищі при церкві — спершу збудуйте церкву");
        a.place = church->id;
        a.kind = ActionKind::Bury;
    } else if (kind == "water") {
        if (!require(ObjKind::Field)) return fail("ActionError", "Поливати можна лише поле");
        if (unbuilt()) return fail_unbuilt();
        if (season_at(now) == Season::Winter) return fail("ActionError", "Узимку поле не росте — поливати не треба");
        if (t->field_state == FieldState::Ripe)
            return fail("ActionError", "Поле " + quote(t->name) + " дозріло — поливати не треба");
        const int need = static_cast<int>(std::lround(b.field_water_units));
        if (carried("water") < need)
            return fail("NoItemError", "Щоб полити поле, потрібно " + std::to_string(need) +
                                           " відер води в руках (me.draw_water(колодязь))");
        a.kind = ActionKind::Water;
    } else if (kind == "mow") {
        if (!require(ObjKind::Meadow)) return fail("ActionError", "Косити можна лише на лузі");
        if (c.tool.kind != "scythe")
            return fail("NoItemError", "Щоб косити, потрібна коса в руках (me.take(Scythe, склад))");
        if (c.tool.broken())
            return fail("ToolBrokenError", "Коса в руках зламана: поверніть її на склад (put_back) і візьміть іншу");
        if (season_at(now) == Season::Winter) return fail("ActionError", "Узимку трава під снігом — косити не можна");
        if (carry_weight(c) + b.hay_kg > b.weight_limit_kg + kEps)
            return fail("TooHeavyError", "Руки повні: більше сіна не піднести");
        a.kind = ActionKind::Mow;
    } else if (kind == "feed" || kind == "milk" || kind == "collect_eggs" || kind == "slaughter") {
        if (!require(ObjKind::Animal) || t->removed) return fail("ActionError", "Це не тварина поселення");
        if (!t->alive) return fail("ActionError", "Тварина " + quote(t->name) + " мертва");
        const bool cow = t->species == "cow";
        if (kind == "feed") {
            const char* feed = cow ? "hay" : "grain";
            if (carried(feed) < 1)
                return fail("NoItemError", std::string("Щоб нагодувати: ") + (cow ? "корова їсть сіно" : "курка їсть зерно") +
                                               " — візьміть його в руки (pick_up)");
            if (p.amount && *p.amount <= 0) return fail("ActionError", "amount= має бути більшим за нуль");
            a.kind = ActionKind::Feed;
        } else if (kind == "milk") {
            if (!cow) return fail("ActionError", "Доїти можна лише корову, а " + quote(t->name) + " — курка");
            a.kind = ActionKind::Milk;
        } else if (kind == "collect_eggs") {
            if (cow) return fail("ActionError", "Яйця несуть кури, а " + quote(t->name) + " — корова");
            a.kind = ActionKind::CollectEggs;
        } else {
            const int meat = static_cast<int>(std::lround(cow ? b.cow_meat : b.chicken_meat));
            if (carry_weight(c) + meat * b.meat_kg > b.weight_limit_kg + kEps)
                return fail("TooHeavyError", "Руки зайняті: м'ясо (" + std::to_string(meat) + " од.) не вміститься");
            a.kind = ActionKind::Slaughter;
        }
    } else if (kind == "dismantle") {
        if (c.tool.empty()) return fail("NoItemError", quote(c.name) + " не тримає інструмента, щоб його розібрати");
        const auto [scrap, wood] = dismantle_yield(b, c.tool.kind);
        if (carry_weight(c) + scrap * b.scrap_kg + wood * b.wood_kg > b.weight_limit_kg + kEps)
            return fail("TooHeavyError", "Руки зайняті: брухт і дерево з інструмента не вмістяться");
        a.kind = ActionKind::Dismantle;
    } else if (kind == "order") {
        if (!require(ObjKind::TradingPost)) return fail("ActionError", "Замовлення залишають на торговому посту");
        if (unbuilt()) return fail_unbuilt();
        if (!merchant_sells(p.item)) return fail("ActionError", "Купець не продає " + quote(resource_ua(p.item)));
        if (p.count <= 0) return fail("ActionError", "Кількість має бути більшою за нуль");
        a.kind = ActionKind::Order;
    } else if (kind == "post" || kind == "read") {
        if (!require(ObjKind::Board)) return fail("ActionError", "Писати й читати записи можна лише біля дошки оголошень");
        if (unbuilt()) return fail_unbuilt();
        for (int id : p.objects)
            if (!object(id)) return fail("NotFoundError", "Такого об'єкта не існує");
        a.kind = kind == "post" ? ActionKind::Post : ActionKind::Read;
    } else {
        return fail("ActionError", "Невідома дія " + quote(kind));
    }

    switch (a.kind) {
        case ActionKind::ChopWood:
        case ActionKind::Harvest:
        case ActionKind::Mow:
        case ActionKind::Mine:
        case ActionKind::GatherHerbs:
        case ActionKind::Plant:
        case ActionKind::Craft:
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
    StepResult r = step_action_impl(a, c, budget);
    if (r.status != StepStatus::Running && c.alive) observe(c);  // ...and when it finishes one
    return r;
}

StepResult World::step_action_impl(Action& a, Character& c, double budget) {
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
            auto it = std::find_if(s.tools.begin(), s.tools.end(), [&](const ToolItem& t) { return t.id == a.params.tool_id; });
            a.reserved = false;
            if (it == s.tools.end()) return failed(used, "NoItemError", "Цього інструмента вже немає на складі " + quote(s.name));
            c.tool = *it;
            c.tool.reserved_by = -1;
            s.tools.erase(it);
            return done(used);
        }
        case ActionKind::Teach: {
            Object& school = objects_[a.target];
            double used = walk(c, school.pos, budget);
            if (!at(c, school.pos)) return running(used);
            bool fin = false;
            used += handle(c, a, b.teach_minutes * 60.0, budget - used, Activity::Light, fin);
            if (!fin) return running(used);
            const std::string& skill = a.params.item;
            const double cap = skill_of(c, skill) * b.lesson_max_share;
            int students = 0;
            for (Character& p : chars_) {
                if (&p == &c || !p.alive || !at(p, school.pos)) continue;
                double& s = p.skills[skill];
                if (s < cap) s = std::min(cap, s + b.lesson_gain);
                ++students;
            }
            log(c.obj, LogKind::Info,
                quote(c.name) + ": урок «" + skill_ua(skill) + "» у " + quote(school.name) + ", учнів " + std::to_string(students));
            return done_int(used, students);
        }
        case ActionKind::Bathe:
        case ActionKind::Drink: {
            Object& place = objects_[a.target];
            double used = walk(c, place.pos, budget);
            if (!at(c, place.pos)) return running(used);
            const bool bath = a.kind == ActionKind::Bathe;
            bool fin = false;
            used += handle(c, a, (bath ? b.bath_minutes : b.drink_minutes) * 60.0, budget - used, Activity::Rest, fin);
            if (!fin) return running(used);
            const std::string what = bath ? "water" : "beer";
            const int n = bath ? static_cast<int>(std::lround(b.bath_water)) : 1;
            auto it = c.carrying.find(what);
            if (it == c.carrying.end() || it->second < n)
                return failed(used, "NoItemError", quote(c.name) + " уже не несе " + resource_ua(what));
            if ((it->second -= n) == 0) c.carrying.erase(it);
            if (bath) {
                c.fatigue = std::max(0.0, c.fatigue - b.bath_fatigue);
                c.mood = std::min(100.0, c.mood + b.bath_mood);
                c.clean_until = c.local_time + b.bath_clean_hours * 3600.0;
                c.last_bath = c.local_time;
                return done(used);
            }
            c.mood = std::min(100.0, c.mood + b.beer_mood);
            c.hunger = std::max(0.0, c.hunger - b.beer_hunger_relief);
            const int day = static_cast<int>(std::floor(c.local_time / kDay));
            if (day != c.drink_day) c.drink_day = day, c.beers = 0;
            if (++c.beers >= static_cast<int>(std::lround(b.hangover_beers))) {
                if (c.hangover_until <= c.local_time)
                    log(c.obj, LogKind::Warning, quote(c.name) + ": забагато пива — завтра похмілля (праця повільніша, травми частіші)");
                c.hangover_until = c.local_time + b.hangover_hours * 3600.0;
            }
            return done_int(used, c.beers);
        }
        case ActionKind::Plant: {
            Object& forest = objects_[a.target];
            double used = walk(c, forest.pos, std::min(budget, a.deadline - c.local_time));
            if (!at(c, forest.pos)) return c.local_time >= a.deadline - kEps ? done_int(used, a.done) : running(used);
            for (;;) {
                if (a.params.amount && a.done >= *a.params.amount) return done_int(used, a.done);
                if (c.local_time >= a.deadline - kEps) return done_int(used, a.done);
                auto it = c.carrying.find("sapling");
                if (it == c.carrying.end() || it->second < 1 || forest_room(forest) <= 0) {
                    if (a.done > 0) return done_int(used, a.done);
                    return failed(used, "ActionError", "Посадити не вдалося: немає саджанців чи місця", 0);
                }
                bool fin = false;
                const double t = handle(c, a, b.plant_minutes * 60.0, std::min(budget - used, a.deadline - c.local_time),
                                        Activity::Heavy, fin);
                used += t;
                practice(c, "woodcutting", t);
                if (!fin) return running(used);
                a.progress = 0;
                if (--it->second == 0) c.carrying.erase(it);
                forest.saplings.push_back(0.0);
                ++a.done;
            }
        }
        case ActionKind::Light: {
            Object& lantern = objects_[a.target];
            double used = walk(c, lantern.pos, budget);
            if (!at(c, lantern.pos)) return running(used);
            bool fin = false;
            used += handle(c, a, b.light_minutes * 60.0, budget - used, Activity::Light, fin);
            if (!fin) return running(used);
            const int have = c.carrying.count("oil") ? c.carrying["oil"] : 0;
            const int n = a.params.amount ? std::min(*a.params.amount, have) : have;
            if (n > 0 && (c.carrying["oil"] -= n) == 0) c.carrying.erase("oil");
            lantern.fuel += n;
            if (lantern.fuel <= kEps) return failed(used, "NoItemError", "У ліхтарі " + quote(lantern.name) + " немає олії");
            lantern.lit = is_night_at(c.local_time);  // by day it is only filled: lights burn at night
            return done_int(used, n);
        }
        case ActionKind::HoldService: {
            Object& church = objects_[a.target];
            double used = walk(c, church.pos, budget);
            if (!at(c, church.pos)) return running(used);
            bool fin = false;
            used += handle(c, a, b.service_minutes * 60.0, budget - used, Activity::Light, fin);
            if (!fin) return running(used);
            if (church.last_service >= 0 && c.local_time < church.last_service + b.service_interval_hours * 3600.0 - kEps)
                return failed(used, "ActionError", "У " + quote(church.name) + " службу щойно правили");
            church.last_service = c.local_time;
            int present = 0;
            for (Character& p : chars_) {
                if (!p.alive || !at(p, church.pos)) continue;
                p.mood = std::min(100.0, p.mood + b.service_mood);
                ++present;
            }
            log(c.obj, LogKind::Info,
                quote(c.name) + " править службу в " + quote(church.name) + ": присутніх " + std::to_string(present));
            return done_int(used, present);
        }
        case ActionKind::Bury: {
            Object& body = objects_[a.target];
            Object& church = objects_[a.place];
            double used = 0;
            if (!a.transferred) {  // to the body first, then with it to the cemetery
                used = walk(c, body.pos, budget);
                if (!at(c, body.pos)) return running(used);
                a.transferred = true;
            }
            used += walk(c, church.pos, budget - used);
            if (!at(c, church.pos)) return running(used);
            bool fin = false;
            used += handle(c, a, b.burial_minutes * 60.0, budget - used, Activity::Heavy, fin);
            if (!fin) return running(used);
            if (!is_body(body)) return failed(used, "ActionError", quote(body.name) + " уже поховано");
            if (body.kind == ObjKind::Person)
                character(body.id).buried = true;
            else
                body.removed = true;
            body.pos = church.pos;
            church.graves += 1;
            log(c.obj, LogKind::Info, quote(c.name) + " ховає " + quote(body.name) + " на кладовищі при " + quote(church.name));
            return done(used);
        }
        case ActionKind::Treat: {
            Object& po = objects_[a.target];
            double used = walk(c, po.pos, budget);
            if (!at(c, po.pos)) return running(used);
            Character& p = character(a.target);
            if (!hospital_at(p))
                return failed(used, "ActionError",
                              quote(p.name) + " не в лікарні: лікують лише в лікарні (хворий іде туди сам: me.go_to(лікарня))");
            bool fin = false;
            const double t = handle(c, a, b.treat_minutes * 60.0 / skill_factor(c, "medicine"), budget - used,
                                    Activity::Light, fin);
            used += t;
            practice(c, "medicine", t);
            if (!fin) return running(used);
            std::string what;
            const int herbs = static_cast<int>(std::lround(b.treat_herbs));
            if (p.illness_left > 0 && !p.illness_treated && c.carrying["herbs"] >= herbs) {
                c.carrying["herbs"] -= herbs;
                p.illness_treated = true;
                p.illness_left = std::min(p.illness_left, b.illness_treated_hours);
                what = "хвороба";
            }
            if (p.injury_left > 0 && !p.injury_treated && c.carrying["bandage"] >= 1) {
                c.carrying["bandage"] -= 1;
                p.injury_treated = true;
                p.injury_left = std::min(p.injury_left, b.injury_treated_hours);
                what += std::string(what.empty() ? "" : " і ") + "травма";
            }
            for (const char* r : {"herbs", "bandage"})
                if (c.carrying.count(r) && c.carrying[r] <= 0) c.carrying.erase(r);
            if (what.empty()) return failed(used, "NoItemError", quote(c.name) + " уже не має чим лікувати");
            log(c.obj, LogKind::Info, quote(c.name) + " лікує " + quote(p.name) + ": " + what);
            return done(used);
        }
        case ActionKind::Water: {
            Object& field = objects_[a.target];
            double used = walk(c, field.pos, budget);
            if (!at(c, field.pos)) return running(used);
            bool fin = false;
            const double t = handle(c, a, b.field_water_minutes * 60.0, budget - used, Activity::Heavy, fin);
            used += t;
            practice(c, "farming", t);
            if (!fin) return running(used);
            const int need = static_cast<int>(std::lround(b.field_water_units));
            auto it = c.carrying.find("water");
            if (it == c.carrying.end() || it->second < need)
                return failed(used, "NoItemError", quote(c.name) + " уже не несе " + std::to_string(need) + " відер води");
            if ((it->second -= need) == 0) c.carrying.erase(it);
            field.watered_until = std::max(field.watered_until, c.local_time + b.field_water_days * kDay);
            log(c.obj, LogKind::Info, "Поле " + quote(field.name) + " полито");
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
            // A broken one too: it lies there until tools can be repaired (or the merchant takes it).
            (s.kind == ObjKind::TradingPost ? s.sale_tools : s.tools).push_back(c.tool);
            c.tool = ToolItem{};
            return done(used);
        }
        case ActionKind::ChopWood:
        case ActionKind::Harvest:
        case ActionKind::Mow:
        case ActionKind::Mine:
        case ActionKind::GatherHerbs:
            return step_gather(a, c, budget);
        case ActionKind::Feed:
        case ActionKind::Milk:
        case ActionKind::CollectEggs:
        case ActionKind::Slaughter:
            return step_animal(a, c, budget);
        case ActionKind::Craft:
            return step_craft(a, c, budget);
        case ActionKind::Sow:
            return step_sow(a, c, budget);
        case ActionKind::Build:
            return step_build(a, c, budget);
        case ActionKind::Post:
        case ActionKind::Read:
            return step_board(a, c, budget);
        case ActionKind::Order:
            return step_order(a, c, budget);
        case ActionKind::Dismantle: {
            bool fin = false;
            double used = handle(c, a, b.dismantle_minutes * 60.0, budget, Activity::Light, fin);
            if (!fin) return running(used);
            if (c.tool.empty()) return failed(used, "NoItemError", quote(c.name) + " уже не тримає інструмента");
            const std::string kind = c.tool.kind;
            const auto [scrap, wood] = dismantle_yield(b, kind);
            c.tool = ToolItem{};
            if (scrap > 0) c.carrying["scrap"] += scrap;
            if (wood > 0) c.carrying["wood"] += wood;
            log(c.obj, LogKind::Info,
                quote(c.name) + " розбирає інструмент (" + resource_ua(kind) + "): брухт " + std::to_string(scrap) +
                    (wood > 0 ? ", дрова " + std::to_string(wood) : ""));
            return done_int(used, scrap);
        }
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
                const std::string spare = c.clothes.kind == "warm_clothes" ? "clothes" : "warm_clothes";
                auto g = std::find_if(from.chest.begin(), from.chest.end(), [&](const Garment& x) { return x.kind == spare; });
                if (g != from.chest.end()) {
                    house.chest.push_back(*g);
                    from.chest.erase(g);
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
                // A storage takes everything; a trading post — for sale; a house — clothes into the chest.
                auto& pile = s.kind == ObjKind::TradingPost ? s.for_sale : s.resources;
                int units = 0;
                for (auto it = c.carrying.begin(); it != c.carrying.end();) {
                    if (s.kind == ObjKind::House && it->first == "bed") {  // furniture: a bed per resident at most
                        const int n = std::min(it->second, std::max(0, s.capacity - s.beds));
                        s.beds += n;
                        units += n;
                        it->second -= n;
                        it = it->second == 0 ? c.carrying.erase(it) : std::next(it);
                        continue;
                    }
                    if (s.kind == ObjKind::House) {
                        if (it->first != "clothes" && it->first != "warm_clothes") {
                            ++it;
                            continue;
                        }
                        for (int i = 0; i < it->second; ++i) s.chest.push_back(Garment{it->first});  // new sets
                    } else {
                        pile[it->first] += it->second;
                    }
                    units += it->second;
                    it = c.carrying.erase(it);
                }
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
                if (house.firewood <= 0) log(c.obj, LogKind::Info, quote(c.name) + " розпалює піч у " + quote(house.name));
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
                // The best set of that kind in the chest.
                auto best = house.chest.end();
                for (auto g = house.chest.begin(); g != house.chest.end(); ++g)
                    if (g->kind == item && (best == house.chest.end() || g->condition > best->condition)) best = g;
                if (best == house.chest.end())
                    return failed(used, "NoItemError",
                                  "У скрині будинку " + quote(house.name) + " немає " + item_names(item).genitive);
                const Garment taken = *best;
                house.chest.erase(best);
                if (!c.clothes.empty()) house.chest.push_back(c.clothes);
                c.clothes = taken;
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
            c.hunger = std::max(0.0, c.hunger - food_relief(a.params.resource));
            c.ate[a.params.resource] = c.local_time;
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
            const Activity sleep = !at_home ? Activity::SleepGround : (has_bed(c) ? Activity::SleepBed : Activity::SleepHome);
            return step_recover(a, c, sleep, budget, used);
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

// me.post / me.read at a notice board. Notes change hands on arrival, then the writing or reading takes time.
// A note replaces an older one about the same object; nobody's fresher knowledge is overwritten.
StepResult World::step_board(Action& a, Character& c, double budget) {
    const Balance& b = balance_;
    Object& board = objects_[a.target];
    double used = walk(c, board.pos, budget);
    if (!at(c, board.pos)) return running(used);
    if (!a.transferred) {
        if (!board.built) return failed(used, "ActionError", quote(board.name) + " ще не збудовано");
        observe(c);
        auto& notes = boards_[board.id];
        int n = 0;
        if (a.kind == ActionKind::Post) {
            std::vector<int> ids = a.params.objects;
            if (ids.empty())
                for (const auto& [id, k] : c.known) ids.push_back(id);
            for (int id : ids) {
                const Object& o = objects_[id];
                if (o.kind == ObjKind::Board) continue;  // a board's notes are read on the spot
                if (id == c.obj) learn(c, o);            // one's own state is always known
                auto mine = c.known.find(id);
                if (mine == c.known.end() || mine->second.time < 0) continue;
                auto note = notes.find(id);
                if (note != notes.end() && note->second.time >= mine->second.time) continue;
                notes[id] = mine->second;
                ++n;
            }
            a.handle_total = b.board_post_minutes_per_note * std::max(1, n) * 60.0;
        } else {
            for (const auto& [id, note] : notes) {
                if (id == c.obj) continue;
                auto mine = c.known.find(id);
                if (mine != c.known.end() && mine->second.time >= note.time) continue;
                c.known[id] = note;
                ++n;
            }
            a.handle_total = (b.board_read_base_minutes + b.board_read_per_note_minutes * n) * 60.0;
        }
        a.done = n;
        a.transferred = true;
    }
    bool fin = false;
    used += handle(c, a, a.handle_total, budget - used, Activity::Light, fin);
    return fin ? done_int(used, a.done) : running(used);
}

// me.order: the order is written on the post when the character gets there.
StepResult World::step_order(Action& a, Character& c, double budget) {
    Object& post = objects_[a.target];
    double used = walk(c, post.pos, budget);
    if (!at(c, post.pos)) return running(used);
    bool fin = false;
    used += handle(c, a, balance_.order_minutes * 60.0, budget - used, Activity::Light, fin);
    if (!fin) return running(used);
    post.orders.push_back({a.params.item, a.params.count});
    return done(used);
}

// Feeding, milking, collecting eggs and slaughter: the character goes to the animal (in its barn), the
// change happens on arrival, then the work takes time.
StepResult World::step_animal(Action& a, Character& c, double budget) {
    const Balance& b = balance_;
    Object& animal = objects_[a.target];
    double used = walk(c, animal.pos, budget);
    if (!at(c, animal.pos)) return running(used);
    if (!a.transferred) {
        if (!animal.alive || animal.removed) return failed(used, "ActionError", "Тварина " + quote(animal.name) + " мертва");
        const bool cow = animal.species == "cow";
        int n = 0;
        if (a.kind == ActionKind::Feed) {
            const char* feed = cow ? "hay" : "grain";
            const double relief = cow ? b.cow_hay_relief : b.chicken_grain_relief;
            auto it = c.carrying.find(feed);
            const int have = it == c.carrying.end() ? 0 : it->second;
            if (have <= 0) return failed(used, "NoItemError", quote(c.name) + " уже не несе корму");
            // As much as it eats: the hunger left is the smallest, without overfeeding.
            const int wanted = static_cast<int>(std::floor(animal.hunger / relief + 0.5));
            n = std::min(have, a.params.amount ? *a.params.amount : wanted);
            n = std::max(0, n);
            animal.hunger = std::max(0.0, animal.hunger - n * relief);
            if ((it->second -= n) == 0) c.carrying.erase(it);
            a.handle_total = n > 0 ? (b.transfer_base_minutes + b.transfer_per_unit_minutes * n) * 60.0 : 0.0;
        } else if (a.kind == ActionKind::Milk || a.kind == ActionKind::CollectEggs) {
            const char* product = cow ? "milk" : "eggs";
            const double kg = resource_kg(product);
            const int fits = static_cast<int>(std::floor((b.weight_limit_kg - carry_weight(c)) / kg + kEps));
            n = std::min(static_cast<int>(std::floor(animal.produce + kEps)), std::max(0, fits));
            if (n == 0 && animal.produce >= 1.0 - kEps)
                return failed(used, "TooHeavyError", "Руки повні: " + resource_ua(product) + " не вміститься");
            animal.produce = std::max(0.0, animal.produce - n);
            if (n > 0) c.carrying[product] += n;
            a.handle_total = cow ? b.milk_minutes_per_unit * 60.0 * n
                                 : (n > 0 ? (b.transfer_base_minutes + b.transfer_per_unit_minutes * n) * 60.0 : 0.0);
        } else {
            n = static_cast<int>(std::lround(cow ? b.cow_meat : b.chicken_meat));
            a.handle_total = b.slaughter_minutes_per_meat * 60.0 * n;
        }
        a.done = n;
        a.transferred = true;
    }
    bool fin = false;
    const double t = handle(c, a, a.handle_total, budget - used, Activity::Light, fin);
    used += t;
    practice(c, "husbandry", t);
    if (!fin) return running(used);
    if (a.kind == ActionKind::Slaughter) {
        if (!animal.alive || animal.removed) return failed(used, "ActionError", "Тварина " + quote(animal.name) + " мертва");
        animal.alive = false;
        animal.removed = true;
        c.carrying["meat"] += a.done;
        log(c.obj, LogKind::Info, quote(c.name) + " забиває тварину " + quote(animal.name) + ": м'яса " + std::to_string(a.done));
    }
    return done_int(used, a.done);
}

// Chopping wood in a forest or harvesting a ripe field: units go into the hands one by one.
StepResult World::step_gather(Action& a, Character& c, double budget) {
    const Balance& b = balance_;
    const bool chop = a.kind == ActionKind::ChopWood;
    const bool mow = a.kind == ActionKind::Mow;
    const bool mine = a.kind == ActionKind::Mine;
    const bool herbs = a.kind == ActionKind::GatherHerbs;
    Object& src = objects_[a.target];
    const std::string resource = chop ? "wood" : (mow ? "hay" : (mine ? src.deposit : (herbs ? "herbs" : src.crop)));
    const double unit = chop    ? b.chop_minutes_per_unit * 60.0
                        : herbs ? b.herbs_minutes * 60.0
                        : mow   ? b.mow_minutes_per_unit * 60.0
                        : mine ? (src.deposit == "stone" ? b.mine_stone_minutes : b.mine_ore_minutes) * 60.0
                               : b.field_harvest_hours * 3600.0 / std::max(1, crop_yield(b, src.crop));
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
        const int units_left = chop   ? forest_units_left(src)
                               : herbs ? (season_at(c.local_time) == Season::Warm ? 1 : 0)
                               : mow  ? static_cast<int>(std::floor(src.hay + kEps))
                               : mine ? src.deposit_left
                                      : (src.field_state == FieldState::Ripe ? src.grain_left : 0);
        if (units_left <= 0) {
            if (chop) return failed(used, "NoItemError", "У лісі " + quote(src.name) + " не лишилося дерев", a.done);
            if (mine) return failed(used, "NoItemError", quote(src.name) + " вичерпано", a.done);
            if (herbs) {
                if (a.done > 0) return done_int(used, a.done);
                return failed(used, "ActionError", "Почалася зима — трав немає", 0);
            }
            if (mow) {
                if (a.done > 0) return done_int(used, a.done);
                return failed(used, "NoItemError", "На лузі " + quote(src.name) + " трава ще не відросла", 0);
            }
            // The field is harvested completely (by this character or by others): a natural end.
            if (a.done > 0) return done_int(used, a.done);
            return failed(used, "NoItemError", "На полі " + quote(src.name) + " немає стиглого врожаю", 0);
        }
        if (carry_weight(c) + resource_kg(resource) > b.weight_limit_kg + kEps) {
            if (!a.has_limits && a.done > 0) return done_int(used, a.done);
            return failed(used, "TooHeavyError",
                          std::string("Руки повні: зібрано ") + std::to_string(a.done) + ", більше не піднести", a.done);
        }

        const char* skill = chop ? "woodcutting" : mow ? "husbandry" : mine ? "mining" : herbs ? "medicine" : "farming";
        const double factor = work_factor(c, true, skill);
        double& partial = c.work_partial[src.id];
        double need = (1.0 - partial) * unit / factor;
        double t = std::min({left, a.deadline - c.local_time, need});
        pass_time(c, t, herbs ? Activity::Light : Activity::Heavy, mine ? b.injury_mine_per_hour : 0.0);
        used += t;
        practice(c, skill, t);
        const bool broke = !herbs && wear_tool(c, t);
        if (t >= need - kEps) {
            partial = 0;
            c.carrying[resource] += 1;
            a.done += 1;
            if (chop) {
                src.wood_taken += 1;
            } else if (mow) {
                src.hay = std::max(0.0, src.hay - 1.0);
            } else if (herbs) {
                // the meadow is big: herbs do not run out in the warm season
            } else if (mine) {
                if (--src.deposit_left == 0) log(c.obj, LogKind::Warning, quote(src.name) + " вичерпано");
            } else if (--src.grain_left <= 0) {
                src.grain_left = 0;
                src.crop.clear();
                src.field_state = FieldState::Empty;
                src.growth = 0;
                log(c.obj, LogKind::Info, "Урожай з поля " + quote(src.name) + " зібрано");
            }
        } else {
            partial = std::min(1.0, partial + t * factor / unit);
        }
        if (broke)
            return failed(used, "ToolBrokenError",
                          std::string(tool_broke_ua(c.tool.kind)) +
                              ": зібрано " + std::to_string(a.done) +
                              ". Поверніть зламаний інструмент на склад (put_back) і візьміть інший",
                          a.done);
    }
}

// Making things by a recipe in a workshop: unit by unit, the inputs leave the hands and the item comes in —
// a resource into the hands, a tool into the tool slot (one at a time). Stops when the hands run out of inputs.
StepResult World::step_craft(Action& a, Character& c, double budget) {
    const Recipe& r = balance_.recipes.at(a.params.item).at(a.params.variant);
    Object& place = objects_[a.target];
    const bool tool = is_tool(a.params.item);
    const double unit = r.minutes * 60.0;
    double used = walk(c, place.pos, std::min(budget, a.deadline - c.local_time));
    if (!at(c, place.pos)) return c.local_time >= a.deadline - kEps ? done_int(used, a.done) : running(used);
    for (;;) {
        if (!c.alive) return running(budget);
        if (a.params.amount && a.done >= *a.params.amount) return done_int(used, a.done);
        if (tool && a.done >= 1) return done_int(used, a.done);  // one tool fills the hands
        if (c.local_time >= a.deadline - kEps) return done_int(used, a.done);
        double left = budget - used;
        if (left <= kEps) return running(used);
        bool enough = true;
        for (const auto& [res, n] : r.inputs) {
            auto it = c.carrying.find(res);
            enough = enough && it != c.carrying.end() && it->second >= n;
        }
        if (!enough) {
            if (a.done > 0) return done_int(used, a.done);
            return failed(used, "NoItemError", "Для «" + resource_ua(a.params.item) + "» потрібно в руках: " + describe(r.inputs), 0);
        }
        if (r.fire && place.firewood <= 0)
            return failed(used, "NoItemError", "Піч у будинку " + quote(place.name) + " згасла", a.done);
        if (!tool && a.progress <= 0) {
            double after = carry_weight(c) + resource_kg(a.params.item);
            for (const auto& [res, n] : r.inputs) after -= resource_kg(res) * n;
            if (after > balance_.weight_limit_kg + kEps) {
                if (!a.has_limits && a.done > 0) return done_int(used, a.done);
                return failed(used, "TooHeavyError",
                              "Руки повні: виготовлено " + std::to_string(a.done) + ", більше не піднести", a.done);
            }
        }
        const double factor = work_factor(c, false, "crafting");
        double t = std::min({left, a.deadline - c.local_time, (unit - a.progress) / factor});
        pass_time(c, t, r.heavy ? Activity::Heavy : Activity::Light, r.workplace == "forge" ? balance_.injury_forge_per_hour : 0.0);
        used += t;
        a.progress += t * factor;
        practice(c, "crafting", t);
        if (a.progress >= unit - kEps) {
            a.progress = 0;
            for (const auto& [res, n] : r.inputs) {
                auto it = c.carrying.find(res);
                if ((it->second -= n) == 0) c.carrying.erase(it);
            }
            if (tool) {
                c.tool = make_tool(a.params.item);
                c.tool.quality = craft_quality(c);  // a skilled smith forges tools that wear slower
            } else {
                c.carrying[a.params.item] += 1;
            }
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
            const std::string& crop = a.params.resource;
            const int seed = crop_seed(b, crop);
            auto it = c.carrying.find(crop);
            if (it == c.carrying.end() || it->second < seed)
                return failed(used, "NoItemError", "Для посіву потрібно в руках: " + resource_ua(crop) + " " + std::to_string(seed));
            it->second -= seed;
            if (it->second == 0) c.carrying.erase(it);
            field.seeded = true;
            field.crop = crop;
        }
        const double factor = work_factor(c, true, "farming");
        double t = std::min({left, a.deadline - c.local_time, (total - field.sow_done) / factor});
        pass_time(c, t, Activity::Heavy);
        used += t;
        practice(c, "farming", t);
        field.sow_done += t * factor;
        if (field.sow_done >= total - kEps) {
            field.field_state = FieldState::Sown;
            field.sow_done = 0;
            field.seeded = false;
            field.growth = 0;
            log(c.obj, LogKind::Info, "Поле " + quote(field.name) + " засіяно: " + resource_ua(field.crop));
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
        const double factor = work_factor(c, true, "building");
        double t = std::min({left, a.deadline - c.local_time, (site.build_total - site.build_done) / factor});
        pass_time(c, t, Activity::Heavy);
        used += t;
        practice(c, "building", t);
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
    if (has_stove(o)) o.temperature = unheated_house_temperature(now_);
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
        if (Object* s = object(a.target))
            if (ToolItem* t = tool_in(*s, a.params.tool_id)) t->reserved_by = -1;
        a.reserved = false;
    }
    if (a.kind == ActionKind::Craft) {
        auto r = balance_.recipes.find(a.params.item);
        Object* w = object(a.target);
        if (w && r != balance_.recipes.end() && a.params.variant < static_cast<int>(r->second.size())) {
            auto user = w->workplace_users.find(r->second[a.params.variant].workplace);
            if (user != w->workplace_users.end() && user->second == a.cid) w->workplace_users.erase(user);  // free again
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
