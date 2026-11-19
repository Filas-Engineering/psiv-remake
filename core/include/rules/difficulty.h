#pragma once

#include "rules/enemy.h"
#include "rules/stats.h"
#include "rules/types.h"

#include <optional>
#include <string>
#include <vector>

namespace rules {

struct Database;

struct AbilityOverlay {
	std::string ability;
	// Scales the power of damage and heal effects.
	int powerPercent = 100;
	std::optional<int> cost;
	std::optional<int> uses;
};

struct EnemyOverlay {
	std::string enemy;
	Stats statsPercent = Stats::filled(100);
	std::vector<std::string> addAbilities;
	std::optional<std::vector<AiRule>> ai;
	// Replaces the listed statuses' resistances; others keep the base value.
	StatusResists statusResist;
};

struct DifficultyDef {
	std::string id;
	std::string name;
	// Scales battle experience and currency, before the reward multiplier.
	int rewardPercent = 100;
	std::vector<AbilityOverlay> abilities;
	std::vector<EnemyOverlay> enemies;
};

// The rules read only the overlaid data, so no rule branches on which difficulty is active.
Database applyDifficulty(const Database& db, const DifficultyDef& difficulty);

}
