#include "runtime.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "world.h"
// pocketpy.h defines macros such as TypeError/ValueError, so it goes after our headers.
#include "minicoro.h"
#include "pocketpy.h"
#include "prelude_py.h"

namespace kindle {

namespace {

constexpr const char* kPlayerFile = "<код>";
constexpr const char* kSharedFile = "<спільний код>";
constexpr const char* kPreludeFile = "<kindle>";
constexpr double kRuleCheckSeconds = 60.0;  // conditions of @on(lambda: ...) rules are checked once a game minute
constexpr int kMaxConditionLines = 1000;    // a condition running longer is treated as an error
constexpr size_t kFiberStackSize = 1024 * 1024;

bool g_initialized = false;
bool g_allow_import = false;
std::vector<int> g_free_vms;  // VMs released by destroyed runtimes; reset before reuse
int g_next_vm = 1;            // VM 0 is pocketpy's default VM and is not used for characters

bool import_guard(const char* path) {
    if (g_allow_import) return true;
    return ImportError("import недоступний у коді персонажа (модуль '%s')", path);
}

char* deny_importfile(const char*, int*) { return nullptr; }

std::string quote(const std::string& s) { return "«" + s + "»"; }

// Sets (or removes, if v == nullptr) a player global in the builtins of the current VM.
void apply_global(const std::string& name, const World::GlobalValue* v) {
    py_GlobalRef builtins = py_getmodule("builtins");
    if (!v) {
        py_deldict(builtins, py_name(name.c_str()));
        return;
    }
    if (const bool* b = std::get_if<bool>(v))
        py_newbool(py_r0(), *b);
    else if (const long long* i = std::get_if<long long>(v))
        py_newint(py_r0(), *i);
    else if (const double* d = std::get_if<double>(v))
        py_newfloat(py_r0(), *d);
    else
        py_newstr(py_r0(), std::get<std::string>(*v).c_str());
    py_setdict(builtins, py_name(name.c_str()), py_r0());
}

// Raises the prelude-defined exception class `e.type` (NoItemError, ...) with reason and done.
bool raise_action_error(const ActionError& e) {
    py_ItemRef cls = py_getbuiltin(py_name(e.type.c_str()));
    if (!cls) return RuntimeError("%s", e.reason.c_str());
    py_TValue cls_copy = *cls;
    py_newstr(py_r0(), e.reason.c_str());
    double int_part = 0;
    if (std::modf(e.done, &int_part) == 0.0)
        py_newint(py_r1(), static_cast<py_i64>(e.done));
    else
        py_newfloat(py_r1(), e.done);
    if (!py_call(&cls_copy, 2, py_r0())) return false;
    py_assign(py_r2(), py_retval());
    return py_raise(py_r2());
}

// Finds the innermost line of player code (own or shared) in a pocketpy traceback.
int player_line_from_traceback(const std::string& tb, CodeFile& file) {
    const std::string own = std::string("File \"") + kPlayerFile + "\", line ";
    const std::string shared = std::string("File \"") + kSharedFile + "\", line ";
    const size_t own_pos = tb.rfind(own);
    const size_t shared_pos = tb.rfind(shared);
    if (own_pos == std::string::npos && shared_pos == std::string::npos) {
        file = CodeFile::None;
        return 0;
    }
    if (shared_pos == std::string::npos || (own_pos != std::string::npos && own_pos > shared_pos)) {
        file = CodeFile::Own;
        return std::atoi(tb.c_str() + own_pos + own.size());
    }
    file = CodeFile::Shared;
    return std::atoi(tb.c_str() + shared_pos + shared.size());
}

std::string last_nonempty_line(const std::string& s) {
    size_t end = s.find_last_not_of("\r\n ");
    if (end == std::string::npos) return {};
    size_t begin = s.rfind('\n', end);
    begin = begin == std::string::npos ? 0 : begin + 1;
    return s.substr(begin, end - begin + 1);
}

}  // namespace

const char* agent_state_name(AgentState s) {
    switch (s) {
        case AgentState::NoCode: return "без коду";
        case AgentState::Running: return "виконується";
        case AgentState::Finished: return "завершився";
        case AgentState::Crashed: return "зупинений помилкою";
        case AgentState::Dead: return "зупинено через смерть";
    }
    return "?";
}

struct Agent {
    ScriptRuntime* rt = nullptr;
    int char_obj = -1;
    int vm = -1;
    bool vm_dirty = false;  // VM was used before and must be reset
    mco_coro* co = nullptr;
    std::string own_source;
    AgentState state = AgentState::NoCode;
    double tick_end = 0;
    int current_line = 0;
    CodeFile current_file = CodeFile::None;
    std::string print_buf;
    bool in_condition = false;  // evaluating the condition of a rule: free of time, no actions allowed
    int condition_lines = 0;

    // A registered @on rule. `event` is "RAIN", "RAIN_STOPPED", "TIME" (daily at `tod`)
    // or "CONDITION" (fires when the condition becomes true).
    // `rank` orders rules (smaller runs first and interrupts larger): the player's priority, and at equal
    // priority a rule written in the character's own code outranks one from the shared code.
    struct Handler {
        std::string event;
        int index;  // position in the prelude's `_handlers` list
        int64_t rank;
        double tod = 0;
        double next_fire = 0;  // TIME: next absolute moment
        int last = -1;         // CONDITION: previous value (-1 = not checked yet)
        double last_check = -1;
    };
    struct Pending {
        std::string event;
        int index;
        int64_t rank;
        uint64_t seq;
    };
    std::vector<Handler> handlers;
    std::vector<Pending> pending;
    std::vector<int64_t> running_ranks;  // ranks of handlers currently on the stack
    uint64_t next_seq = 0;

    static int64_t rule_rank(int64_t priority, bool own) { return priority * 2 + (own ? 0 : 1); }
    Handler* handler(int index) {
        for (Handler& h : handlers)
            if (h.index == index) return &h;
        return nullptr;
    }
};

// Native functions of the `_kn` module (used only by core/prelude.py) and VM callbacks.
struct RuntimeNatives {
    static Agent* agent() { return static_cast<Agent*>(py_getvmctx()); }
    static World& world() { return agent()->rt->world_; }
    static Character& me() { return world().character(agent()->char_obj); }

    // (kind, id, name); an animal's kind is its species ("cow" / "chicken"), as the prelude's classes.
    // An animal's kind is its species, a workshop's — its type ("smithy"), as the prelude's classes.
    static const char* info_kind(const Object& o) {
        if (o.kind == ObjKind::Animal) return o.species.c_str();
        if (o.kind == ObjKind::Workshop) return o.workshop.c_str();
        if (o.kind == ObjKind::Storage && o.cellar) return "cellar";
        return kind_name(o.kind);
    }
    static void make_info(py_OutRef out, const Object& o) {
        py_TValue* t = py_newtuple(out, 3);
        py_newstr(&t[0], info_kind(o));
        py_newint(&t[1], o.id);
        py_newstr(&t[2], o.name.c_str());
    }

