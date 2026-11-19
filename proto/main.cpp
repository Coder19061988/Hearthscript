// Kindlescript console prototype: runs player scripts without graphics, scenario tests, benchmark.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "proto.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

using namespace kindle;

namespace proto {

std::string source_path(const std::string& relative) { return std::string(KINDLE_SOURCE_DIR) + "/" + relative; }

Balance load_balance(const std::string& path, bool verbose) {
    Balance b;
    std::string error;
    std::vector<std::string> warnings;
    if (!b.load_json_file(path, error, warnings)) {
        std::fprintf(stderr, "Баланс: %s — використано значення за замовчуванням\n", error.c_str());
        return Balance{};
    }
    for (const auto& w : warnings) std::fprintf(stderr, "Баланс: %s\n", w.c_str());
    if (verbose) std::printf("Баланс: %s\n", path.c_str());
    return b;
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    if (out.size() >= 3 && out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);
    return true;
}

std::string format_entry(World& world, const LogEntry& e) {
    std::string who;
    if (e.object_id >= 0) who = world.objects()[e.object_id].name;
    std::string prefix = "[" + world.format_time(e.time) + "] ";
    switch (e.kind) {
        case LogKind::Info: return prefix + e.text;
        case LogKind::Notify: return prefix + who + ": " + e.text;
        case LogKind::Print: return prefix + who + " (print): " + e.text;
        case LogKind::Warning: return prefix + "УВАГА: " + e.text;
        case LogKind::Error: return prefix + "ПОМИЛКА: " + e.text;
        case LogKind::Death: return prefix + "СМЕРТЬ: " + e.text;
        case LogKind::Outcome: return prefix + "ПІДСУМОК: " + e.text;
    }
    return prefix + e.text;
}

double memory_mb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc))
        return static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
#endif
    return 0;
}

}  // namespace proto

namespace {

void usage() {
    std::printf(
        "Kindlescript — консольний прототип\n"
        "\n"
        "  kindle_proto run <скрипт.py> [параметри]   запустити скрипт для всіх персонажів\n"
        "      --days N      скільки ігрових днів моделювати (1)\n"
        "      --chars N     кількість персонажів (1)\n"
        "      --axes N      сокир на складі (1)\n"
        "      --bread N     хліба на складі (10)\n"
        "      --seed N      зерно випадковості (1)\n"
        "      --no-rain     без дощу\n"
        "      --quiet       не друкувати журнал подій\n"
        "      --balance F   файл балансу (config/balance.json)\n"
        "  kindle_proto test [назва...]                сценарні тести (усі або вибрані)\n"
        "  kindle_proto list                           перелік тестів\n"
        "  kindle_proto bench [--chars N] [--days N] [--script F]   навантажувальний тест\n");
}

struct Options {
    std::vector<std::string> positional;
    StartScene scene;
    double days = 1;
    uint64_t seed = 1;
    bool rain = true;
    bool quiet = false;
    std::string balance = proto::source_path("config/balance.json");
    std::string script;
};

bool parse_options(const std::vector<std::string>& args, Options& o) {
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](double& out) {
            if (i + 1 >= args.size()) return false;
            out = std::atof(args[++i].c_str());
            return true;
        };
        double v = 0;
        if (a == "--days") {
            if (!next(o.days)) return false;
        } else if (a == "--chars") {
            if (!next(v)) return false;
            o.scene.characters = static_cast<int>(v);
        } else if (a == "--axes") {
            if (!next(v)) return false;
            o.scene.axes = static_cast<int>(v);
        } else if (a == "--bread") {
            if (!next(v)) return false;
            o.scene.bread = static_cast<int>(v);
        } else if (a == "--seed") {
            if (!next(v)) return false;
            o.seed = static_cast<uint64_t>(v);
        } else if (a == "--no-rain") {
            o.rain = false;
        } else if (a == "--quiet") {
            o.quiet = true;
        } else if (a == "--balance") {
            if (i + 1 >= args.size()) return false;
            o.balance = args[++i];
        } else if (a == "--script") {
            if (i + 1 >= args.size()) return false;
            o.script = args[++i];
        } else if (a.rfind("--", 0) == 0) {
            std::fprintf(stderr, "Невідомий параметр %s\n", a.c_str());
            return false;
        } else {
            o.positional.push_back(a);
        }
    }
    return true;
}

