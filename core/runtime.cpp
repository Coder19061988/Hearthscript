#include "runtime.h"

#include <algorithm>
#include <climits>
#include <cmath>
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
constexpr const char* kPreludeFile = "<kindle>";
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

// Finds the innermost line of player code in a pocketpy traceback.
int player_line_from_traceback(const std::string& tb) {
    const std::string needle = std::string("File \"") + kPlayerFile + "\", line ";
    size_t pos = tb.rfind(needle);
    if (pos == std::string::npos) return 0;
    return std::atoi(tb.c_str() + pos + needle.size());
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
        case AgentState::Dead: return "помер";
    }
    return "?";
}

struct Agent {
    ScriptRuntime* rt = nullptr;
    int char_obj = -1;
    int vm = -1;
    bool vm_dirty = false;  // VM was used before and must be reset
    mco_coro* co = nullptr;
    std::string source;
    AgentState state = AgentState::NoCode;
    double tick_end = 0;
    int current_line = 0;
    std::string print_buf;

    struct Handler {
        std::string event;
        int index;  // position in the prelude's `_handlers` list
        int priority;
    };
    struct Pending {
        std::string event;
        int index;
        int priority;
        uint64_t seq;
    };
    std::vector<Handler> handlers;
    std::vector<Pending> pending;
    std::vector<int> running_priorities;  // priorities of handlers currently on the stack
    uint64_t next_seq = 0;
};

// Native functions of the `_kn` module (used only by core/prelude.py) and VM callbacks.
struct RuntimeNatives {
    static Agent* agent() { return static_cast<Agent*>(py_getvmctx()); }
    static World& world() { return agent()->rt->world_; }
    static Character& me() { return world().character(agent()->char_obj); }

    static void make_info(py_OutRef out, const Object& o) {
        py_TValue* t = py_newtuple(out, 3);
        py_newstr(&t[0], kind_name(o.kind));
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
        for (const Object& o : world().objects())
            if (kind == kind_name(o.kind)) make_info(py_list_emplace(py_r0()), o);
        py_assign(py_retval(), py_r0());
        return true;
    }

