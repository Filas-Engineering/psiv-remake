#pragma once

#include "rules/stats.h"
#include "rules/types.h"

#include <string>
#include <vector>

namespace rules {

// An enemy draws one of its eight action slots each turn, then the first override whose condition holds replaces it.
inline constexpr int kAiSlots = 8;
inline constexpr int kMaxAiOverrides = 4;

enum class AiConditionKind {
	SelfHpHalf,
	// Fires once per battle, whichever enemy it fires for.
	SelfHpQuarterOnce,
	AnyAllyHpHalf,
	LastStanding,
	HitByAttack,
	HitByAbility,
	HitByTechnique,
	TechniquesBlocked,
};

// How an action picks its single target. Default: a foe drawn by party order when the round's order is resolved,
// the ally with the lowest hit point share for heals, an ally with the status for cures, the first fallen ally for
// revives.
enum class AiTargetRule { Default, Random, LowestHp, HighestHp, HasStatus, LacksStatus, LastAttacker };

struct AiTarget {
	AiTargetRule rule = AiTargetRule::Default;
	// For HasStatus and LacksStatus.
	std::string status;
};

struct AiOverride {
	AiConditionKind when = AiConditionKind::SelfHpHalf;
	std::string ability;
	AiTarget target = {};
};

// An encounter's item drop: one roll of 0 to 127 per won battle must come out below the rate.
struct ItemDrop {
	std::string item;
	int rate = 0;
};

struct EnemyDef {
	std::string id;
	std::string name;
	Stats stats;
	std::string basicAttack;
	std::vector<std::string> abilities;
	Affinities affinities;
	StatusResists statusResist;
	// The basic attack or one of the abilities, kAiSlots of them.
	std::vector<std::string> slots;
	std::vector<AiOverride> overrides;
	int experience = 0;
	int currency = 0;
};

// One position in an enemy group's formation. Group targeting hits every living enemy with the same group.
struct FormationSlot {
	std::string enemy;
	std::string group;
};

// An enemy group: the enemy side of one battle, in formation order.
struct Encounter {
	std::string id;
	std::vector<FormationSlot> enemies;
	bool escapeAllowed = true;
	// The party's agility rolls against these for a random-mode opening and for escape. Data may leave them
	// out; the loader then uses the highest agility among the enemies.
	int awareness = 0;
	int escapeRating = 0;
	// No item when empty.
	ItemDrop drop;
};

}
