#pragma once

#include "rules/types.h"

namespace rules {

// Global rules that wait on measurements of the original, kept in data (rules.json).
struct BattleRules {
	// Rounds the surprised side loses after a Party first or Enemy first opening.
	int surpriseRounds = 1;
	ExperienceSplit experienceSplit = ExperienceSplit::EachLiving;
	// Whether rest stands a fallen member up (FLD-035).
	bool restRevives = true;
};

}
