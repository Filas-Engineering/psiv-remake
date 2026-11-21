#pragma once

#include "rules/types.h"

#include <string>
#include <vector>

namespace rules {

// The loader requires a status with this id. The battle adds it when hit points reach zero.
inline constexpr const char* kDefeatedStatus = "defeated";

enum class StatusAction { Normal, CannotAct, Forced };

// Who a forced command (a basic attack) picks from, at random.
enum class ForcedTarget { RandomAny, RandomAlly, RandomFoe };

// What applying a status the target already has does.
enum class StatusStacking { Refresh, Extend, Keep };

struct StatusDef {
	std::string id;
	std::string name;
	StatusAction action = StatusAction::Normal;
	ForcedTarget forcedTarget = ForcedTarget::RandomAny;
	bool blocksTechniques = false;
	bool wakesOnDamage = false;
	// Held at 1 while the status lasts.
	std::vector<Stat> statsToOne;
	// Statuses that receiving this one removes.
	std::vector<std::string> removes;
	// Rounds before it wears off; 0 lasts until cured or, unless it persists, until the battle ends.
	int duration = 0;
	// Replaces duration on an enemy when nonzero.
	int enemyDuration = 0;
	// Rolled at the end of each round.
	int recoverChance = 0;
	StatusStacking stacking = StatusStacking::Refresh;
	bool persistsOutsideBattle = false;
	// A party member with such a status counts as down when the battle checks for Defeat.
	bool countsTowardDefeat = false;
	// Kept when its holder is defeated, and so after a revive.
	bool survivesDefeat = false;
	bool restCures = true;
	// When nonzero, no hit takes more than this from the holder (B11).
	int damageCap = 0;
};

struct ActiveStatus {
	std::string id;
	// Rounds left; 0 has no round limit.
	int remaining = 0;
};

}
