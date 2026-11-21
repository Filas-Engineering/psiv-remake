#pragma once

#include <map>
#include <string>

namespace rules {

enum class Element { None, Heat, Chill, Shock, Gale, Holy };

enum class Stat { Strength, Mental, Agility, Dexterity, Defense };

enum class Side { Party, Enemy };

enum class Targeting { OneEnemy, EnemyGroup, AllEnemies, OneAlly, AllAllies, Self, OneFallenAlly, RandomEnemy };

enum class AbilityKind { Attack, Technique, Skill, Combo };

enum class EffectKind { PhysicalDamage, ElementalDamage, Heal, ApplyStatus, CureStatus, ModifyStat, Revive, Travel, InstantDefeat, RestoreTp, StripModifiers, Summon, Merge, Teach };
// What a combatant is (B6, P6). Spirit is a kind of living being: an effect aimed at the living reaches it too.
enum class CreatureKind { Living, Spirit, Machine };

// Where a Travel effect takes the party (FLD-051): a town visited before, or out of the dungeon it is in.
enum class TravelTarget { Town, DungeonExit };

// A piece's slot, and where a member wears it. Weapons and shields are hand pieces: Weapon is also the right hand,
// where a hand piece goes first, and LeftHand is only a place to wear one (P10).
enum class EquipSlot { Weapon, Head, Body, Arms, Accessory, Shield, LeftHand };

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
