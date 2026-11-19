#pragma once

#include "rules/stats.h"
#include "rules/types.h"

#include <string>
#include <vector>

namespace rules {

enum class AiConditionKind { Always, SelfHpBelow, AllyHpBelow, AllyDefeated, SelfHasStatus, FoeHasStatus };

struct AiCondition {
	AiConditionKind kind = AiConditionKind::Always;
	// Percent of max hit points for the HpBelow kinds.
	int value = 0;
	std::string status;
};

struct AiRule {
	std::string ability;
	int weight = 1;
	AiCondition condition;
};

struct EnemyDef {
	std::string id;
	std::string name;
	Stats stats;
	std::string basicAttack;
	std::vector<std::string> abilities;
	Affinities affinities;
	StatusResists statusResist;
	std::vector<AiRule> ai;
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
};

}
