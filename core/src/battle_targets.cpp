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

// The requested foe, or the next living foe by formation position after it (wrapping to the first).
int Battle::primaryFoe(int user, int requested) const {
	const std::vector<int> foes = livingFoes(user);
	if (foes.empty()) {
		return -1;
	}
	if (requested >= 0 && requested < static_cast<int>(combatants_.size()) && combatants_[requested].side != combatants_[user].side) {
		const auto next = std::lower_bound(foes.begin(), foes.end(), requested);
		return next == foes.end() ? foes.front() : *next;
	}
	return foes.front();
}

// A single ally target that is gone moves to the user. A fallen-ally target that is back up cancels.
std::vector<int> Battle::resolveTargets(int user, Targeting targeting, int requested, bool revives) const {
	const bool validIndex = requested >= 0 && requested < static_cast<int>(combatants_.size());
	const bool requestedAlly = validIndex && combatants_[requested].side == combatants_[user].side;
	std::vector<int> result;
	switch (targeting) {
	case Targeting::Self:
		result.push_back(user);
		break;
	case Targeting::OneEnemy:
	case Targeting::EnemyGroup: {
		const int primary = primaryFoe(user, requested);
		if (primary < 0) {
			break;
		}
		if (targeting == Targeting::OneEnemy) {
			result.push_back(primary);
			break;
		}
		for (int foe : livingFoes(user)) {
			if (combatants_[foe].group == combatants_[primary].group) {
				result.push_back(foe);
			}
		}
		break;
	}
	case Targeting::AllEnemies:
	case Targeting::RandomEnemy:
		result = livingFoes(user);
		break;
	case Targeting::OneAlly:
		result.push_back(requestedAlly && combatants_[requested].alive() ? requested : user);
		break;
	case Targeting::OneFallenAlly:
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
