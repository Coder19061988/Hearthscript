#include "kindle_simulation.h"

#include "save.h"

#include <cmath>
#include <map>
#include <string>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

namespace kindle_godot {

namespace {

String to_godot(const std::string& s) { return String::utf8(s.c_str()); }
std::string to_std(const String& s) { return std::string(s.utf8().get_data()); }

const char* log_kind_name(kindle::LogKind k) {
    switch (k) {
        case kindle::LogKind::Info: return "info";
        case kindle::LogKind::Notify: return "notify";
        case kindle::LogKind::Print: return "print";
        case kindle::LogKind::Warning: return "warning";
        case kindle::LogKind::Error: return "error";
        case kindle::LogKind::Death: return "death";
        case kindle::LogKind::Outcome: return "outcome";
    }
    return "info";
}

const char* state_id(kindle::AgentState s) {
    switch (s) {
        case kindle::AgentState::NoCode: return "no_code";
        case kindle::AgentState::Running: return "running";
        case kindle::AgentState::Finished: return "finished";
        case kindle::AgentState::Crashed: return "crashed";
        case kindle::AgentState::Dead: return "dead";
    }
    return "no_code";
}

Dictionary to_dictionary(const std::map<std::string, int>& m) {
    Dictionary d;
    for (const auto& [key, n] : m)
        if (n != 0) d[to_godot(key)] = n;
    return d;
}

}  // namespace

void KindleSimulation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_balance_json", "text"), &KindleSimulation::load_balance_json);
    ClassDB::bind_method(D_METHOD("start", "options"), &KindleSimulation::start);
    ClassDB::bind_method(D_METHOD("is_started"), &KindleSimulation::is_started);
    ClassDB::bind_method(D_METHOD("advance", "game_seconds"), &KindleSimulation::advance);
    ClassDB::bind_method(D_METHOD("get_time_scale"), &KindleSimulation::get_time_scale);
    ClassDB::bind_method(D_METHOD("get_now"), &KindleSimulation::get_now);
    ClassDB::bind_method(D_METHOD("get_time_of_day"), &KindleSimulation::get_time_of_day);
    ClassDB::bind_method(D_METHOD("get_day"), &KindleSimulation::get_day);
    ClassDB::bind_method(D_METHOD("get_time_text"), &KindleSimulation::get_time_text);
    ClassDB::bind_method(D_METHOD("is_raining"), &KindleSimulation::is_raining);
    ClassDB::bind_method(D_METHOD("is_night"), &KindleSimulation::is_night);
    ClassDB::bind_method(D_METHOD("get_season"), &KindleSimulation::get_season);
    ClassDB::bind_method(D_METHOD("get_season_days_left"), &KindleSimulation::get_season_days_left);
    ClassDB::bind_method(D_METHOD("get_temperature"), &KindleSimulation::get_temperature);
    ClassDB::bind_method(D_METHOD("get_outcome"), &KindleSimulation::get_outcome);
    ClassDB::bind_method(D_METHOD("get_buildings"), &KindleSimulation::get_buildings);
    ClassDB::bind_method(D_METHOD("get_characters"), &KindleSimulation::get_characters);
    ClassDB::bind_method(D_METHOD("add_blueprint", "kind", "name", "position"), &KindleSimulation::add_blueprint);
    ClassDB::bind_method(D_METHOD("get_build_costs"), &KindleSimulation::get_build_costs);
    ClassDB::bind_method(D_METHOD("set_global", "name", "value"), &KindleSimulation::set_global);
    ClassDB::bind_method(D_METHOD("remove_global", "name"), &KindleSimulation::remove_global);
    ClassDB::bind_method(D_METHOD("get_globals"), &KindleSimulation::get_globals);
    ClassDB::bind_method(D_METHOD("save_game"), &KindleSimulation::save_game);
    ClassDB::bind_method(D_METHOD("load_game", "text"), &KindleSimulation::load_game);
    ClassDB::bind_method(D_METHOD("set_code", "character_id", "code"), &KindleSimulation::set_code);
    ClassDB::bind_method(D_METHOD("get_code", "character_id"), &KindleSimulation::get_code);
    ClassDB::bind_method(D_METHOD("set_shared_code", "code", "restart"), &KindleSimulation::set_shared_code, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("get_shared_code"), &KindleSimulation::get_shared_code);
    ClassDB::bind_method(D_METHOD("take_log"), &KindleSimulation::take_log);
}

