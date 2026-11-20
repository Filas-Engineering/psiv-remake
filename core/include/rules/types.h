#pragma once

#include <map>
#include <string>

namespace rules {

enum class Element { None, Heat, Chill, Shock, Gale };

enum class Stat { Strength, Mental, Agility, Dexterity, Defense };

enum class Side { Party, Enemy };

enum class Targeting { OneEnemy, EnemyGroup, AllEnemies, OneAlly, AllAllies, Self, OneFallenAlly, RandomEnemy };

enum class AbilityKind { Attack, Technique, Skill, Combo };

enum class EffectKind { PhysicalDamage, ElementalDamage, Heal, ApplyStatus, CureStatus, ModifyStat, Revive };

enum class EquipSlot { Weapon, Head, Body, Arms, Accessory };

enum class Opening { Normal, PartyFirst, EnemyFirst };

enum class RewardMultiplier { X1, X1_5, X2 };

struct BattleFlags {
	bool escapeAllowed = true;
	// Without a defeat branch, Defeat is a game over (FLOW-040).
	bool defeatHasBranch = false;
	bool vehicleBattle = false;
};

using Inventory = std::map<std::string, int>;

// Equipment id to the number of pieces nobody wears.
using EquipmentBag = std::map<std::string, int>;

// Percent of incoming elemental damage taken. Missing elements count as 100.
using Affinities = std::map<Element, int>;

// Status id to the percent taken off its success chance. 100 is immunity.
using StatusResists = std::map<std::string, int>;

}
