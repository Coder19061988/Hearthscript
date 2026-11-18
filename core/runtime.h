#pragma once

#include <memory>
#include <string>
#include <vector>

namespace kindle {

class World;
struct ActionParams;
struct Agent;
struct RuntimeNatives;

enum class AgentState {
    NoCode,    // no script assigned yet
    Running,   // script is executing (possibly suspended inside an action)
    Finished,  // script reached its end; the character stands idle
    Crashed,   // unhandled exception or syntax error; the character stands idle
    Dead,      // the character died
};
const char* agent_state_name(AgentState s);

// Executes player scripts: one pocketpy VM and one stackful coroutine (minicoro) per character.
//
// Every game action called from Python (`me.chop_wood(...)`) runs inside the coroutine:
// it advances the world in time slices and yields whenever the character's time budget for
// the current tick is spent. Every executed line costs "thinking" time (trace hook), so even
// an empty `while True: pass` yields regularly. Events posted with post_event() run the
// matching @on handlers at the next action point, then the interrupted action continues.
class ScriptRuntime {
public:
    explicit ScriptRuntime(World& world);
    ~ScriptRuntime();
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    // Assigns code to a character. If code was already running, the current action is
    // interrupted (its progress stays in the world) and the new code starts from the top.
    void set_code(int char_obj, const std::string& source);

    // Queues an event (e.g. "RAIN") for every character whose code registered a handler for it.
    void post_event(const std::string& event);

    // Runs a character until its local time reaches `tick_end` (idle time if its code is not running).
    void run_until(int char_obj, double tick_end);

    AgentState state(int char_obj) const;
    int current_line(int char_obj) const;  // line of player code being executed (0 if unknown)
    World& world() { return world_; }

private:
    friend struct RuntimeNatives;

    Agent* find_agent(int char_obj) const;
    Agent* get_agent(int char_obj);
    void init_vm(Agent* a);
    void stop_agent(Agent* a);
    void fiber_main(Agent* a);
    void on_think(Agent* a);
    [[noreturn]] void abandon(Agent* a, AgentState state);
    bool do_action(Agent* a, const std::string& kind, int target, const ActionParams& params);
    bool deliver_events(Agent* a);
    void report_exception(Agent* a, const char* context);
    void flush_print(Agent* a, bool force);

    World& world_;
    std::vector<std::unique_ptr<Agent>> agents_;
    Agent* current_ = nullptr;  // agent whose coroutine is running right now
};

}  // namespace kindle