String KindleSimulation::load_balance_json(const String& text) {
    kindle::Balance b;
    std::string error;
    std::vector<std::string> warnings;
    if (!b.load_json_text(to_std(text), error, warnings)) return to_godot("Баланс: " + error);
    balance_ = b;
    std::string report;
    for (const auto& w : warnings) report += "Баланс: " + w + "\n";
    return to_godot(report);
}

void KindleSimulation::start(const Dictionary& options) {
    kindle::StartScene scene;
    scene.characters = int(options.get("characters", 4));
    scene.axes = int(options.get("axes", scene.characters));
    scene.sickles = int(options.get("sickles", 0));
    scene.bread = int(options.get("bread", 40));
    scene.grain = int(options.get("grain", 0));
    scene.trees = int(options.get("trees", 200));
    scene.warm_clothes = int(options.get("warm_clothes", -1));
    // For tests and screenshots: another start moment or a weak settlement.
    const int start_day = int(options.get("start_day", 1));
    const double start_hour = double(options.get("start_hour", scene.start_time / 3600.0));
    scene.start_time = (start_day - 1) * kindle::kDay + start_hour * 3600.0;
    scene.hunger = double(options.get("hunger", scene.hunger));
    scene.health = double(options.get("health", scene.health));
    const int wood = int(options.get("wood", 0));
    const bool random_rain = bool(options.get("random_rain", true));
    const int64_t seed = int64_t(options.get("seed", 1));
    ERR_FAIL_COND_MSG(scene.characters < 1 || scene.characters > 127, "characters must be 1..127");
    ERR_FAIL_COND_MSG(start_day < 1 || start_hour < 0 || start_hour >= 24, "start_day must be >= 1, start_hour 0..24");

    pending_log_.clear();
    sim_.reset();  // the old runtime must release its VMs before the new one takes them
    sim_ = std::make_unique<kindle::Simulation>(balance_, static_cast<uint64_t>(seed));
    sim_->world().set_random_rain(random_rain);
    kindle::build_start_scene(sim_->world(), scene);
    if (const kindle::Object* storage = sim_->world().find("Склад_1"))
        sim_->world().object(storage->id)->resources["wood"] = wood;
    // Called from inside character coroutines: only queue, never touch the Godot API here.
    sim_->world().on_log = [this](const kindle::LogEntry& e) { pending_log_.push_back(e); };
}

void KindleSimulation::advance(double game_seconds) {
    ERR_FAIL_COND_MSG(!sim_, "call start() first");
    if (game_seconds <= 0) return;
    sim_->run_for(game_seconds, 60.0);
}

double KindleSimulation::get_time_scale() const { return kindle::kDay / (balance_.day_real_minutes * 60.0); }

double KindleSimulation::get_now() const { return sim_ ? sim_->world().now() : 0.0; }

double KindleSimulation::get_time_of_day() const { return std::fmod(get_now(), kindle::kDay); }

int KindleSimulation::get_day() const { return int(std::floor(get_now() / kindle::kDay)) + 1; }

String KindleSimulation::get_time_text() const {
    return sim_ ? to_godot(sim_->world().format_time(get_now())) : String();
}

bool KindleSimulation::is_raining() const { return sim_ && sim_->world().is_raining_at(get_now()); }

bool KindleSimulation::is_night() const { return sim_ && sim_->world().is_night_at(get_now()); }

String KindleSimulation::get_season() const {
    return sim_ ? String(kindle::season_name(sim_->world().season_at(get_now()))) : String();
}

int KindleSimulation::get_season_days_left() const { return sim_ ? sim_->world().season_days_left(get_now()) : 0; }

double KindleSimulation::get_temperature() const { return sim_ ? sim_->world().outdoor_temperature(get_now()) : 0.0; }

Dictionary KindleSimulation::get_outcome() const {
    Dictionary d;
    kindle::World* w = sim_ ? &sim_->world() : nullptr;
    d["winters_survived"] = w ? w->winters_survived() : 0;
    d["alive"] = w ? w->alive_count() : 0;
    d["total"] = w ? int(w->characters().size()) : 0;
    return d;
}

