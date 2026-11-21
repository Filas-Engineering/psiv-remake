#pragma once

#include "game/data.h"

#include <string>
#include <utility>
#include <vector>

namespace game {

struct CheckCount {
	std::string what;
	std::size_t count = 0;
};

// What `game_cli --check` reports on a data directory.
struct CheckReport {
	// Every problem the loader and crossCheck found. `file` is a path a person can open whenever the problem is in
	// a file; otherwise it names the part of the data.
	std::vector<rules::Problem> problems;
	// What loaded, in loading order: a heading ("core data ../core/data", "game data data", "text en") and its counts.
	std::vector<std::pair<std::string, std::vector<CheckCount>>> loaded;
	// False when problems stopped the load before its last step, so some script checks or the loader's
	// cross-references did not run.
	bool complete = false;

	bool ok() const { return problems.empty(); }
};

// Loads the game data and its battle data as far as they go (loadGameDataPartly), adds crossCheck's problems
// and counts what loaded.
CheckReport checkData(const DataDirectories& directories);

// Problems loadGameData does not look for, found on whatever loaded: two things on one cell; a marker, exit,
// step hook or spawn point on a solid cell (interact hooks, shops, rest points, chests and ports are solid); hook ids taken twice in an area, zone ids twice in an area, chest ids
// twice across areas; zones reversed, off the map or overlapping; shops with nothing in stock; exits, markers,
// hooks and interactables the party cannot walk to from any way into the area; areas no exit, script warp or
// start position leads into, or that cannot be reached from the start area. `loaderChecked` says the loader
// got as far as its own cross-references; when it did not, exit targets and the area display names (area.<id>,
// in every language) are checked here.
std::vector<rules::Problem> crossCheck(const GameData& data, bool loaderChecked);

// The report as English text: the problems, one per line as "file:line: text", what loaded, then the result.
std::string formatCheckReport(const CheckReport& report);

}