    static bool obj_arg(py_Ref v, ObjKind kind, bool any_kind, Object** out) {
        if (!py_checkint(v)) return false;
        Object* o = world().object(static_cast<int>(py_toint(v)));
        if (!o) return ValueError("такого об'єкта не існує");
        if (!any_kind && o->kind != kind) return TypeError("очікується %s, а отримано %s", kind_name(kind), kind_name(o->kind));
        *out = o;
        return true;
    }

    // ---- identity and lookup
    static bool self_id(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newint(py_retval(), agent()->char_obj);
        return true;
    }
    static bool self_name(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newstr(py_retval(), me().name.c_str());
        return true;
    }
    static bool find(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        PY_CHECK_ARG_TYPE(0, tp_str);
        const Object* o = world().find(py_tostr(py_arg(0)));
        if (o)
            make_info(py_retval(), *o);
        else
            py_newnone(py_retval());
        return true;
    }
    static bool list_objects(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        PY_CHECK_ARG_TYPE(0, tp_str);
        std::string kind = py_tostr(py_arg(0));
        py_newlist(py_r0());
        const bool any_animal = kind == "animal", any_workshop = kind == "workshop", any_storage = kind == "storage";
        const bool heated = kind == "heated_workshop";  // workshops with a stove
        for (const Object& o : world().objects()) {
            if (o.removed) continue;  // slaughtered animals are gone
            if (kind == info_kind(o) || (any_animal && o.kind == ObjKind::Animal) ||
                (any_workshop && o.kind == ObjKind::Workshop) || (any_storage && o.kind == ObjKind::Storage) ||
                (heated && o.kind == ObjKind::Workshop && world().has_stove(o)))
                make_info(py_list_emplace(py_r0()), o);
        }
        py_assign(py_retval(), py_r0());
        return true;
    }

