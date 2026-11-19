#pragma once

#include "rules/character.h"
#include "rules/enemy.h"
#include "rules/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rules {

struct Database;

struct BattleFlags {
	bool escapeAllowed = true;
	bool defeatHasBranch = false;
	bool vehicleBattle = false;
};

// Everything a battle starts from. The same setup and commands always give the same event stream.
struct BattleSetup {
	std::vector<PartyMember> party;
	Inventory inventory;
	std::string enemyGroup;
	std::vector<FormationSlot> enemies;
	Opening opening = Opening::Normal;
	BattleFlags flags;
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
