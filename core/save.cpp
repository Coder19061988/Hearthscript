#include "save.h"

#include <sstream>
#include <stdexcept>

#include "json.hpp"

namespace kindle {

using nlohmann::json;

namespace {

constexpr const char* kFormat = "kindlescript-save";
constexpr int kVersion = 1;
constexpr size_t kSavedLogEntries = 500;

ObjKind kind_from(const std::string& s) {
    for (ObjKind k : {ObjKind::Storage, ObjKind::House, ObjKind::Forest, ObjKind::Field, ObjKind::Person})
        if (s == kind_name(k)) return k;
    throw std::runtime_error("невідомий тип об'єкта " + s);
}

FieldState field_state_from(const std::string& s) {
    for (FieldState f : {FieldState::Empty, FieldState::Sown, FieldState::Ripe})
        if (s == field_state_name(f)) return f;
    throw std::runtime_error("невідомий стан поля " + s);
}

json counts(const std::map<std::string, int>& m) {
    json j = json::object();
    for (const auto& [k, v] : m) j[k] = v;
    return j;
}

std::map<std::string, int> counts_from(const json& j) {
    std::map<std::string, int> m;
    for (const auto& [k, v] : j.items()) m[k] = v.get<int>();
    return m;
}

}  // namespace

// Has access to World internals (declared a friend in world.h).
struct SaveAccess {
    static json save(World& w, ScriptRuntime& rt) {
        json j;
        j["format"] = kFormat;
        j["version"] = kVersion;
        j["now"] = w.now_;
        std::ostringstream rng;
        rng << w.rng_;
        j["rng"] = rng.str();
        j["random_rain"] = w.random_rain_;
        j["weather_days"] = w.weather_days_;
        json rain = json::array();
        for (const auto& r : w.rain_) rain.push_back({r.start, r.end});
        j["rain"] = rain;

        json globals = json::object();
        for (const auto& [name, value] : w.globals_) std::visit([&](const auto& v) { globals[name] = v; }, value);
        j["globals"] = globals;

        json objects = json::array();
        for (const Object& o : w.objects_) {
            objects.push_back({
                {"id", o.id}, {"kind", kind_name(o.kind)}, {"name", o.name}, {"x", o.pos.x}, {"y", o.pos.y},
                {"resources", counts(o.resources)}, {"tools", counts(o.tools)},
                {"firewood", o.firewood}, {"temperature", o.temperature}, {"chest", counts(o.chest)},
                {"trees_total", o.trees_total}, {"wood_taken", o.wood_taken},
                {"field_state", field_state_name(o.field_state)}, {"seeded", o.seeded}, {"sow_done", o.sow_done},
                {"growth", o.growth}, {"grain_left", o.grain_left},
                {"built", o.built}, {"materials_needed", counts(o.materials_needed)},
                {"materials_delivered", counts(o.materials_delivered)}, {"build_done", o.build_done},
                {"build_total", o.build_total}, {"capacity", o.capacity},
            });
        }
        j["objects"] = objects;

        json characters = json::array();
        for (const Character& c : w.chars_) {
            json partial = json::object();
            for (const auto& [id, v] : c.work_partial) partial[std::to_string(id)] = v;
            characters.push_back({
                {"obj", c.obj}, {"name", c.name}, {"hunger", c.hunger}, {"fatigue", c.fatigue},
                {"health", c.health}, {"warmth", c.warmth}, {"alive", c.alive}, {"tool", c.tool},
                {"clothes", c.clothes}, {"carrying", counts(c.carrying)}, {"home", c.home},
                {"last_action_time", c.last_action_time}, {"work_partial", partial},
                {"idle_warned", c.idle_warned}, {"hunger_warned", c.hunger_warned},
                {"fatigue_warned", c.fatigue_warned}, {"cold_warned", c.cold_warned},
                {"health_warn_level", c.health_warn_level}, {"death_time", c.death_time},
                {"death_cause", c.death_cause}, {"code", rt.source(c.obj)},
            });
        }
        j["characters"] = characters;

        json log = json::array();
        const size_t first = w.log_.size() > kSavedLogEntries ? w.log_.size() - kSavedLogEntries : 0;
        for (size_t i = first; i < w.log_.size(); ++i) {
            const LogEntry& e = w.log_[i];
            log.push_back({{"time", e.time}, {"obj", e.object_id}, {"kind", static_cast<int>(e.kind)},
                           {"text", e.text}, {"detail", e.detail}});
        }
        j["log"] = log;
        j["shared_code"] = rt.shared_code();
        return j;
    }

