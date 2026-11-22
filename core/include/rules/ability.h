#pragma once

#include "rules/types.h"

#include <optional>
#include <string>
#include <vector>

namespace rules {

struct Effect {
	EffectKind kind = EffectKind::PhysicalDamage;
	// Damage and heal strength; for Revive, the percent of max hit points restored; for an item's ModifyStat, the amount.
	int power = 0;
	Element element = Element::None;
	std::string status;
	// ApplyStatus, a lowering ModifyStat and InstantDefeat: the target resists when the roll's score is at or below this.
	int threshold = 0;
	Stat stat = Stat::Strength;
	// ModifyStat: raises the stat rather than lowering it.
	bool raises = false;
	TravelTarget travel = TravelTarget::Town;
	// The kinds of target the effect works on; empty works on every kind.
	std::vector<CreatureKind> kinds = {};
	// Teach: the ability the target learns for good.
	std::string ability = {};
	// Split: how many enemies the user becomes.
	int count = 0;
	// SelfDestruct and RemoveBeside: the enemies that leave still pay their experience and currency.
	bool rewards = false;
	// Damage: the thousandths of the user's attacking stat the formula attacks with (`stat_permille`, or `stat_percent`
	// times 10).
	int statPermille = 1000;
	// An item's heal or damage: with a value, the item rolls the spread with its power as the stat and this as the bonus.
	std::optional<int> statValue = {};
	// PhysicalDamage: the hit takes the user's attack element (a member's weapon, an enemy's attack_element).
	bool weaponElement = false;
	// The roll of ApplyStatus, a lowering ModifyStat and InstantDefeat: the user's side is userPower, else the user's
	// userStat, else the attacking stat; the target's side is its targetStat, else mental. `element` scales the factor.
	std::optional<Stat> userStat = {};
	std::optional<Stat> targetStat = {};
	std::optional<int> userPower = {};
	// No roll and no random draw; the immunities that need no roll still hold.
	bool alwaysLands = false;
	// Revive (`living_target`): the action may be aimed at a living ally, whom the revive passes by while the action's other
	// effects still apply.
	bool livingTarget = false;
	// Revive (`hp`): the hit points it stands the target up with, in place of `power`'s percent; capped at the maximum.
	int flatHp = 0;
	// Revive `hp: 0`: the target stands at 0 for the rest of the action, so its later heals count from 0, and at 1 if
	// nothing raised it.
	bool zeroHp = false;
	// StripModifiers (`equipment_affinities`): also takes away what worn pieces did to the target's element affinities, for
	// the rest of the battle.
	bool equipmentAffinities = false;
	// GrantAffinity: the element (in `element`) takes this affinity; with `permanent` a party member keeps it after the
	// battle (saved with the member), otherwise until the battle ends.
	int affinity = 0;
	bool permanent = false;
	bool reaches(CreatureKind kind) const;
};

// The data's hint to automatic choices (autoCommand): Prefer and Avoid rank or weigh the ability, Never keeps it out.
enum class AutoHint { None, Prefer, Avoid, Never };

struct AbilityDef {
	std::string id;
	std::string name;
	AbilityKind kind = AbilityKind::Attack;
	int cost = 0;
	int uses = 0;
	Targeting targeting = Targeting::OneEnemy;
	std::vector<Effect> effects;
	// Usable on party members outside battle.
	bool field = false;
	// A combination component names an ability or a family; any ability of that family fills it (B3).
	std::string family = {};
	// The user's stat the ability attacks, heals and rolls with, and moves stats by; unset uses mental for techniques
	// and strength otherwise, as before.
	std::optional<Stat> attackStat = {};
	// In place of the rules' variance or the original's spread for this ability's damage.
	std::optional<DamageVariance> variance = {};
	AutoHint autoHint = AutoHint::None;

	bool revives() const;
	bool hasEffect(EffectKind kind) const;
};

struct ItemDef {
	std::string id;
	std::string name;
	Targeting targeting = Targeting::OneAlly;
	std::vector<Effect> effects;
	bool field = false;
	// False for an item that is never used up.
	bool consumed = true;
	// `consume_when_no_effect`: used up even when its effects changed nothing; unset, rules.json's.
	std::optional<bool> consumeWhenNoEffect = std::nullopt;
	// `discardable`: false keeps the game's discard from throwing it away (a story item).
	bool discardable = true;
};

bool effectsRevive(const std::vector<Effect>& effects);
// A revive among them may be aimed at a living ally (`living_target`).
bool effectsTakeLiving(const std::vector<Effect>& effects);
// The Teach effect of a field item, or null. Like travel, it acts on the party between battles only.
const Effect* teachEffect(const std::vector<Effect>& effects);
// The Travel effect of a field item or technique, or null. The game moves the party; core only takes the cost, and a
// battle refuses the command.
const Effect* travelEffect(const std::vector<Effect>& effects);
// Effects that mean nothing outside battle (modify_stat, reset_stat, strip_modifiers, grant_affinity that is not
// permanent): a field use skips them.
bool battleOnlyEffect(const Effect& effect);
std::vector<Effect> fieldEffects(const std::vector<Effect>& effects);

}