    // ---- characters
    static bool char_stat(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Person, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        Character& c = world().character(o->id);
        std::string key = py_tostr(py_arg(1));
        if (key == "hunger")
            py_newfloat(py_retval(), c.hunger);
        else if (key == "fatigue")
            py_newfloat(py_retval(), c.fatigue);
        else if (key == "health")
            py_newfloat(py_retval(), c.health);
        else
            return ValueError("невідома властивість '%s'", key.c_str());
        return true;
    }
    static bool char_tool(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Person, false, &o)) return false;
        Character& c = world().character(o->id);
        if (c.tool.empty())
            py_newnone(py_retval());
        else
            py_newstr(py_retval(), c.tool.c_str());
        return true;
    }
    static bool char_carrying(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Person, false, &o)) return false;
        Character& c = world().character(o->id);
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
    static bool storage_amount(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Storage, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        auto it = o->resources.find(py_tostr(py_arg(1)));
        py_newint(py_retval(), it == o->resources.end() ? 0 : it->second);
        return true;
    }
    static bool storage_has_tool(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(2);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Storage, false, &o)) return false;
        PY_CHECK_ARG_TYPE(1, tp_str);
        bool axe = std::strcmp(py_tostr(py_arg(1)), "axe") == 0;
        py_newbool(py_retval(), axe && o->axes - o->axes_reserved > 0);
        return true;
    }
    static bool forest_trees(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        Object* o = nullptr;
        if (!obj_arg(py_arg(0), ObjKind::Forest, false, &o)) return false;
        py_newint(py_retval(), world().forest_trees_left(*o));
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
        PY_CHECK_ARGC(3);
        PY_CHECK_ARG_TYPE(0, tp_str);
        PY_CHECK_ARG_TYPE(1, tp_int);
        PY_CHECK_ARG_TYPE(2, tp_int);
        agent()->handlers.push_back({py_tostr(py_arg(0)), static_cast<int>(py_toint(py_arg(1))),
                                     static_cast<int>(py_toint(py_arg(2)))});
        py_newnone(py_retval());
        return true;
    }
    static bool set_dispatch(int argc, py_StackRef argv) {
        PY_CHECK_ARGC(1);
        py_setdict(py_getmodule("_kn"), py_name("_dispatch"), py_arg(0));
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
        if (!get_param(d, "resource", &v, &found)) return false;
        if (found) {
            if (!py_checkstr(&v)) return false;
            p.resource = py_tostr(&v);
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
        if (file && std::strcmp(file, kPlayerFile) == 0) a->current_line = line;
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
    py_bindfunc(kn, "char_home", RuntimeNatives::char_home);
    py_bindfunc(kn, "storage_amount", RuntimeNatives::storage_amount);
    py_bindfunc(kn, "storage_has_tool", RuntimeNatives::storage_has_tool);
    py_bindfunc(kn, "forest_trees", RuntimeNatives::forest_trees);
    py_bindfunc(kn, "world_time", RuntimeNatives::world_time);
    py_bindfunc(kn, "world_day", RuntimeNatives::world_day);
    py_bindfunc(kn, "world_now", RuntimeNatives::world_now);
    py_bindfunc(kn, "world_is_night", RuntimeNatives::world_is_night);
    py_bindfunc(kn, "world_is_raining", RuntimeNatives::world_is_raining);
    py_bindfunc(kn, "travel_time", RuntimeNatives::travel_time);
    py_bindfunc(kn, "notify", RuntimeNatives::notify);
    py_bindfunc(kn, "register_handler", RuntimeNatives::register_handler);
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
    stop_agent(a);
    a->source = source;
    a->handlers.clear();
    a->pending.clear();
    a->running_priorities.clear();
    a->current_line = 0;
    a->print_buf.clear();

    Character& c = world_.character(char_obj);
    if (!c.alive) {
        a->state = AgentState::Dead;
        return;
    }
    init_vm(a);
    if (restart) world_.log(char_obj, LogKind::Info, "Код персонажа " + quote(c.name) + " змінено — виконання почалося спочатку");

    if (!py_compile(source.c_str(), kPlayerFile, EXEC_MODE, false)) {
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
            if (h.event == event) a->pending.push_back({event, h.index, h.priority, a->next_seq++});
    }
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
    bool ok = py_exec(a->source.c_str(), kPlayerFile, EXEC_MODE, nullptr);
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
    for (;;) {
        const int current = a->running_priorities.empty() ? INT_MAX : a->running_priorities.back();
        int best = -1;
        for (int i = 0; i < static_cast<int>(a->pending.size()); ++i) {
            const auto& p = a->pending[i];
            if (p.priority >= current) continue;
            if (best < 0 || p.priority < a->pending[best].priority ||
                (p.priority == a->pending[best].priority && p.seq < a->pending[best].seq))
                best = i;
        }
        if (best < 0) return true;
        Agent::Pending ev = a->pending[best];
        a->pending.erase(a->pending.begin() + best);

        py_ItemRef dispatch = py_getdict(py_getmodule("_kn"), py_name("_dispatch"));
        if (!dispatch) {
            world_.log(a->char_obj, LogKind::Error, "Внутрішня помилка: немає диспетчера подій");
            return false;
        }
        py_TValue fn = *dispatch;
        py_newint(py_r0(), ev.index);
        py_newstr(py_r1(), ev.event.c_str());
        a->running_priorities.push_back(ev.priority);
        bool ok = py_call(&fn, 2, py_r0());
        a->running_priorities.pop_back();
        if (!ok) {
            report_exception(a, ("помилка в обробнику події " + ev.event).c_str());
            return false;
        }
    }
}

void ScriptRuntime::report_exception(Agent* a, const char* context) {
    char* raw = py_formatexc();
    std::string tb = raw ? raw : "";
    if (raw) py_free(raw);
    int line = player_line_from_traceback(tb);
    std::string text = quote(world_.character(a->char_obj).name) + ": " + context;
    if (line > 0) text += " (рядок " + std::to_string(line) + ")";
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
