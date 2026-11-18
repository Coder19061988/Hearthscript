#pragma once

#include <string>
#include <vector>

#include "simulation.h"

// Helpers shared by the console commands (main.cpp) and the scenario tests (tests.cpp).
namespace proto {

std::string source_path(const std::string& relative);  // path inside the repository
kindle::Balance load_balance(const std::string& path, bool verbose);
bool read_file(const std::string& path, std::string& out);
std::string format_entry(kindle::World& world, const kindle::LogEntry& e);
double memory_mb();  // private memory of the process, MB (0 if unknown)

int run_tests(const std::vector<std::string>& names);
void list_tests();

}  // namespace proto