    // ---- limited knowledge: state of other objects as this character knows it
    // An unknown object looks empty: numbers 0, flags False, nothing inside (field state "unknown").
    static const Knowledge* known(const Object* o) { return world().knowledge(me(), o->id); }
    static const Object& known_state(const Object* o) {
        static Object empty;
        const Knowledge* k = known(o);
        if (k) return k->state;
        empty = Object{};
        empty.kind = o->kind;
        empty.health = 0;  // unknown: numbers are 0
        return empty;
    }
    // A person as this character knows it: itself for real, anyone else from memory.
    static PersonView person_view(const Object* o) {
        if (o->id == agent()->char_obj) {
            const Character& c = me();
            return PersonView{c.hunger,       c.fatigue,     c.health,          c.warmth,
                              c.alive,        c.tool,        c.clothes,         c.carrying,
                              c.illness_left, c.injury_left, c.illness_treated, c.injury_treated,
                              c.mood};
        }
        const Knowledge* k = known(o);
        if (k) return k->person;
        PersonView none;
        none.alive = world().character(o->id).alive;
        return none;
    }
    static bool known_at(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Storage, true, &o)) return false;
        const Knowledge* k = o->id == agent()->char_obj ? nullptr : known(o);
        if (o->id == agent()->char_obj)
            py_newfloat(py_retval(), me().local_time);
        else if (k && k->time >= 0)
            py_newfloat(py_retval(), k->time);
        else
            py_newnone(py_retval());
        return true;
    }

    // ---- characters
    static bool char_stat(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Person, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        const PersonView c = person_view(o);
        std::string key = py_tostr(py_arg(1));
        if (key == "hunger")
            py_newfloat(py_retval(), c.hunger);
        else if (key == "fatigue")
            py_newfloat(py_retval(), c.fatigue);
        else if (key == "health")
            py_newfloat(py_retval(), c.health);
        else if (key == "warmth")
            py_newfloat(py_retval(), c.warmth);
        else if (key == "mood")
            py_newfloat(py_retval(), c.mood);
        else if (key == "alive")
            py_newbool(py_retval(), world().character(o->id).alive);  // public: everyone knows of a death
        else if (key == "sick")
            py_newbool(py_retval(), c.illness_left > 0);
        else if (key == "injured")
            py_newbool(py_retval(), c.injury_left > 0);
        else if (key == "needs_treatment")
            py_newbool(py_retval(), (c.illness_left > 0 && !c.illness_treated) || (c.injury_left > 0 && !c.injury_treated));
        else
            return ValueError("невідома властивість '%s'", key.c_str());
        return true;
    }
    // Dead people and animals not buried yet (public: everyone knows of a death).
    static bool bodies(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newlist(py_r0());
        for (const Object& o : world().objects())
            if (world().is_body(o)) make_info(py_list_emplace(py_r0()), o);
        py_assign(py_retval(), py_r0());
        return true;
    }
    static bool lantern_info(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Lantern, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        const Object& l = known_state(o);
        std::string key = py_tostr(py_arg(1));
        if (key == "fuel")
            py_newfloat(py_retval(), l.fuel);
        else if (key == "lit")
            py_newbool(py_retval(), l.lit);
        else
            return ValueError("невідома властивість '%s'", key.c_str());
        return true;
    }
    static bool self_diet(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newlist(py_r0());
        for (const std::string& food : world().diet(me())) py_newstr(py_list_emplace(py_r0()), food.c_str());
        py_assign(py_retval(), py_r0());
        return true;
    }
    // Skills are public: everyone in a village knows who is good at what.
    static bool char_skill(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Person, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        const std::string skill = py_tostr(py_arg(1));
        if (!is_skill(skill)) return ValueError("невідома навичка '%s'", skill.c_str());
        py_newfloat(py_retval(), world().skill_of(world().character(o->id), skill));
        return true;
    }
    static bool self_state(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        PY_CHECK_ARG_TYPE(0, tp_str);
        const Character& c = me();
        std::string key = py_tostr(py_arg(0));
        if (key == "clean")
            py_newbool(py_retval(), c.clean_until > c.local_time);
        else if (key == "hangover")
            py_newbool(py_retval(), c.hangover_until > c.local_time);
        else
            return ValueError("невідома властивість '%s'", key.c_str());
        return true;
    }
    static bool self_in_dark(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newbool(py_retval(), !world().lit_near(me()));
        return true;
    }
    static bool church_info(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Workshop, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        const Object& ch = known_state(o);
        std::string key = py_tostr(py_arg(1));
        if (key == "graves")
            py_newint(py_retval(), ch.graves);
        else if (key == "service_ready")
            py_newbool(py_retval(), ch.last_service < 0 ||
                                        me().local_time >= ch.last_service + world().balance().service_interval_hours * 3600.0);
        else
            return ValueError("невідома властивість '%s'", key.c_str());
        return true;
    }
    // People at this hospital who are ill or injured (as the character knows them); seen only from there.
    static bool hospital_patients(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* h = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Workshop, false, &h)) return false;
        py_newlist(py_r0());
        if (world().hospital_at(me()) == h) {
            for (const Character& p : world().characters()) {
                const Object* po = world().object(p.obj);
                if (!p.alive || world().hospital_at(p) != h) continue;
                const PersonView v = person_view(po);
                if (v.illness_left > 0 || v.injury_left > 0) make_info(py_list_emplace(py_r0()), *po);
            }
        }
        py_assign(py_retval(), py_r0());
        return true;
    }
    // (id, kind, quality, condition, where) — `where` is the storage or the person holding it.
    static void make_tool_info(py_OutRef out, const ToolItem& t, const Object& where) {
        py_TValue* tv = py_newtuple(out, 5);
        py_newint(&tv[0], t.id);
        py_newstr(&tv[1], t.kind.c_str());
        py_newint(&tv[2], t.quality);
        py_newfloat(&tv[3], t.condition);
        make_info(&tv[4], where);
    }
    static bool char_tool(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Person, false, &o)) return false;
        const PersonView c = person_view(o);
        if (c.tool.empty())
            py_newnone(py_retval());
        else
            make_tool_info(py_retval(), c.tool, *o);
        return true;
    }
    // Tools the character knows about: in storages (as last seen) and its own; `free` — whole ones lying
    // in a storage that nobody reserved.
    static bool list_tools(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        PY_CHECK_ARG_TYPE(0, tp_str);
        PY_CHECK_ARG_TYPE(1, tp_bool);
        const std::string kind = py_tostr(py_arg(0));
        const bool free = py_tobool(py_arg(1));
        World& w = world();
        py_newlist(py_r0());
        for (const Object& o : w.objects()) {
            if (o.kind != ObjKind::Storage || !o.built) continue;
            const Knowledge* k = known(&o);
            if (!k) continue;
            for (const ToolItem& t : k->state.tools) {
                if (!kind.empty() && t.kind != kind) continue;
                if (free && (t.broken() || t.reserved_by >= 0)) continue;
                make_tool_info(py_list_emplace(py_r0()), t, o);
            }
        }
        const Character& c = me();
        if (!free && !c.tool.empty() && (kind.empty() || c.tool.kind == kind))
            make_tool_info(py_list_emplace(py_r0()), c.tool, w.objects()[c.obj]);
        py_assign(py_retval(), py_r0());
        return true;
    }
    static bool char_clothes(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Person, false, &o)) return false;
        const PersonView c = person_view(o);
        if (c.clothes.empty())
            py_newnone(py_retval());
        else
            py_newstr(py_retval(), c.clothes.kind.c_str());
        return true;
    }
    static bool char_clothes_condition(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Person, false, &o)) return false;
        const PersonView c = person_view(o);
        if (c.clothes.empty())
            py_newnone(py_retval());
        else
            py_newfloat(py_retval(), c.clothes.condition);
        return true;
    }
    static bool char_carrying(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Person, false, &o)) return false;
        const PersonView c = person_view(o);
        py_newdict(py_r0());
        for (const auto& [res, n] : c.carrying) {
            if (n <= 0) continue;
            py_newint(py_r1(), n);
            if (!py_dict_setitem_by_str(py_r0(), res.c_str(), py_r1())) return false;
        }
        py_assign(py_retval(), py_r0());
        return true;
    }
    static bool char_home(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Person, false, &o)) return false;
        Character& c = world().character(o->id);
        if (c.home < 0)
            py_newnone(py_retval());
        else
            make_info(py_retval(), *world().object(c.home));
        return true;
    }

    // ---- buildings
    // A storage, or a trading post (what the merchant left there).
    static bool storage_arg(py_Ref v, Object** out) {
        if (!obj_arg(v, ObjKind::Storage, true, out)) return false;
        if ((*out)->kind != ObjKind::Storage && (*out)->kind != ObjKind::TradingPost)
            return TypeError("очікується склад або торговий пост");
        return true;
    }
    static bool storage_amount(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!storage_arg(py_arg(0), &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        const Object& s = known_state(o);
        auto it = s.resources.find(py_tostr(py_arg(1)));
        py_newint(py_retval(), it == s.resources.end() ? 0 : it->second);
        return true;
    }
    static bool storage_has_tool(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!storage_arg(py_arg(0), &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        const std::string tool = py_tostr(py_arg(1));
        bool free = false;
        for (const ToolItem& t : known_state(o).tools) free = free || (t.kind == tool && !t.broken() && t.reserved_by < 0);
        py_newbool(py_retval(), free);
        return true;
    }
    static bool house_info(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::House, true, &o)) return false;
        if (!world().has_stove(*o)) return TypeError("очікується будинок чи лікарня, а отримано %s", kind_name(o->kind));
        PY_CHECK_ARG_TYPE(1, tp_str);
        const Object& h = known_state(o);
        std::string key = py_tostr(py_arg(1));
        if (key == "firewood")
            py_newfloat(py_retval(), h.firewood);
        else if (key == "temperature")
            py_newfloat(py_retval(), h.temperature);
        else if (key == "heated")
            py_newbool(py_retval(), h.firewood > 0);
        else if (key == "beds")
            py_newint(py_retval(), h.beds);
        else
            return ValueError("невідома властивість '%s'", key.c_str());
        return true;
    }
    static bool house_chest(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::House, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        const Object& h = known_state(o);
        const std::string kind = py_tostr(py_arg(1));
        int n = 0;
        for (const Garment& g : h.chest) n += g.kind == kind ? 1 : 0;
        py_newint(py_retval(), n);
        return true;
    }
    static bool field_info(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Field, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        const Knowledge* k = known(o);
        const Object& f = known_state(o);
        std::string key = py_tostr(py_arg(1));
        if (key == "state")
            py_newstr(py_retval(), k ? field_state_name(f.field_state) : "unknown");
        else if (key == "crop")
            f.crop.empty() ? py_newnone(py_retval()) : py_newstr(py_retval(), f.crop.c_str());
        else if (key == "watered")
            py_newbool(py_retval(), k && f.watered_until > me().local_time);
        else if (key == "growth")
            py_newfloat(py_retval(), f.growth);
        else if (key == "grain_left")
            py_newint(py_retval(), f.grain_left);
        else
            return ValueError("невідома властивість '%s'", key.c_str());
        return true;
    }
    static bool building_info(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Storage, true, &o)) return false;
        if (!is_building(o->kind)) return TypeError("очікується будівля, а не %s", kind_name(o->kind));
        PY_CHECK_ARG_TYPE(1, tp_str);
        World& w = world();
        std::string key = py_tostr(py_arg(1));
        // Public: whether it stands, how many it houses and who lives there. Known: the construction site.
        if (key == "built") {
            py_newbool(py_retval(), o->built);
        } else if (key == "progress") {
            const Object& s = known_state(o);
            py_newfloat(py_retval(), o->built ? 1.0 : (s.build_total > 0 ? s.build_done / s.build_total : 0.0));
        } else if (key == "needs") {
            py_newdict(py_r0());
            const Object& s = known_state(o);
            if (!o->built)
                for (const auto& [res, n] : w.materials_missing(s)) {
                py_newint(py_r1(), n);
                if (!py_dict_setitem_by_str(py_r0(), res.c_str(), py_r1())) return false;
            }
            py_assign(py_retval(), py_r0());
        } else if (key == "capacity") {
            py_newint(py_retval(), o->capacity);
        } else if (key == "residents") {
            py_newlist(py_r0());
            if (o->kind == ObjKind::Barn) {
                for (const Object& a : w.objects())
                    if (a.kind == ObjKind::Animal && a.alive && !a.removed && a.home == o->id) make_info(py_list_emplace(py_r0()), a);
            } else {
                for (const Character& c : w.characters())
                    if (c.alive && c.home == o->id) make_info(py_list_emplace(py_r0()), w.objects()[c.obj]);
            }
            py_assign(py_retval(), py_r0());
        } else {
            return ValueError("невідома властивість '%s'", key.c_str());
        }
        return true;
    }
    // hunger, health, alive, produce — as this character knows them; home (its barn) — public.
    static bool animal_info(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Animal, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        std::string key = py_tostr(py_arg(1));
        if (key == "home") {
            const Object* barn = world().object(o->home);
            if (barn)
                make_info(py_retval(), *barn);
            else
                py_newnone(py_retval());
            return true;
        }
        const Object& a = known_state(o);
        if (key == "hunger")
            py_newfloat(py_retval(), a.hunger);
        else if (key == "health")
            py_newfloat(py_retval(), a.health);
        else if (key == "alive")
            py_newbool(py_retval(), a.alive);
        else if (key == "produce")
            py_newint(py_retval(), static_cast<py_i64>(std::floor(a.produce + kEps)));
        else
            return ValueError("невідома властивість '%s'", key.c_str());
        return true;
    }
    // ---- trade
    static bool town_hall_coins(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::TownHall, false, &o)) return false;
        py_newint(py_retval(), known_state(o).coins);
        return true;
    }
    // "for_sale": {item: n} (tools by kind); "orders": [(item, n)] — as this character knows the post.
    static bool post_info(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::TradingPost, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        const Object& post = known_state(o);
        std::string key = py_tostr(py_arg(1));
        if (key == "for_sale") {
            std::map<std::string, int> all = post.for_sale;
            for (const ToolItem& t : post.sale_tools) all[t.kind]++;
            py_newdict(py_r0());
            for (const auto& [item, n] : all) {
                if (n <= 0) continue;
                py_newint(py_r1(), n);
                if (!py_dict_setitem_by_str(py_r0(), item.c_str(), py_r1())) return false;
            }
        } else if (key == "orders") {
            py_newlist(py_r0());
            for (const auto& [item, n] : post.orders) {
                py_TValue* t = py_newtuple(py_list_emplace(py_r0()), 2);
                py_newstr(&t[0], item.c_str());
                py_newint(&t[1], n);
            }
        } else {
            return ValueError("невідома властивість '%s'", key.c_str());
        }
        py_assign(py_retval(), py_r0());
        return true;
    }
    // The merchant is public knowledge (news from the road): "here", "next_arrival", "days_until".
    static bool merchant_info(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        PY_CHECK_ARG_TYPE(0, tp_str);
        const Merchant& m = world().merchant();
        std::string key = py_tostr(py_arg(0));
        if (key == "here") {
            py_newbool(py_retval(), m.here);
        } else if (key == "next_arrival") {
            py_newfloat(py_retval(), m.next_arrival);
        } else if (key == "days_until") {
            const double now = me().local_time;
            const int days = m.here ? 0 : static_cast<int>(std::floor(m.next_arrival / kDay) - std::floor(now / kDay));
            py_newint(py_retval(), std::max(0, days));
        } else {
            return ValueError("невідома властивість '%s'", key.c_str());
        }
        return true;
    }
    // merchant_price(item, "price" | "offer"): coins per unit, or None.
    static bool merchant_price(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        PY_CHECK_ARG_TYPE(0, tp_str);
        PY_CHECK_ARG_TYPE(1, tp_str);
        const Merchant& m = world().merchant();
        const std::map<std::string, int>& table = std::string(py_tostr(py_arg(1))) == "offer" ? m.offers : m.prices;
        auto it = table.find(py_tostr(py_arg(0)));
        if (it == table.end())
            py_newnone(py_retval());
        else
            py_newint(py_retval(), it->second);
        return true;
    }
    static bool meadow_hay(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Meadow, false, &o)) return false;
        py_newint(py_retval(), static_cast<py_i64>(std::floor(known_state(o).hay + kEps)));
        return true;
    }
    static bool deposit_info(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Deposit, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        std::string key = py_tostr(py_arg(1));
        if (key == "resource")
            py_newstr(py_retval(), o->deposit.c_str());  // public: what is mined there
        else if (key == "left")
            py_newint(py_retval(), known(o) ? known_state(o).deposit_left : 0);
        else
            return ValueError("невідома властивість '%s'", key.c_str());
        return true;
    }
    static bool forest_info(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Forest, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        std::string key = py_tostr(py_arg(1));
        if (key == "saplings")
            py_newint(py_retval(), known(o) ? static_cast<py_i64>(known_state(o).saplings.size()) : 0);
        else if (key == "room")
            py_newint(py_retval(), known(o) ? world().forest_room(known_state(o)) : 0);
        else if (key == "capacity")
            py_newint(py_retval(), o->capacity);  // public: the size of the forest
        else
            return ValueError("невідома властивість '%s'", key.c_str());
        return true;
    }
    static bool forest_trees(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Forest, false, &o)) return false;
        py_newint(py_retval(), known(o) ? world().forest_trees_left(known_state(o)) : 0);
        return true;
    }

    // ---- world (as seen at the character's local time)
    static bool world_time(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newfloat(py_retval(), std::fmod(me().local_time, kDay));
        return true;
    }
    static bool world_day(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newint(py_retval(), static_cast<py_i64>(std::floor(me().local_time / kDay)) + 1);
        return true;
    }
    static bool world_now(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newfloat(py_retval(), me().local_time);
        return true;
    }
    static bool world_is_night(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newbool(py_retval(), world().is_night_at(me().local_time));
        return true;
    }
    static bool world_is_raining(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newbool(py_retval(), world().is_raining_at(me().local_time));
        return true;
    }
    static bool world_season(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newstr(py_retval(), season_name(world().season_at(me().local_time)));
        return true;
    }
    static bool world_season_days_left(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newint(py_retval(), world().season_days_left(me().local_time));
        return true;
    }
    static bool world_temperature(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(0);
        py_newfloat(py_retval(), world().outdoor_temperature(me().local_time));
        return true;
    }
    static bool travel_time(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Storage, true, &o)) return false;
        py_newfloat(py_retval(), world().travel_seconds(me(), o->pos));
        return true;
    }

    // ---- log and events
    static bool notify(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        PY_CHECK_ARG_TYPE(0, tp_str);
        world().log(agent()->char_obj, LogKind::Notify, py_tostr(py_arg(0)));
        py_newnone(py_retval());
        return true;
    }
    static bool register_handler(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(4);
        PY_CHECK_ARG_TYPE(0, tp_str);
        PY_CHECK_ARG_TYPE(1, tp_int);
        PY_CHECK_ARG_TYPE(2, tp_int);
        double tod = 0;
        if (!py_castfloat(py_arg(3), &tod)) return false;
        // A rule belongs to the file of the line that registered it (the @on line), as the trace hook saw it.
        const bool own = agent()->current_file == CodeFile::Own;
        int64_t priority = py_toint(py_arg(2));
        priority = std::clamp<int64_t>(priority, -1'000'000'000, 1'000'000'000);
        Agent::Handler h;
        h.event = py_tostr(py_arg(0));
        h.index = static_cast<int>(py_toint(py_arg(1)));
        h.rank = Agent::rule_rank(priority, own);
        h.tod = tod;
        if (h.event == "TIME") {
            const double now = me().local_time;
            h.next_fire = std::floor(now / kDay) * kDay + tod;
            if (h.next_fire <= now + kEps) h.next_fire += kDay;
        }
        agent()->handlers.push_back(h);
        py_newbool(py_retval(), own);  // the prelude replaces shared rules of the same name with own ones
        return true;
    }
    static bool unregister_handler(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        PY_CHECK_ARG_TYPE(0, tp_int);
        const int index = static_cast<int>(py_toint(py_arg(0)));
        Agent* a = agent();
        auto& hs = a->handlers;
        hs.erase(std::remove_if(hs.begin(), hs.end(), [&](const Agent::Handler& h) { return h.index == index; }), hs.end());
        auto& ps = a->pending;
        ps.erase(std::remove_if(ps.begin(), ps.end(), [&](const Agent::Pending& p) { return p.index == index; }), ps.end());
        py_newnone(py_retval());
        return true;
    }
    static bool set_dispatch(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        py_setdict(py_getmodule("_kn"), py_name("_dispatch"), py_arg(0));
        py_setdict(py_getmodule("_kn"), py_name("_check"), py_arg(1));
        py_newnone(py_retval());
        return true;
    }

    // ---- actions
    static bool get_param(py_Ref dict, const char* key, py_TValue* out, bool* found) {
        int r = py_dict_getitem_by_str(dict, key);
        if (r < 0) return false;
        *found = r == 1;
        if (*found) *out = *py_retval();
        return true;
    }
    static bool parse_params(py_Ref d, ActionParams& p) {
        if (py_isnone(d)) return true;
        if (!py_checktype(d, tp_dict)) return false;
        py_TValue v;
        bool found = false;
        if (!get_param(d, "amount", &v, &found)) return false;
        if (found) {
            if (!py_checkint(&v)) return false;
            p.amount = static_cast<int>(py_toint(&v));
        }
        if (!get_param(d, "count", &v, &found)) return false;
        if (found) {
            if (!py_checkint(&v)) return false;
            p.count = static_cast<int>(py_toint(&v));
        }
        struct FloatKey {
            const char* key;
            std::optional<double> ActionParams::*field;
        };
        const FloatKey float_keys[] = {{"until", &ActionParams::until},
                                       {"hours", &ActionParams::duration},
                                       {"seconds", &ActionParams::seconds},
                                       {"timeout", &ActionParams::timeout}};
        for (const auto& fk : float_keys) {
            if (!get_param(d, fk.key, &v, &found)) return false;
            if (!found) continue;
            double x = 0;
            if (!py_castfloat(&v, &x)) return false;
            p.*fk.field = x;
        }
        if (!get_param(d, "tool", &v, &found)) return false;
        if (found) {
            if (!py_checkstr(&v)) return false;
            p.tool = py_tostr(&v);
        }
        if (!get_param(d, "tool_id", &v, &found)) return false;
        if (found) {
            if (!py_checkint(&v)) return false;
            p.tool_id = static_cast<int>(py_toint(&v));
        }
        if (!get_param(d, "item", &v, &found)) return false;
        if (found) {
            if (!py_checkstr(&v)) return false;
            p.item = py_tostr(&v);
        }
        if (!get_param(d, "resource", &v, &found)) return false;
        if (found) {
            if (!py_checkstr(&v)) return false;
            p.resource = py_tostr(&v);
        }
        if (!get_param(d, "objects", &v, &found)) return false;
        if (found) {
            if (!py_checktype(&v, tp_list)) return false;
            const int n = py_list_len(&v);
            for (int i = 0; i < n; ++i) {
                py_ItemRef item = py_list_getitem(&v, i);
                if (!py_checkint(item)) return false;
                p.objects.push_back(static_cast<int>(py_toint(item)));
            }
        }
        return true;
    }
    static bool act(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(3);
        PY_CHECK_ARG_TYPE(0, tp_str);
        PY_CHECK_ARG_TYPE(1, tp_int);
        Agent* a = agent();
        if (!a || a->rt->current_ != a || mco_running() != a->co)
            return RuntimeError("дії можна виконувати лише під час роботи коду персонажа");
        if (a->in_condition) return RuntimeError("умова правила (@on(lambda: ...)) не може виконувати дії");
        std::string kind = py_tostr(py_arg(0));
        int target = static_cast<int>(py_toint(py_arg(1)));
        ActionParams p;
        if (!parse_params(py_arg(2), p)) return false;
        return a->rt->do_action(a, kind, target, p);
    }

    // ---- VM callbacks
    static void trace_cb(py_Frame* frame, enum py_TraceEvent ev) {
        if (ev != TRACE_EVENT_LINE) return;
        Agent* a = agent();
        if (!a || a->rt->current_ != a) return;
        int line = 0;
        const char* file = py_Frame_sourceloc(frame, &line);
        if (file && std::strcmp(file, kPreludeFile) == 0) return;  // API internals are free
        if (!a->in_condition && file) {
            if (std::strcmp(file, kPlayerFile) == 0) {
                a->current_line = line;
                a->current_file = CodeFile::Own;
            } else if (std::strcmp(file, kSharedFile) == 0) {
                a->current_line = line;
                a->current_file = CodeFile::Shared;
            }
        }
        a->rt->on_think(a);
    }
    static void print_cb(const char* s) {
        Agent* a = agent();
        if (!a) {
            std::fputs(s, stdout);
            return;
        }
        a->print_buf += s;
        a->rt->flush_print(a, false);
    }
    static void fiber_entry(mco_coro* co) {
        Agent* a = static_cast<Agent*>(mco_get_user_data(co));
        a->rt->fiber_main(a);
    }
};

