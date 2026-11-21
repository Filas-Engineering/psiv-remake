#pragma once

#include "rules/types.h"

#include <vector>

namespace rules {

// Global rules that wait on measurements of the original, kept in data (rules.json).
struct BattleRules {
	// Rounds the surprised side loses after a Party first or Enemy first opening.
	int surpriseRounds = 1;
	// Whether rest stands a fallen member up (FLD-035).
	bool restRevives = true;
	// Hit points a fallen machine member stands up with after a won or escaped battle; 0 leaves it down (P6).
	int machineRecoverHp = 1;
	// What an affinity is divided by when it scales damage: 100 makes affinities percents. A physical hit without a weapon
	// element uses affinity 100 and a defended one kDefendedAffinity scaled to this divisor.
	int affinityDivisor = 100;
	// Elemental damage subtracts the target's magic defense in place of its mental.
	bool magicDefense = false;
	// Party members of these kinds can be felled by instant-defeat effects, besides characters marked `vulnerable`.
	std::vector<CreatureKind> instantDefeatKinds = {};
};

}
