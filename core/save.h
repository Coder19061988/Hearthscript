#pragma once

#include <memory>
#include <string>

#include "simulation.h"

namespace kindle {

// Whole game state as JSON text: the world (objects, characters, weather, RNG), every character's
// code, the player's global variables and the tail of the event log. Running actions are not saved:
// after loading, every character starts its code from the top (MVP §7), while work progress that lives
// in the world (a half-chopped unit, a ploughed field, a building site) stays.
std::string save_game(Simulation& sim);

// A new simulation from a save made by save_game(); nullptr and `error` (Ukrainian) on failure.
// `balance` comes from the current config, not from the save.
std::unique_ptr<Simulation> load_game(const Balance& balance, const std::string& text, std::string& error);

}  // namespace kindle