// ------------------------------------------------------------------ ScriptRuntime

ScriptRuntime::ScriptRuntime(World& world) : world_(world) {
    if (!g_initialized) {
        py_initialize();
        py_kindle_setimportguard(import_guard);
        g_initialized = true;
    }
}

ScriptRuntime::~ScriptRuntime() {
    for (auto& a : agents_) {
        if (!a) continue;
        if (a->co) {
            mco_destroy(a->co);
            a->co = nullptr;
        }
        if (a->vm >= 0) {
            py_switchvm(a->vm);
            py_setvmctx(nullptr);
            g_free_vms.push_back(a->vm);
        }
    }
    py_switchvm(0);
}

Agent* ScriptRuntime::find_agent(int char_obj) const {
    int index = world_.objects().at(char_obj).char_index;
    if (index < 0 || index >= static_cast<int>(agents_.size())) return nullptr;
    return agents_[index].get();
}

Agent* ScriptRuntime::get_agent(int char_obj) {
    int index = world_.objects().at(char_obj).char_index;
    if (index < 0) return nullptr;
    if (index >= static_cast<int>(agents_.size())) agents_.resize(index + 1);
    if (!agents_[index]) {
        agents_[index] = std::make_unique<Agent>();
        agents_[index]->rt = this;
        agents_[index]->char_obj = char_obj;
    }
    return agents_[index].get();
}

