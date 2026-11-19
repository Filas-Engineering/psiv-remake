#include "rules/battle.h"

namespace rules {

std::vector<int> Battle::livingFoes(int actor) const {
	std::vector<int> result;
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (combatants_[i].side != combatants_[actor].side && combatants_[i].alive()) {
			result.push_back(i);
		}
	}
	return result;
}

std::vector<int> Battle::allies(int actor) const {
	std::vector<int> result;
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (combatants_[i].side == combatants_[actor].side) {
			result.push_back(i);
		}
	}
	return result;
}

// Single-target commands whose target is gone move to the first valid one, so a queued action is
// never wasted. Heals and cures fall back to the user; revives look for any defeated ally.
std::vector<int> Battle::resolveTargets(int user, Targeting targeting, int requested, bool revives) const {
	const bool validIndex = requested >= 0 && requested < static_cast<int>(combatants_.size());
	const Side userSide = combatants_[user].side;
	std::vector<int> result;
	switch (targeting) {
	case Targeting::Self:
		result.push_back(user);
		break;
	case Targeting::OneEnemy:
	case Targeting::EnemyGroup: {
		const std::vector<int> foes = livingFoes(user);
		if (foes.empty()) {
			break;
		}
		int primary = foes.front();
		if (validIndex && combatants_[requested].side != userSide && combatants_[requested].alive()) {
			primary = requested;
		}
		if (targeting == Targeting::OneEnemy) {
			result.push_back(primary);
			break;
		}
		for (int foe : foes) {
			if (combatants_[foe].defId == combatants_[primary].defId) {
				result.push_back(foe);
			}
		}
		break;
	}
	case Targeting::AllEnemies:
		result = livingFoes(user);
		break;
	case Targeting::OneAlly: {
		const bool requestedAlly = validIndex && combatants_[requested].side == userSide;
		if (revives) {
			if (requestedAlly && !combatants_[requested].alive()) {
				result.push_back(requested);
				break;
			}
			for (int ally : allies(user)) {
				if (!combatants_[ally].alive()) {
					result.push_back(ally);
					break;
				}
			}
			break;
		}
		result.push_back(requestedAlly && combatants_[requested].alive() ? requested : user);
		break;
	}
	case Targeting::AllAllies:
		for (int ally : allies(user)) {
			if (revives || combatants_[ally].alive()) {
				result.push_back(ally);
			}
		}
		break;
	}
	return result;
}

}
