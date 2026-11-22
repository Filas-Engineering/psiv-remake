#pragma once

#include "rules/result.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace rules {

// Combination detection looks at most this many queued turns ahead, the actor's own included.
inline constexpr int kMaxComboParticipants = 4;

// Fires when party members use these abilities back to back in one round.
struct ComboRecipe {
	std::string id;
	std::vector<std::string> abilities;
	std::string result;
	// Parallel to `abilities`: the character who must use that ability, or empty for anyone.
	std::vector<std::string> characters = {};
	// When set, the abilities must run in the listed order.
	bool ordered = false;
	// `orders`: further orders that fire it besides the listed one, each the component indexes in turn order; with any, the
	// recipe fires in those orders only (as if ordered).
	std::vector<std::vector<int>> orders = {};
};

// One part of an enemy combination: an enemy of this id using this ability.
struct EnemyComboPart {
	std::string enemy;
	std::string ability;
};

// Fires when enemies of the parts' ids take their turns back to back, the first drawing its part's ability; the
// others' turns are spent on it (B4).
struct EnemyComboRecipe {
	std::string id;
	std::vector<EnemyComboPart> parts;
	std::string result;
	// When set, the enemies must act in the listed order; otherwise in any order.
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