AgentState ScriptRuntime::state(int char_obj) const {
    if (!world_.character(char_obj).alive) return AgentState::Dead;
    Agent* a = find_agent(char_obj);
    return a ? a->state : AgentState::NoCode;
}

std::string ScriptRuntime::source(int char_obj) const {
    Agent* a = find_agent(char_obj);
    return a ? a->own_source : std::string();
}

CodeFile ScriptRuntime::current_file(int char_obj) const {
    Agent* a = find_agent(char_obj);
    return a ? a->current_file : CodeFile::None;
}

int ScriptRuntime::current_line(int char_obj) const {
    Agent* a = find_agent(char_obj);
    return a ? a->current_line : 0;
}

void ScriptRuntime::init_vm(Agent* a) {
    if (a->vm < 0) {
        if (!g_free_vms.empty()) {
            a->vm = g_free_vms.back();
            g_free_vms.pop_back();
            a->vm_dirty = true;
        } else {
            if (g_next_vm >= PK_MAX_VM_COUNT) {
                std::fprintf(stderr, "kindle: забагато персонажів (максимум %d)\n", PK_MAX_VM_COUNT - 1);
                std::abort();
            }
            a->vm = g_next_vm++;
            a->vm_dirty = false;
        }
    }
    py_switchvm(a->vm);
    if (a->vm_dirty) py_resetvm();
    a->vm_dirty = true;
    py_setvmctx(a);

    py_Callbacks* cb = py_callbacks();
    cb->print = RuntimeNatives::print_cb;
    cb->importfile = deny_importfile;

    py_GlobalRef kn = py_newmodule("_kn");
    py_bindfunc(kn, "self_id", RuntimeNatives::self_id);
    py_bindfunc(kn, "self_name", RuntimeNatives::self_name);
    py_bindfunc(kn, "find", RuntimeNatives::find);
    py_bindfunc(kn, "list_objects", RuntimeNatives::list_objects);
    py_bindfunc(kn, "char_stat", RuntimeNatives::char_stat);
    py_bindfunc(kn, "char_tool", RuntimeNatives::char_tool);
    py_bindfunc(kn, "char_carrying", RuntimeNatives::char_carrying);
    py_bindfunc(kn, "char_clothes", RuntimeNatives::char_clothes);
    py_bindfunc(kn, "house_info", RuntimeNatives::house_info);
    py_bindfunc(kn, "house_chest", RuntimeNatives::house_chest);
    py_bindfunc(kn, "char_clothes_condition", RuntimeNatives::char_clothes_condition);
    py_bindfunc(kn, "deposit_info", RuntimeNatives::deposit_info);
    py_bindfunc(kn, "hospital_patients", RuntimeNatives::hospital_patients);
    py_bindfunc(kn, "bodies", RuntimeNatives::bodies);
    py_bindfunc(kn, "church_info", RuntimeNatives::church_info);
    py_bindfunc(kn, "lantern_info", RuntimeNatives::lantern_info);
    py_bindfunc(kn, "self_in_dark", RuntimeNatives::self_in_dark);
    py_bindfunc(kn, "self_diet", RuntimeNatives::self_diet);
    py_bindfunc(kn, "self_state", RuntimeNatives::self_state);
    py_bindfunc(kn, "char_skill", RuntimeNatives::char_skill);
    py_bindfunc(kn, "forest_info", RuntimeNatives::forest_info);
    py_bindfunc(kn, "field_info", RuntimeNatives::field_info);
    py_bindfunc(kn, "building_info", RuntimeNatives::building_info);
    py_bindfunc(kn, "world_season", RuntimeNatives::world_season);
    py_bindfunc(kn, "world_season_days_left", RuntimeNatives::world_season_days_left);
    py_bindfunc(kn, "world_temperature", RuntimeNatives::world_temperature);
    py_bindfunc(kn, "char_home", RuntimeNatives::char_home);
    py_bindfunc(kn, "storage_amount", RuntimeNatives::storage_amount);
    py_bindfunc(kn, "storage_has_tool", RuntimeNatives::storage_has_tool);
    py_bindfunc(kn, "forest_trees", RuntimeNatives::forest_trees);
    py_bindfunc(kn, "animal_info", RuntimeNatives::animal_info);
    py_bindfunc(kn, "meadow_hay", RuntimeNatives::meadow_hay);
    py_bindfunc(kn, "town_hall_coins", RuntimeNatives::town_hall_coins);
    py_bindfunc(kn, "post_info", RuntimeNatives::post_info);
    py_bindfunc(kn, "merchant_info", RuntimeNatives::merchant_info);
    py_bindfunc(kn, "merchant_price", RuntimeNatives::merchant_price);
    py_bindfunc(kn, "world_time", RuntimeNatives::world_time);
    py_bindfunc(kn, "world_day", RuntimeNatives::world_day);
    py_bindfunc(kn, "world_now", RuntimeNatives::world_now);
    py_bindfunc(kn, "world_is_night", RuntimeNatives::world_is_night);
    py_bindfunc(kn, "world_is_raining", RuntimeNatives::world_is_raining);
    py_bindfunc(kn, "travel_time", RuntimeNatives::travel_time);
    py_bindfunc(kn, "known_at", RuntimeNatives::known_at);
    py_bindfunc(kn, "list_tools", RuntimeNatives::list_tools);
    py_bindfunc(kn, "notify", RuntimeNatives::notify);
    py_bindfunc(kn, "register_handler", RuntimeNatives::register_handler);
    py_bindfunc(kn, "unregister_handler", RuntimeNatives::unregister_handler);
    py_bindfunc(kn, "set_dispatch", RuntimeNatives::set_dispatch);
    py_bindfunc(kn, "act", RuntimeNatives::act);

    py_GlobalRef module = py_newmodule("kindle");
    g_allow_import = true;
    bool ok = py_exec(reinterpret_cast<const char*>(kPreludeSource), kPreludeFile, EXEC_MODE, module);
    g_allow_import = false;
    if (!ok) {
        char* msg = py_formatexc();
        std::fprintf(stderr, "kindle: помилка в prelude.py:\n%s\n", msg ? msg : "?");
        std::abort();
    }
    for (const auto& [name, value] : world_.globals()) apply_global(name, &value);
    py_sys_settrace(RuntimeNatives::trace_cb, true);
}

