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

// The spread step rounds down as the original's does; the affinity step multiplies before it divides, so it rounds down
// once, before the defense is subtracted.
int damage(int attack, int defense, int bonus, int affinity, int spread, int divisor) {
	if (affinity <= 0) {
		return 0;
	}
	const int raw = attack * spread / 64 + attack + bonus * 2;
	return std::clamp(raw * affinity / divisor - defense, 1, kMaxDamage);
}

int variedDamage(int attack, int defense, int bonus, int affinity, int scaleNumerator, int scaleDenominator, int divisor) {
	if (affinity <= 0) {
		return 0;
	}
	const long long raw = static_cast<long long>(attack * 2 + bonus * 2) * scaleNumerator / scaleDenominator;
	return static_cast<int>(std::clamp<long long>(raw * affinity / divisor - defense, 1, kMaxDamage));
}

int healAmount(int power, int mental, int spread) {
	return (mental * spread / 64 + mental + power * 2) / 2;
}

int reviveHp(int maxHp, int percent) {
	return std::max(1, maxHp * percent / 100);
}

int statusScore(int roll, int actorMental, int targetMental, int resistance) {
	return (roll + actorMental - targetMental) * ((100 - resistance) * 4 / 100);
}

int elementScore(int roll, int actorMental, int targetMental, int affinity, int divisor) {
	return (roll + actorMental - targetMental) * (affinity * 4 / divisor);
}

int experienceToNextLevel(int level) {
	return 15 * level * level + 10;
}

}
