#pragma once

#include <string>

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
	int tickDamagePercent = 0;
	// Rounds before it wears off; 0 lasts until cured or, unless it persists, until the battle ends.
	int duration = 0;
	// Replaces duration on an enemy when nonzero.
	int enemyDuration = 0;
	// Rolled at the end of each round.
	int recoverChance = 0;
	StatusStacking stacking = StatusStacking::Refresh;
	bool persistsOutsideBattle = false;
	// A combatant with such a status counts as down when the battle checks for Victory and Defeat.
	bool countsTowardDefeat = false;
	bool restCures = true;
};

struct ActiveStatus {
	std::string id;
	// Rounds left; 0 has no round limit.
	int remaining = 0;
};

}