void ScriptRuntime::stop_agent(Agent* a) {
    if (a->co) {
        mco_destroy(a->co);
        a->co = nullptr;
    }
    world_.cancel_actions(a->char_obj);
    flush_print(a, true);
}

void ScriptRuntime::set_code(int char_obj, const std::string& source) {
    Agent* a = get_agent(char_obj);
    const bool restart = a->state != AgentState::NoCode;
    a->own_source = source;
    start_agent(a, restart);
}

void ScriptRuntime::set_shared_code(const std::string& source, bool restart) {
    shared_source_ = source;
    if (!restart) return;
    world_.log(-1, LogKind::Info, "Спільний код змінено — усі персонажі почали свій код спочатку");
    for (const Character& c : world_.characters())
        if (c.alive) start_agent(get_agent(c.obj), false);
}

// (Re)starts a character's program: shared code + own code + main(), from the top.
void ScriptRuntime::start_agent(Agent* a, bool log_restart) {
    stop_agent(a);
    a->handlers.clear();
    a->pending.clear();
    a->running_ranks.clear();
    a->current_line = 0;
    a->current_file = CodeFile::None;
    a->print_buf.clear();
    a->in_condition = false;

    Character& c = world_.character(a->char_obj);
    if (!c.alive) {
        a->state = AgentState::Dead;
        return;
    }
    if (shared_source_.empty() && a->own_source.empty()) {
        a->state = AgentState::NoCode;
        return;
    }
    init_vm(a);
    if (log_restart)
        world_.log(a->char_obj, LogKind::Info, "Код персонажа " + quote(c.name) + " змінено — виконання почалося спочатку");

    if (!shared_source_.empty() && !py_compile(shared_source_.c_str(), kSharedFile, EXEC_MODE, false)) {
        report_exception(a, "помилка в спільному коді");
        py_clearexc(nullptr);
        a->state = AgentState::Crashed;
        return;
    }
    if (!a->own_source.empty() && !py_compile(a->own_source.c_str(), kPlayerFile, EXEC_MODE, false)) {
        report_exception(a, "помилка в коді");
        py_clearexc(nullptr);
        a->state = AgentState::Crashed;
        return;
    }
    mco_desc desc = mco_desc_init(RuntimeNatives::fiber_entry, kFiberStackSize);
    desc.user_data = a;
    if (mco_create(&a->co, &desc) != MCO_SUCCESS) {
        std::fprintf(stderr, "kindle: не вдалося створити співпрограму\n");
        std::abort();
    }
    a->state = AgentState::Running;
}

