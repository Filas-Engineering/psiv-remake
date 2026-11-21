#pragma once

#include "rules/types.h"

#include <optional>
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
	// What an affinity is divided by when it scales damage: 100 makes affinities percents.
	int affinityDivisor = 100;
	// The affinity a hit without an element takes, and a defending target's for it; unset, the divisor and
	// kDefendedAffinity scaled to it.
	std::optional<int> elementlessAffinity;
	std::optional<int> defendAffinity;
	// Defend also holds an elemental hit's affinity at most at the defended one.
	bool defendElemental = false;
	// Elemental damage subtracts the target's magic defense in place of its mental.
	bool magicDefense = false;
	// Party members of these kinds can be felled by instant-defeat effects, besides characters marked `vulnerable`.
	std::vector<CreatureKind> instantDefeatKinds = {};
	// The opening that marks enemies ambushed. Party first: spent at each enemy's first turn. Enemy first: kept until an
	// ambushed override fires.
	Opening ambushOpening = Opening::PartyFirst;
	// Elements beyond the built-in ones, usable wherever an element is.
	std::vector<std::string> elements = {};
	// Combatants take this affinity for an element their data leaves out; set from affinityDivisor, so normal damage.
	int neutralAffinity() const { return affinityDivisor; }
	int elementlessHitAffinity() const { return elementlessAffinity.value_or(affinityDivisor); }
	int defendedAffinity() const;
};

}
