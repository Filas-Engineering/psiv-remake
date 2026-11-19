#pragma once

// Every formula here is a placeholder to be tuned. They are pure so tests can pin the math.
namespace rules::formulas {

constexpr int kVarianceMin = 90;
constexpr int kVarianceMax = 110;
constexpr int kMaxLevel = 50;

int initiativeSpread(int agility);
int initiative(int agility, int roll);
int hitChance(int attackerDexterity, int defenderAgility);
int physicalDamage(int strength, int defense, int power, int variance);
int elementalDamage(int power, int mental, int targetMental, int affinity, int variance);
int defended(int damage);
int healAmount(int power, int mental);
int reviveHp(int maxHp, int percent);
int statusTick(int maxHp, int percent);
int fleeChance(int partyAverageAgility, int enemyAverageAgility);
int modifiedStat(int base, int percentTotal);
int experienceToNextLevel(int level);

}
