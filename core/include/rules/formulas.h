#pragma once

#include "rules/battle_rules.h"

// Turn order, chance rolls, damage and healing follow the original's rules. The rest are placeholders to be tuned.
// They are pure so tests can pin the math.
namespace rules::formulas {

constexpr int kMaxLevel = 99;
// A chance roll is 0 to 63.
constexpr int kChanceRollMax = 63;
// A basic attack misses at or below the floor and is critical above the ceiling.
constexpr int kHitFloor = 8;
constexpr int kCriticalAbove = 116;
constexpr int kEscapeFloor = 40;
// An escape rating at or above this forbids escape.
constexpr int kEscapeForbiddenRating = 240;
// A random-mode opening is Enemy first at or below the floor and Party first above the ceiling.
constexpr int kEnemyFirstFloor = 12;
constexpr int kPartyFirstAbove = 116;
// A spread is 8 plus sixteen rolls of 0 to 7. It scales the attacking stat by spread / 64.
constexpr int kSpreadRolls = 16;
constexpr int kSpreadRollMax = 7;
constexpr int kSpreadBase = 8;
constexpr int kSpreadMin = kSpreadBase;
constexpr int kSpreadMean = 64;
constexpr int kSpreadMax = kSpreadBase + kSpreadRolls * kSpreadRollMax;
constexpr int kMaxDamage = 999;
// A defending target takes a physical hit without a weapon element as if its affinity were this.
constexpr int kDefendedAffinity = 25;

// The largest roll in a round: just under half the highest agility among those acting.
int initiativeSpread(int highestAgility);
int initiative(int agility, int roll);
// Twice the roll plus the actor's stat less the target's.
int chanceScore(int roll, int actorStat, int targetStat);
int criticalBonus(int attack);
// The attack scaled by the spread, plus the attack and twice the bonus, scaled by affinity, less defense. 1 to 999; 0 when immune.
int damage(int attack, int defense, int bonus, int affinity, int spread, int divisor = 100);
// The damage at the spread's mean, times scaleNumerator / scaleDenominator, then as damage(): data's own variance.
int variedDamage(int attack, int defense, int bonus, int affinity, int scaleNumerator, int scaleDenominator, int divisor);
int healAmount(int power, int mental, int spread);
int reviveHp(int maxHp, int percent);
// A status is resisted when the score is at or below the effect's threshold.
int statusScore(int roll, int actorMental, int targetMental, int resistance);
// A roll whose factor is the target's affinity for an element on the original's scale of 4 for normal
// (original-battle-rules.md, section 6): affinity * 4 / divisor.
int elementScore(int roll, int actorMental, int targetMental, int affinity, int divisor);
int experienceToNextLevel(int level);

}