    // Fills an empty world (fresh Simulation) from a save. Throws on malformed data.
    static void load(World& w, const json& j) {
        w.now_ = j.at("now").get<double>();
        std::istringstream rng(j.at("rng").get<std::string>());
        rng >> w.rng_;
        if (!rng) throw std::runtime_error("пошкоджений стан генератора випадковості");
        w.random_rain_ = j.at("random_rain").get<bool>();
        w.weather_days_ = j.at("weather_days").get<int>();
        w.rain_.clear();
        for (const auto& r : j.at("rain")) w.rain_.push_back({r.at(0).get<double>(), r.at(1).get<double>()});

        w.globals_.clear();
        for (const auto& [name, v] : j.at("globals").items()) {
            if (v.is_boolean())
                w.globals_[name] = v.get<bool>();
            else if (v.is_number_integer())
                w.globals_[name] = v.get<long long>();
            else if (v.is_number())
                w.globals_[name] = v.get<double>();
            else
                w.globals_[name] = v.get<std::string>();
        }

        w.objects_.clear();
        w.by_name_.clear();
        for (const auto& oj : j.at("objects")) {
            Object o;
            o.id = oj.at("id").get<int>();
            if (o.id != static_cast<int>(w.objects_.size())) throw std::runtime_error("порушено порядок об'єктів");
            o.kind = kind_from(oj.at("kind").get<std::string>());
            o.name = oj.at("name").get<std::string>();
            o.pos = {oj.at("x").get<double>(), oj.at("y").get<double>()};
            o.resources = counts_from(oj.at("resources"));
            o.tools = counts_from(oj.at("tools"));
            o.firewood = oj.at("firewood").get<double>();
            o.temperature = oj.at("temperature").get<double>();
            o.chest = counts_from(oj.at("chest"));
            o.trees_total = oj.at("trees_total").get<int>();
            o.wood_taken = oj.at("wood_taken").get<int>();
            o.field_state = field_state_from(oj.at("field_state").get<std::string>());
            o.seeded = oj.at("seeded").get<bool>();
            o.sow_done = oj.at("sow_done").get<double>();
            o.growth = oj.at("growth").get<double>();
            o.grain_left = oj.at("grain_left").get<int>();
            o.built = oj.at("built").get<bool>();
            o.materials_needed = counts_from(oj.at("materials_needed"));
            o.materials_delivered = counts_from(oj.at("materials_delivered"));
            o.build_done = oj.at("build_done").get<double>();
            o.build_total = oj.at("build_total").get<double>();
            o.capacity = oj.at("capacity").get<int>();
            if (w.by_name_.count(o.name)) throw std::runtime_error("повторна назва " + o.name);
            w.by_name_[o.name] = o.id;
            w.objects_.push_back(o);
        }

        w.chars_.clear();
        w.actions_.clear();
        for (const auto& cj : j.at("characters")) {
            Character c;
            c.obj = cj.at("obj").get<int>();
            if (c.obj < 0 || c.obj >= static_cast<int>(w.objects_.size()) || w.objects_[c.obj].kind != ObjKind::Person)
                throw std::runtime_error("персонаж посилається на неправильний об'єкт");
            c.name = cj.at("name").get<std::string>();
            c.hunger = cj.at("hunger").get<double>();
            c.fatigue = cj.at("fatigue").get<double>();
            c.health = cj.at("health").get<double>();
            c.warmth = cj.at("warmth").get<double>();
            c.alive = cj.at("alive").get<bool>();
            c.tool = cj.at("tool").get<std::string>();
            c.clothes = cj.at("clothes").get<std::string>();
            c.carrying = counts_from(cj.at("carrying"));
            c.home = cj.at("home").get<int>();
            if (c.home >= static_cast<int>(w.objects_.size()) || (c.home >= 0 && w.objects_[c.home].kind != ObjKind::House))
                throw std::runtime_error("дім персонажа — не будинок");
            c.local_time = w.now_;
            c.last_action_time = cj.at("last_action_time").get<double>();
            for (const auto& [id, v] : cj.at("work_partial").items()) c.work_partial[std::stoi(id)] = v.get<double>();
            c.idle_warned = cj.at("idle_warned").get<bool>();
            c.hunger_warned = cj.at("hunger_warned").get<bool>();
            c.fatigue_warned = cj.at("fatigue_warned").get<bool>();
            c.cold_warned = cj.at("cold_warned").get<bool>();
            c.health_warn_level = cj.at("health_warn_level").get<int>();
            c.death_time = cj.at("death_time").get<double>();
            c.death_cause = cj.at("death_cause").get<std::string>();
            w.objects_[c.obj].char_index = static_cast<int>(w.chars_.size());
            w.chars_.push_back(c);
        }

        w.log_.clear();
        for (const auto& e : j.at("log")) {
            LogEntry entry;
            entry.time = e.at("time").get<double>();
            entry.object_id = e.at("obj").get<int>();
            entry.kind = static_cast<LogKind>(e.at("kind").get<int>());
            entry.text = e.at("text").get<std::string>();
            entry.detail = e.at("detail").get<std::string>();
            w.log_.push_back(entry);
        }
    }
};

std::string save_game(Simulation& sim) { return SaveAccess::save(sim.world(), sim.runtime()).dump(1); }

std::unique_ptr<Simulation> load_game(const Balance& balance, const std::string& text, std::string& error) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = "файл не є збереженням Kindlescript (неправильний JSON)";
        return nullptr;
    }
    if (j.value("format", "") != kFormat) {
        error = "файл не є збереженням Kindlescript";
        return nullptr;
    }
    if (j.value("version", 0) != kVersion) {
        error = "непідтримувана версія збереження";
        return nullptr;
    }
    auto sim = std::make_unique<Simulation>(balance, 1);
    try {
        SaveAccess::load(sim->world(), j);
        sim->runtime().set_shared_code(j.value("shared_code", ""), false);
        for (const auto& cj : j.at("characters")) sim->runtime().set_code(cj.at("obj").get<int>(), cj.value("code", ""));
    } catch (const std::exception& e) {
        error = std::string("пошкоджене збереження: ") + e.what();
        return nullptr;
    }
    return sim;
}

}  // namespace kindle
