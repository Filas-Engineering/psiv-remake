#pragma once

#include "rules/result.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace rules {

// Fires when party members use these abilities back to back in one round.
struct ComboRecipe {
	std::string id;
	std::vector<std::string> abilities;
	std::string result;
	// Parallel to `abilities`: the character who must use that ability, or empty for anyone.
	std::vector<std::string> characters = {};
	// When set, the abilities must run in the listed order.
	bool ordered = false;
};

struct ComboLogEntry {
	std::string recipe;
	// The host's play time when the combination first fired, in milliseconds.
	std::int64_t playTimeMs = 0;
	// Character ids of the participants, in the order they acted.
	std::vector<std::string> characters;
};

class ComboLog {
public:
	// Returns true the first time a recipe is discovered.
	bool discover(const std::string& recipeId, std::int64_t playTimeMs, const std::vector<std::string>& characters);
	bool isDiscovered(const std::string& recipeId) const;
	const ComboLogEntry* entry(const std::string& recipeId) const;
	// In discovery order.
	const std::vector<ComboLogEntry>& entries() const { return entries_; }
	std::set<std::string> discovered() const;

private:
	std::vector<ComboLogEntry> entries_;
};

// A versioned line-based text form for saves.
std::string serializeComboLog(const ComboLog& log);
Result<ComboLog> parseComboLog(const std::string& text);

}