Array KindleSimulation::get_buildings() const {
    Array result;
    if (!sim_) return result;
    kindle::World& w = sim_->world();
    for (const kindle::Object& o : w.objects()) {
        if (o.kind == kindle::ObjKind::Person) continue;
        Dictionary d;
        d["id"] = o.id;
        d["kind"] = kindle::kind_name(o.kind);
        d["name"] = to_godot(o.name);
        d["position"] = Vector2(real_t(o.pos.x), real_t(o.pos.y));
        if (o.kind == kindle::ObjKind::Storage) {
            d["resources"] = to_dictionary(o.resources);
            d["tools"] = to_dictionary(o.tools);
        }
        if (o.kind == kindle::ObjKind::House) {
            d["firewood"] = o.firewood;
            d["temperature"] = o.temperature;
            d["heated"] = o.firewood > 0;
            d["chest"] = to_dictionary(o.chest);
        }
        if (o.kind == kindle::ObjKind::Field) {
            d["state"] = kindle::field_state_name(o.field_state);
            d["growth"] = o.growth;
            d["grain_left"] = o.grain_left;
        }
        if (kindle::is_building(o.kind)) {
            d["built"] = o.built;
            d["progress"] = o.built ? 1.0 : (o.build_total > 0 ? o.build_done / o.build_total : 0.0);
            d["needs"] = to_dictionary(w.materials_missing(o));
            d["materials_needed"] = to_dictionary(o.materials_needed);
        }
        if (o.kind == kindle::ObjKind::House) {
            d["capacity"] = o.capacity;
            Array residents;
            for (const kindle::Character& c : w.characters())
                if (c.alive && c.home == o.id) residents.push_back(to_godot(c.name));
            d["residents"] = residents;
        }
        if (o.kind == kindle::ObjKind::Forest) {
            d["trees_left"] = w.forest_trees_left(o);
            d["trees_total"] = o.trees_total;
        }
        result.push_back(d);
    }
    return result;
}

Array KindleSimulation::get_characters() const {
    Array result;
    if (!sim_) return result;
    kindle::World& w = sim_->world();
    kindle::ScriptRuntime& rt = sim_->runtime();
    for (const kindle::Character& c : w.characters()) {
        const kindle::Object& o = w.objects()[c.obj];
        Dictionary d;
        d["id"] = c.obj;
        d["name"] = to_godot(c.name);
        d["position"] = Vector2(real_t(o.pos.x), real_t(o.pos.y));
        d["alive"] = c.alive;
        d["hunger"] = c.hunger;
        d["fatigue"] = c.fatigue;
        d["health"] = c.health;
        d["warmth"] = c.warmth;
        d["tool"] = to_godot(c.tool);
        d["clothes"] = to_godot(c.clothes);
        d["home"] = c.home >= 0 ? to_godot(w.objects()[c.home].name) : String();
        d["carrying"] = to_dictionary(c.carrying);
        const kindle::AgentState state = rt.state(c.obj);
        d["state"] = state_id(state);
        d["state_text"] = to_godot(kindle::agent_state_name(state));
        d["line"] = rt.current_line(c.obj);
        const kindle::CodeFile file = rt.current_file(c.obj);
        d["line_file"] = file == kindle::CodeFile::Shared ? "shared" : (file == kindle::CodeFile::Own ? "own" : "");
        const kindle::Action* a = w.current_action(c.obj);
        d["action"] = a ? to_godot(a->name) : String();
        const kindle::Object* target = a ? w.object(a->target) : nullptr;
        d["action_target"] = target ? to_godot(target->name) : String();
        d["death_cause"] = to_godot(c.death_cause);
        d["death_time"] = c.death_time;  // -1 while alive
        d["death_time_text"] = c.alive ? String() : to_godot(w.format_time(c.death_time));
        result.push_back(d);
    }
    return result;
}

String KindleSimulation::add_blueprint(const String& kind, const String& name, const Vector2& position) {
    ERR_FAIL_COND_V_MSG(!sim_, "start() first", "call start() first");
    kindle::ObjKind k;
    if (kind == "house")
        k = kindle::ObjKind::House;
    else if (kind == "storage")
        k = kindle::ObjKind::Storage;
    else if (kind == "field")
        k = kindle::ObjKind::Field;
    else
        return to_godot("Невідомий тип будівлі");
    std::string error;
    if (sim_->world().add_blueprint(k, to_std(name), {position.x, position.y}, error) < 0) return to_godot(error);
    return String();
}