void ScriptRuntime::post_event(const std::string& event) {
    for (auto& a : agents_) {
        if (!a || a->state != AgentState::Running) continue;
        for (const auto& h : a->handlers)
            if (h.event == event) a->pending.push_back({event, h.index, h.rank, a->next_seq++});
    }
}

bool ScriptRuntime::valid_global_name(const std::string& name) {
    if (name.empty() || name.size() > 40 || name[0] < 'A' || name[0] > 'Z') return false;
    for (char ch : name)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')) return false;
    return true;
}

bool ScriptRuntime::set_global(const std::string& name, const World::GlobalValue& value, std::string& error) {
    if (!valid_global_name(name)) {
        error = "Назва глобальної змінної — великі латинські літери, цифри та _ (наприклад, WINTER_MODE)";
        return false;
    }
    world_.set_global(name, value);
    for (auto& a : agents_) {
        if (!a || a->vm < 0) continue;
        py_switchvm(a->vm);
        apply_global(name, &value);
    }
    return true;
}

bool ScriptRuntime::remove_global(const std::string& name) {
    if (!world_.remove_global(name)) return false;
    for (auto& a : agents_) {
        if (!a || a->vm < 0) continue;
        py_switchvm(a->vm);
        apply_global(name, nullptr);
    }
    return true;
}

void ScriptRuntime::run_until(int char_obj, double tick_end) {
    Character& c = world_.character(char_obj);
    if (!c.alive) return;
    Agent* a = find_agent(char_obj);
    if (a && a->state == AgentState::Running && a->co && c.local_time < tick_end) {
        a->tick_end = tick_end;
        py_switchvm(a->vm);
        current_ = a;
        mco_result res = mco_resume(a->co);
        current_ = nullptr;
        if (res != MCO_SUCCESS) {
            std::fprintf(stderr, "kindle: mco_resume: %s\n", mco_result_description(res));
            std::abort();
        }
        if (mco_status(a->co) == MCO_DEAD || a->state != AgentState::Running) {
            mco_destroy(a->co);
            a->co = nullptr;
        }
    }
    // Code finished/crashed/absent: the character just stands there for the rest of the tick.
    if (c.alive && c.local_time < tick_end) world_.pass_time(c, tick_end - c.local_time, Activity::Idle);
}

// Runs inside the coroutine.
void ScriptRuntime::fiber_main(Agent* a) {
    bool ok = true;
    if (!shared_source_.empty()) ok = py_exec(shared_source_.c_str(), kSharedFile, EXEC_MODE, nullptr);
    if (ok && !a->own_source.empty()) ok = py_exec(a->own_source.c_str(), kPlayerFile, EXEC_MODE, nullptr);
    if (ok) {
        // The settlement's daily routine, unless the character's own code never returned.
        py_ItemRef main = py_getglobal(py_name("main"));
        if (main && py_callable(main)) {
            py_TValue fn = *main;
            ok = py_call(&fn, 0, py_r0());
        }
    }
    flush_print(a, true);
    world_.cancel_actions(a->char_obj);
    if (ok) {
        a->state = AgentState::Finished;
        world_.log(a->char_obj, LogKind::Info,
                   "Код персонажа " + quote(world_.character(a->char_obj).name) + " завершився — персонаж стоїть без діла");
    } else {
        report_exception(a, "необроблений виняток");
        py_clearexc(nullptr);
        a->state = AgentState::Crashed;
    }
}

// Called for every executed line of player code (inside the coroutine).
void ScriptRuntime::on_think(Agent* a) {
    if (a->in_condition) {  // conditions of rules cost no game time, but must stay short
        if (++a->condition_lines > kMaxConditionLines) {
            world_.log(a->char_obj, LogKind::Error,
                       quote(world_.character(a->char_obj).name) + ": умова правила виконує понад " +
                           std::to_string(kMaxConditionLines) + " рядків — код зупинено");
            world_.cancel_actions(a->char_obj);
            abandon(a, AgentState::Crashed);
        }
        return;
    }
    Character& c = world_.character(a->char_obj);
    world_.pass_time(c, world_.balance().think_seconds_per_line, Activity::Idle);
    if (!c.alive) {
        world_.cancel_actions(a->char_obj);
        abandon(a, AgentState::Dead);
    }
    if (c.local_time >= a->tick_end - kEps) mco_yield(a->co);
}

// Leaves the coroutine for good: run_until() destroys it, set_code() resets the VM.
void ScriptRuntime::abandon(Agent* a, AgentState state) {
    a->state = state;
    mco_yield(a->co);
    std::fprintf(stderr, "kindle: покинута співпрограма відновилася\n");
    std::abort();
}

