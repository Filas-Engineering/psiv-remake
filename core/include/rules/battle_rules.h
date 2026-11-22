#pragma once

#include "rules/types.h"

#include <map>
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
	// Field statuses (areas with `field_status`): every `fieldPoisonEvery` steps since the area was entered a poisoned
	// member loses 1 hit point (0: never); a paralyzed member shakes it off on a 1 in `fieldParalysisChance` roll each step
	// from step `fieldParalysisFrom`, and surely at step `fieldParalysisBy` (chance 0: never). The status ids are
	// `fieldPoisonStatus` and `fieldParalysisStatus`.
	int fieldPoisonEvery = 0;
	std::string fieldPoisonStatus = "poison";
	int fieldParalysisFrom = 0;
	int fieldParalysisChance = 0;
	int fieldParalysisBy = 0;
	std::string fieldParalysisStatus = "paralysis";
	// What an affinity is divided by when it scales damage: 100 makes affinities percents.
	int affinityDivisor = 100;
	// The affinity a hit without an element takes, and a defending target's for it; unset, the divisor and
	// kDefendedAffinity scaled to it.
	std::optional<int> elementlessAffinity;
	std::optional<int> defendAffinity;
	// Defend also holds an elemental hit's affinity at most at the defended one.
	bool defendElemental = false;
	// The elements Defend reaches, None for a hit without one; unset, as defendElemental says.
	std::optional<std::vector<Element>> defendElements;
	// Every damage roll's variance in place of the original's spread, unless the ability has its own.
	std::optional<DamageVariance> damageVariance;
	// Elemental damage subtracts the target's magic defense in place of its mental.
	bool magicDefense = false;
	// Party members of these kinds can be felled by instant-defeat effects, besides characters marked `vulnerable`.
	std::vector<CreatureKind> instantDefeatKinds = {};
	// The opening that marks enemies ambushed. Party first: spent at each enemy's first turn. Enemy first: kept until an
	// ambushed override fires.
	Opening ambushOpening = Opening::PartyFirst;
	// Elements beyond the built-in ones, usable wherever an element is.
	std::vector<std::string> elements = {};
	// Defend holds its affinity from the defender's own turn to the end of the round, not from the round's start.
	bool defendFromTurn = false;
	// The floor of 1 damage holds at an affinity of 0 too, so an immune target still takes 1.
	bool immuneMinDamage = false;
	// A critical hit's bonus is the attack's low byte divided by 4, not the attack divided by 4.
	bool criticalLowByte = false;
	// The attack times the spread wraps at 65536 before it is divided by 64, as 16-bit arithmetic would.
	bool damageWraparound = false;
	// Escape always works in the first round of a Party first opening.
	bool escapePartyFirstRound = false;
	// By the number of party members in an enemy's foe draw: each one's weight in 256ths, in party order; a size left
	// out draws count, count - 1, ... 1.
	std::map<int, std::vector<int>> targetWeights = {};
	// Stats whose raised value wraps past 255 to 0 and up, as a byte would.
	std::vector<Stat> statWraparound = {};
	// A macro's one-ally picks pass by every member the effect would not change (Battle::wouldChange), as automatic
	// choices do, and a step with the default target falls back when nobody qualifies.
	bool macroAllyWouldChange = false;
	// Automatic choices weigh each attack by its expected effect on the foes (rules.json auto_policy expected) in place of
	// taking the largest damage power.
	bool autoExpected = false;
	// What weak, resisted and immune labels compare an elemental hit's affinity with, in place of the divisor; damage
	// still divides by the divisor. elementBaselines wins for its elements.
	std::optional<int> effectivenessBaseline;
	std::map<Element, int> elementBaselines = {};
	// Combatants take this affinity for an element their data leaves out; set from affinityDivisor, so normal damage.
	int neutralAffinity() const { return affinityDivisor; }
	int elementlessHitAffinity() const { return elementlessAffinity.value_or(affinityDivisor); }
	int defendedAffinity() const;
	bool defends(const Element& element) const;
	int normalAffinity(const Element& element) const;
};

}
