#pragma once

#include "rules/character.h"
#include "rules/enemy.h"
#include "rules/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rules {

struct Database;

enum class ScriptedEndKind { None, SurviveRounds, EnemyDefeated };

// An end condition the event sets (BAT-013). Victory and Defeat take precedence when they happen together.
struct ScriptedEnd {
	ScriptedEndKind kind = ScriptedEndKind::None;
	// SurviveRounds: the battle ends after this many rounds.
	int rounds = 0;
	// EnemyDefeated: the battle ends when the enemy at this formation position falls.
	int enemySlot = -1;
};

// Everything a battle starts from. The same setup and commands always give the same event stream.
struct BattleSetup {
	std::vector<PartyMember> party;
	Inventory inventory;
	std::string enemyGroup;
	std::vector<FormationSlot> enemies;
	Opening opening = Opening::Normal;
	BattleFlags flags;
	ScriptedEnd scriptedEnd;
	// Difficulty id; empty applies no overlay.
	std::string difficulty;
	RewardMultiplier rewardMultiplier = RewardMultiplier::X1;
	std::uint64_t seed = 0;
};

// Fills the enemy group and the escape flag from an encounter. Returns an empty string, or the error.
std::string useEncounter(BattleSetup& setup, const Database& db, const std::string& encounterId);

// The visible-mode opening for a contact between the leader and a visible enemy.
Opening openingFromContact(bool leaderInEnemyRearArc, bool enemyChasing, bool enemyInLeaderRearArc);

int rewardMultiplierPercent(RewardMultiplier multiplier);

}