Dictionary KindleSimulation::get_build_costs() const {
    Dictionary costs;
    auto entry = [](double wood, double hours) {
        Dictionary d;
        d["wood"] = int(std::lround(wood));
        d["hours"] = hours;
        return d;
    };
    costs["house"] = entry(balance_.house_build_wood, balance_.house_build_hours);
    costs["storage"] = entry(balance_.storage_build_wood, balance_.storage_build_hours);
    costs["field"] = entry(0, balance_.field_build_hours);
    return costs;
}

String KindleSimulation::set_global(const String& name, const Variant& value) {
    ERR_FAIL_COND_V_MSG(!sim_, "start() first", "call start() first");
    kindle::World::GlobalValue v;
    switch (value.get_type()) {
        case Variant::BOOL: v = bool(value); break;
        case Variant::INT: v = static_cast<long long>(int64_t(value)); break;
        case Variant::FLOAT: v = double(value); break;
        case Variant::STRING: v = to_std(String(value)); break;
        default: return to_godot("Значення має бути True/False, числом або текстом");
    }
    std::string error;
    if (!sim_->runtime().set_global(to_std(name), v, error)) return to_godot(error);
    return String();
}

bool KindleSimulation::remove_global(const String& name) { return sim_ && sim_->runtime().remove_global(to_std(name)); }

Dictionary KindleSimulation::get_globals() const {
    Dictionary d;
    if (!sim_) return d;
    for (const auto& [name, value] : sim_->world().globals()) {
        Variant v;
        if (const bool* b = std::get_if<bool>(&value))
            v = *b;
        else if (const long long* i = std::get_if<long long>(&value))
            v = int64_t(*i);
        else if (const double* f = std::get_if<double>(&value))
            v = *f;
        else
            v = to_godot(std::get<std::string>(value));
        d[to_godot(name)] = v;
    }
    return d;
}

String KindleSimulation::save_game() {
    ERR_FAIL_COND_V_MSG(!sim_, String(), "call start() first");
    return to_godot(kindle::save_game(*sim_));
}

String KindleSimulation::load_game(const String& text) {
    std::string error;
    auto loaded = kindle::load_game(balance_, to_std(text), error);
    if (!loaded) return to_godot(error);
    sim_ = std::move(loaded);  // the old game goes away only now, after a successful load
    // The restored history goes to the UI log; new entries are queued as usual.
    pending_log_ = sim_->world().log_entries();
    sim_->world().on_log = [this](const kindle::LogEntry& e) { pending_log_.push_back(e); };
    return String();
}

void KindleSimulation::set_code(int character_id, const String& code) {
    ERR_FAIL_COND_MSG(!sim_, "call start() first");
    const kindle::Object* o = sim_->world().object(character_id);
    ERR_FAIL_COND_MSG(!o || o->kind != kindle::ObjKind::Person, "not a character id");
    sim_->runtime().set_code(character_id, to_std(code));
}

void KindleSimulation::set_shared_code(const String& code, bool restart) {
    ERR_FAIL_COND_MSG(!sim_, "call start() first");
    sim_->runtime().set_shared_code(to_std(code), restart);
}

String KindleSimulation::get_shared_code() const { return sim_ ? to_godot(sim_->runtime().shared_code()) : String(); }

String KindleSimulation::get_code(int character_id) const {
    if (!sim_) return String();
    const kindle::Object* o = sim_->world().object(character_id);
    if (!o || o->kind != kindle::ObjKind::Person) return String();
    return to_godot(sim_->runtime().source(character_id));
}

Array KindleSimulation::take_log() {
    Array result;
    if (!sim_) return result;
    kindle::World& w = sim_->world();
    for (const kindle::LogEntry& e : pending_log_) {
        Dictionary d;
        d["time_text"] = to_godot(w.format_time(e.time));
        d["kind"] = log_kind_name(e.kind);
        d["who"] = e.object_id >= 0 ? to_godot(w.objects()[e.object_id].name) : String();
        d["text"] = to_godot(e.text);
        d["detail"] = to_godot(e.detail);
        result.push_back(d);
    }
    pending_log_.clear();
    return result;
}

}  // namespace kindle_godot