// Runs inside the coroutine: performs one game action, yielding at the end of every tick.
bool ScriptRuntime::do_action(Agent* a, const std::string& kind, int target, const ActionParams& params) {
    BeginResult begin = world_.begin_action(a->char_obj, kind, target, params);
    if (begin.error) return raise_action_error(*begin.error);
    const int id = begin.action_id;
    for (;;) {
        // @on handlers run here; the action stays on the character's stack, its progress intact.
        if (!deliver_events(a)) {
            world_.cancel_actions(a->char_obj);
            abandon(a, AgentState::Crashed);
        }
        Character& c = world_.character(a->char_obj);
        if (!c.alive) {
            world_.cancel_actions(a->char_obj);
            abandon(a, AgentState::Dead);
        }
        const double budget = a->tick_end - c.local_time;
        if (budget <= kEps) {
            mco_yield(a->co);
            continue;
        }
        StepResult r = world_.step_action(id, budget);
        if (!c.alive) {
            world_.cancel_actions(a->char_obj);
            abandon(a, AgentState::Dead);
        }
        if (r.status == StepStatus::Running) {
            if (r.used + kEps < budget) world_.pass_time(c, budget - r.used, Activity::Idle);  // safety net
            continue;
        }
        world_.end_action(id);
        if (r.status == StepStatus::Failed) return raise_action_error(r.error);
        switch (r.value.kind) {
            case ValueKind::None: py_newnone(py_retval()); break;
            case ValueKind::Int: py_newint(py_retval(), r.value.i); break;
            case ValueKind::Bool: py_newbool(py_retval(), r.value.b); break;
        }
        return true;
    }
}

// Runs pending @on handlers that outrank the handler currently running (if any).
// Returns false if a handler raised an exception (already reported).
bool ScriptRuntime::deliver_events(Agent* a) {
    if (!check_rules(a)) return false;
    for (;;) {
        const int64_t current = a->running_ranks.empty() ? INT64_MAX : a->running_ranks.back();
        int best = -1;
        for (int i = 0; i < static_cast<int>(a->pending.size()); ++i) {
            const auto& p = a->pending[i];
            if (p.rank >= current) continue;
            if (best < 0 || p.rank < a->pending[best].rank ||
                (p.rank == a->pending[best].rank && p.seq < a->pending[best].seq))
                best = i;
        }
        if (best < 0) return true;
        Agent::Pending ev = a->pending[best];
        a->pending.erase(a->pending.begin() + best);

        // A condition rule may have waited for a rule that outranks it; it runs only if the condition
        // still holds (night_sleep that waited for an evening shift until 07:00 must not sleep till 06:00).
        if (ev.event == "CONDITION") {
            bool value = false;
            if (!eval_condition(a, ev.index, value)) return false;
            Agent::Handler* h = a->handler(ev.index);  // the condition may have called off(...)
            if (!h) continue;
            if (!value) {
                h->last = 0;
                continue;
            }
        }

        py_ItemRef dispatch = py_getdict(py_getmodule("_kn"), py_name("_dispatch"));
        if (!dispatch) {
            world_.log(a->char_obj, LogKind::Error, "Внутрішня помилка: немає диспетчера подій");
            return false;
        }
        py_TValue fn = *dispatch;
        py_newint(py_r0(), ev.index);
        py_newstr(py_r1(), ev.event.c_str());
        a->running_ranks.push_back(ev.rank);
        const int interrupted_line = a->current_line;
        const CodeFile interrupted_file = a->current_file;
        bool ok = py_call(&fn, 2, py_r0());
        a->running_ranks.pop_back();
        a->current_line = interrupted_line;  // back at the line of the interrupted action
        a->current_file = interrupted_file;
        if (!ok) {
            report_exception(a, ("помилка в обробнику події " + ev.event).c_str());
            return false;
        }
    }
}

// Queues TIME rules whose moment came and CONDITION rules whose condition became true.
// Returns false if a condition raised an exception (already reported).
bool ScriptRuntime::check_rules(Agent* a) {
    if (a->in_condition) return true;
    const double now = world_.character(a->char_obj).local_time;
    for (size_t i = 0; i < a->handlers.size(); ++i) {
        Agent::Handler& h = a->handlers[i];
        if (h.event == "TIME") {
            if (now + kEps < h.next_fire) continue;
            a->pending.push_back({h.event, h.index, h.rank, a->next_seq++});
            while (h.next_fire <= now + kEps) h.next_fire += kDay;
        } else if (h.event == "CONDITION") {
            if (h.last_check >= 0 && now - h.last_check < kRuleCheckSeconds) continue;
            h.last_check = now;
            const int index = h.index;
            bool value = false;
            if (!eval_condition(a, index, value)) return false;
            // The condition may have called off(...): find the rule again.
            Agent::Handler* again = a->handler(index);
            if (!again) continue;
            const bool queued = std::any_of(a->pending.begin(), a->pending.end(),
                                            [&](const Agent::Pending& p) { return p.index == index; });
            if (value && again->last != 1 && !queued) a->pending.push_back({again->event, index, again->rank, a->next_seq++});
            again->last = value ? 1 : 0;
        }
    }
    return true;
}

bool ScriptRuntime::eval_condition(Agent* a, int index, bool& value) {
    py_ItemRef check = py_getdict(py_getmodule("_kn"), py_name("_check"));
    if (!check) return true;
    py_TValue fn = *check;
    py_newint(py_r0(), index);
    a->in_condition = true;
    a->condition_lines = 0;
    const bool ok = py_call(&fn, 1, py_r0());
    a->in_condition = false;
    if (!ok) {
        report_exception(a, "помилка в умові правила");
        return false;
    }
    value = py_tobool(py_retval());
    return true;
}

void ScriptRuntime::report_exception(Agent* a, const char* context) {
    char* raw = py_formatexc();
    std::string tb = raw ? raw : "";
    if (raw) py_free(raw);
    CodeFile file = CodeFile::None;
    int line = player_line_from_traceback(tb, file);
    std::string text = quote(world_.character(a->char_obj).name) + ": " + context;
    if (line > 0 && file == CodeFile::Shared) text += " (спільний код, рядок " + std::to_string(line) + ")";
    if (line > 0 && file == CodeFile::Own) text += " (рядок " + std::to_string(line) + ")";
    text += " — " + last_nonempty_line(tb);
    world_.log(a->char_obj, LogKind::Error, text, tb);
}

void ScriptRuntime::flush_print(Agent* a, bool force) {
    size_t pos;
    while ((pos = a->print_buf.find('\n')) != std::string::npos) {
        world_.log(a->char_obj, LogKind::Print, a->print_buf.substr(0, pos));
        a->print_buf.erase(0, pos + 1);
    }
    if (force && !a->print_buf.empty()) {
        world_.log(a->char_obj, LogKind::Print, a->print_buf);
        a->print_buf.clear();
    }
}

}  // namespace kindle