void print_summary(Simulation& sim) {
    World& w = sim.world();
    std::printf("\n--- Підсумок: %s ---\n", w.format_time(w.now()).c_str());
    int shown = 0;
    for (Character& c : w.characters()) {
        if (shown++ == 10) {
            std::printf("  ... ще %d персонажів\n", static_cast<int>(w.characters().size()) - 10);
            break;
        }
        std::string carrying;
        for (const auto& [res, n] : c.carrying) carrying += " " + res + "=" + std::to_string(n);
        std::printf("  %-10s код: %-18s голод %3.0f  втома %3.0f  здоров'я %3.0f  несе:%s\n", c.name.c_str(),
                    agent_state_name(sim.runtime().state(c.obj)), c.hunger, c.fatigue, c.health,
                    carrying.empty() ? " —" : carrying.c_str());
    }
    for (const Object& o : w.objects()) {
        if (o.kind != ObjKind::Storage) continue;
        std::string res;
        for (const auto& [name, n] : o.resources) res += " " + name + "=" + std::to_string(n);
        std::string tools;
        for (const ToolItem& t : o.tools)
            tools += " " + t.kind + "(" + std::to_string(static_cast<int>(t.condition)) + "%)";
        std::printf("  %s:%s;%s\n", o.name.c_str(), res.c_str(), tools.c_str());
    }
}

int cmd_run(const std::vector<std::string>& args) {
    Options o;
    if (!parse_options(args, o) || o.positional.size() != 1) {
        usage();
        return 2;
    }
    std::string source;
    if (!proto::read_file(o.positional[0], source)) {
        std::fprintf(stderr, "Не вдалося прочитати %s\n", o.positional[0].c_str());
        return 1;
    }
    Simulation sim(proto::load_balance(o.balance, !o.quiet), o.seed);
    sim.world().set_random_rain(o.rain);
    build_start_scene(sim.world(), o.scene);
    if (!o.quiet) sim.world().on_log = [&](const LogEntry& e) { std::printf("%s\n", proto::format_entry(sim.world(), e).c_str()); };
    for (Character& c : sim.world().characters()) sim.runtime().set_code(c.obj, source);

    auto t0 = std::chrono::steady_clock::now();
    sim.run_for(o.days * kDay);
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    print_summary(sim);
    std::printf("Змодельовано %g дн. за %.2f с реального часу\n", o.days, wall);
    return 0;
}

int cmd_bench(const std::vector<std::string>& args) {
    Options o;
    o.scene.characters = 50;
    o.days = 3;
    if (!parse_options(args, o) || !o.positional.empty()) {
        usage();
        return 2;
    }
    std::string script_path = o.script.empty() ? proto::source_path("scripts/woodcutter.py") : o.script;
    std::string source;
    if (!proto::read_file(script_path, source)) {
        std::fprintf(stderr, "Не вдалося прочитати %s\n", script_path.c_str());
        return 1;
    }
    const int n = o.scene.characters;
    o.scene.axes = n;
    o.scene.bread = n * 4 * static_cast<int>(std::ceil(o.days));
    o.scene.trees = n * 10 * static_cast<int>(std::ceil(o.days));

    Balance balance = proto::load_balance(o.balance, false);
    double mem0 = proto::memory_mb();
    Simulation sim(balance, o.seed);
    sim.world().set_random_rain(o.rain);
    build_start_scene(sim.world(), o.scene);
    auto ts = std::chrono::steady_clock::now();
    for (Character& c : sim.world().characters()) sim.runtime().set_code(c.obj, source);
    double setup = std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
    double mem1 = proto::memory_mb();

    std::printf("Навантажувальний тест: %d персонажів, %g дн., скрипт %s\n", n, o.days, script_path.c_str());
    std::printf("  запуск коду: %.1f мс; пам'ять: +%.1f МБ (%.2f МБ на персонажа)\n", setup * 1000, mem1 - mem0,
                (mem1 - mem0) / n);
    double total = 0;
    for (int day = 0; day < static_cast<int>(std::ceil(o.days)); ++day) {
        double span = std::min(kDay, o.days * kDay - day * kDay);
        auto t0 = std::chrono::steady_clock::now();
        sim.run_for(span);
        double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        total += wall;
        std::printf("  доба %d: %.3f с реального часу\n", day + 1, wall);
    }
    double per_day = total / o.days;
    double x10_budget = balance.day_real_minutes * 60.0 / 10.0;  // real seconds per game day at x10
    std::printf("  у середньому %.3f с на ігрову добу; на швидкості x10 доба триває %.0f с — запас x%.0f\n", per_day,
                x10_budget, per_day > 0 ? x10_budget / per_day : 0.0);

    int alive = 0, errors = 0;
    for (Character& c : sim.world().characters()) alive += c.alive ? 1 : 0;
    for (const LogEntry& e : sim.world().log_entries()) errors += e.kind == LogKind::Error ? 1 : 0;
    std::printf("  живих: %d/%d, помилок у журналі: %d, пам'ять зараз: %.1f МБ\n", alive, n, errors, proto::memory_mb());
    print_summary(sim);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 2) {
        usage();
        return 2;
    }
    std::string cmd = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);
    int code = 2;
    if (cmd == "run")
        code = cmd_run(args);
    else if (cmd == "test")
        code = proto::run_tests(args);
    else if (cmd == "list") {
        proto::list_tests();
        code = 0;
    } else if (cmd == "bench")
        code = cmd_bench(args);
    else
        usage();
    std::fflush(stdout);
    return code;
}
