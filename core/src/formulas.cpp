#include "rules/formulas.h"

#include <algorithm>

namespace rules::formulas {

int initiativeSpread(int highestAgility) {
	return highestAgility / 2 > 0 ? highestAgility / 2 - 1 : 0;
}

int initiative(int agility, int roll) {
	return agility > 0 ? agility + roll : 0;
}

int chanceScore(int roll, int actorStat, int targetStat) {
	return (roll + actorStat - targetStat) * 2;
}

int criticalBonus(int attack) {
	return attack / 4;
}

int damage(int attack, int defense, int bonus, int affinity, int spread) {
	if (affinity <= 0) {
		return 0;
	}
	const int raw = attack * spread / 64 + attack + bonus * 2;
	return std::clamp(raw * affinity / 100 - defense, 1, kMaxDamage);
}

int healAmount(int power, int mental, int spread) {
	return (mental * spread / 64 + mental + power * 2) / 2;
}

int reviveHp(int maxHp, int percent) {
	return std::max(1, maxHp * percent / 100);
}

int statusTick(int maxHp, int percent) {
	return std::max(1, maxHp * percent / 100);
}

int statusScore(int roll, int actorMental, int targetMental, int resistance) {
	return (roll + actorMental - targetMental) * ((100 - resistance) * 4 / 100);
}

int modifiedStat(int base, int percentTotal) {
	const int percent = std::clamp(percentTotal, -50, 100);
	return std::max(1, base * (100 + percent) / 100);
}

int experienceToNextLevel(int level) {
	return 15 * level * level + 10;
}

}
