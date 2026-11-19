#pragma once

#include "rules/types.h"

namespace rules {

struct EscapeRule {
	int baseChance = 50;
	int agilityFactor = 2;
	int minChance = 10;
	int maxChance = 90;
};

// Global rules that wait on measurements of the original, kept in data (rules.json).
struct BattleRules {
	// Rounds the surprised side loses after a Party first or Enemy first opening.
	int surpriseRounds = 1;
	int criticalDamagePercent = 150;
	ExperienceSplit experienceSplit = ExperienceSplit::EachLiving;
	EscapeRule escape;
};

}
