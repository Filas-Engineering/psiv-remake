#include "rules/battle.h"

#include <algorithm>

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

// A single foe target that is gone moves to the next living foe by formation position. A single ally
// target that is gone moves to the user. A revive whose chosen ally is already up is cancelled.
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
		if (validIndex && combatants_[requested].side != userSide) {
			const auto next = std::lower_bound(foes.begin(), foes.end(), requested);
			primary = next == foes.end() ? foes.front() : *next;
		}
		if (targeting == Targeting::OneEnemy) {
			result.push_back(primary);
			break;
		}
		for (int foe : foes) {
			if (combatants_[foe].group == combatants_[primary].group) {
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
			if (requestedAlly) {
				if (!combatants_[requested].alive()) {
					result.push_back(requested);
				}
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
