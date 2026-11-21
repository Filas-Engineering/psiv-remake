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
	// A party ability aimed at a whole group or every enemy landed damage on it since an override last read the hits.
	HitByGroup,
	// Its first turn after a Party first opening; fires once per enemy.
	Ambushed,
	// Another living enemy of the override's `enemy` kind is (or is not) in the battle.
	AllyPresent,
	AllyAbsent,
	// A formation position next to it is free, for a summon.
	FreeSlotBeside,
	// Every enemy that started the battle in its group is still standing.
	GroupIntact,
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
	// AllyPresent and AllyAbsent: the enemy id looked for.
	std::string enemy = {};
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
	CreatureKind kind = CreatureKind::Living;
	// Instant-defeat effects work only on a vulnerable target (B7).
	bool vulnerable = false;
	// Statuses it starts every battle with, such as a shield (B11).
	std::vector<std::string> statuses = {};
	// The enemy its Summon effect calls up beside it (B13).
	std::string summons = {};
	// What its Merge effect makes of it and another living enemy of its own id (B14).
	std::string mergesInto = {};
	// The form it takes when it falls, at full hit points, in place of being defeated (B16).
	std::string nextForm = {};
	// Presentation only: a renderer shows it off the ground.
	bool flying = false;
	// The percent of its maximum hit points at or below which it takes its next form; 0 takes it on defeat.
	int nextFormAt = 0;
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
	// A fallen enemy is replaced at once by a fresh one of its kind, so only escape or defeat ends the battle (B12).
	bool replaceFallen = false;
};

}
