#pragma once

#include <map>
#include <string>
#include <utility>

namespace rules {

// An element by id: one of the built-in ones below or one rules.json `elements` declares. An empty id is none.
struct Element {
	std::string id;

	Element() = default;
	Element(std::string elementId) : id(std::move(elementId)) {}
	Element(const char* elementId) : id(elementId) {}
	bool operator==(const Element& other) const { return id == other.id; }
	bool operator!=(const Element& other) const { return id != other.id; }
	bool operator<(const Element& other) const { return id < other.id; }

	static const Element None;
	static const Element Heat;
	static const Element Chill;
	static const Element Shock;
	static const Element Gale;
	static const Element Holy;
};

inline const Element Element::None{};
inline const Element Element::Heat{"heat"};
inline const Element Element::Chill{"chill"};
inline const Element Element::Shock{"shock"};
inline const Element Element::Gale{"gale"};
inline const Element Element::Holy{"holy"};

// The elements every data set has, in the order the C API numbers them from 1.
inline constexpr const char* kBuiltInElements[] = {"heat", "chill", "shock", "gale", "holy"};

// Attack is derived: strength plus the weapons' attack bonuses. Rolls and attacking stats can name it; nothing changes it.
enum class Stat { Strength, Mental, Agility, Dexterity, Defense, MagicDefense, Attack };

enum class Side { Party, Enemy };

enum class Targeting { OneEnemy, EnemyGroup, AllEnemies, OneAlly, AllAllies, Self, OneFallenAlly, RandomEnemy };

enum class AbilityKind { Attack, Technique, Skill, Combo };

enum class EffectKind { PhysicalDamage, ElementalDamage, Heal, ApplyStatus, CureStatus, ModifyStat, Revive, Travel, InstantDefeat, RestoreTp, StripModifiers, Summon, Merge, Teach, Transform, Split, SelfDestruct, RemoveBeside, SetHpPercent, GrantAffinity };
// What a combatant is (B6, P6). Spirit is a kind of living being: an effect aimed at the living reaches it too.
enum class CreatureKind { Living, Spirit, Machine };

// Where a Travel effect takes the party (FLD-051): a town visited before, or out of the dungeon it is in.
enum class TravelTarget { Town, DungeonExit };

// A piece's slot, and where a member wears it. Weapons and shields are hand pieces: Weapon is also the right hand,
// where a hand piece goes first, and LeftHand is only a place to wear one (P10).
enum class EquipSlot { Weapon, Head, Body, Arms, Accessory, Shield, LeftHand };

enum class Opening { Normal, PartyFirst, EnemyFirst };

// How a damage roll varies when data sets a variance in place of the original's spread: one even roll over the range,
// or the original's sixteen-roll shape scaled to it.
enum class VarianceShape { Uniform, Bell };

// The pre-defense damage, worked out at the spread's mean, moves up to `percent` either way.
struct DamageVariance {
	int percent = 0;
	VarianceShape shape = VarianceShape::Uniform;
};

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
