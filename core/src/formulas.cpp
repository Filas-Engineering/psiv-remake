#include "rules/formulas.h"

#include <algorithm>

namespace rules::formulas {

int initiativeSpread(int agility) {
	return 8 + agility / 4;
}

int initiative(int agility, int roll) {
	return agility + roll;
}

int hitChance(int attackerDexterity, int defenderAgility) {
	return std::clamp(85 + attackerDexterity - defenderAgility, 40, 100);
}

int physicalDamage(int strength, int defense, int power, int variance) {
	const int base = std::max(strength * 2 - defense, std::max(1, strength / 4));
	return std::max(1, base * power / 100 * variance / 100);
}

int elementalDamage(int power, int mental, int targetMental, int affinity, int variance) {
	if (affinity <= 0) {
		return 0;
	}
	const int base = std::max(power + mental - targetMental / 2, std::max(1, power / 2));
	return std::max(1, base * variance / 100 * affinity / 100);
}

int defended(int damage) {
	return std::max(1, damage / 2);
}

int healAmount(int power, int mental) {
	return power + mental / 2;
}

int reviveHp(int maxHp, int percent) {
	return std::max(1, maxHp * percent / 100);
}

int statusTick(int maxHp, int percent) {
	return std::max(1, maxHp * percent / 100);
}

int fleeChance(int partyAverageAgility, int enemyAverageAgility) {
	return std::clamp(50 + (partyAverageAgility - enemyAverageAgility) * 2, 10, 90);
}

int modifiedStat(int base, int percentTotal) {
	const int percent = std::clamp(percentTotal, -50, 100);
	return std::max(1, base * (100 + percent) / 100);
}

int experienceToNextLevel(int level) {
	return 15 * level * level + 10;
}

}
