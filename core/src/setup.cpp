#include "rules/setup.h"

#include "rules/database.h"
#include "rules/formulas.h"

namespace rules {

std::string useEncounter(BattleSetup& setup, const Database& db, const std::string& encounterId) {
	const Encounter* encounter = db.encounter(encounterId);
	if (!encounter) {
		return "unknown encounter '" + encounterId + "'";
	}
	setup.enemyGroup = encounter->id;
	setup.enemies = encounter->enemies;
	setup.flags.escapeAllowed = encounter->escapeAllowed;
	setup.escapeRating = encounter->escapeRating;
	return "";
}

Opening openingFromContact(bool leaderInEnemyRearArc, bool enemyChasing, bool enemyInLeaderRearArc) {
	if (!enemyChasing && leaderInEnemyRearArc) {
		return Opening::PartyFirst;
	}
	if (enemyChasing && enemyInLeaderRearArc) {
		return Opening::EnemyFirst;
	}
	return Opening::Normal;
}

Opening openingFromRoll(int roll, int partyAgility, int awareness) {
	const int score = formulas::chanceScore(roll, partyAgility, awareness);
	if (score <= formulas::kEnemyFirstFloor) {
		return Opening::EnemyFirst;
	}
	return score > formulas::kPartyFirstAbove ? Opening::PartyFirst : Opening::Normal;
}

int rewardMultiplierPercent(RewardMultiplier multiplier) {
	switch (multiplier) {
	case RewardMultiplier::X1: return 100;
	case RewardMultiplier::X1_5: return 150;
	case RewardMultiplier::X2: return 200;
	}
	return 100;
}

}
