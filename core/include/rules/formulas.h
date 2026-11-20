#pragma once

#include "rules/battle_rules.h"

// Turn order, chance rolls, damage and healing follow the original's rules. The rest are placeholders to be tuned.
// They are pure so tests can pin the math.
namespace rules::formulas {

constexpr int kMaxLevel = 50;
// A chance roll is 0 to 63.
constexpr int kChanceRollMax = 63;
// A basic attack misses at or below the floor and is critical above the ceiling.
constexpr int kHitFloor = 8;
constexpr int kCriticalAbove = 116;
constexpr int kEscapeFloor = 40;
// A spread is 8 plus sixteen rolls of 0 to 7. It scales the attacking stat by spread / 64.
constexpr int kSpreadRolls = 16;
constexpr int kSpreadRollMax = 7;
constexpr int kSpreadBase = 8;
constexpr int kSpreadMin = kSpreadBase;
constexpr int kSpreadMean = 64;
constexpr int kSpreadMax = kSpreadBase + kSpreadRolls * kSpreadRollMax;
constexpr int kMaxDamage = 999;

// The largest roll in a round: just under half the highest agility among those acting.
int initiativeSpread(int highestAgility);
int initiative(int agility, int roll);
// Twice the roll plus the actor's stat less the target's.
int chanceScore(int roll, int actorStat, int targetStat);
int criticalBonus(int attack);
// The attack scaled by the spread, plus the attack and twice the bonus, scaled by affinity, less defense. 1 to 999; 0 when immune.
int damage(int attack, int defense, int bonus, int affinity, int spread);
int defended(int damage);
int healAmount(int power, int mental, int spread);
int reviveHp(int maxHp, int percent);
int statusTick(int maxHp, int percent);
int statusChance(int chance, int resistance);
int modifiedStat(int base, int percentTotal);
int experienceToNextLevel(int level);

}
