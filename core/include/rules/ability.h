#pragma once

#include "rules/types.h"

#include <string>
#include <vector>

namespace rules {

struct Effect {
	EffectKind kind = EffectKind::PhysicalDamage;
	// Damage and heal strength; for Revive, the percent of max hit points restored; for an item's ModifyStat, the amount.
	int power = 0;
	Element element = Element::None;
	std::string status;
	// ApplyStatus and a lowering ModifyStat: the target resists when the status score is at or below this.
	int threshold = 0;
	Stat stat = Stat::Strength;
	// ModifyStat: raises the stat rather than lowering it.
	bool raises = false;
	TravelTarget travel = TravelTarget::Town;
	// The kinds of target the effect works on; empty works on every kind.
	std::vector<CreatureKind> kinds = {};
	// Teach: the ability the target learns for good.
	std::string ability = {};
	bool reaches(CreatureKind kind) const;
};

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

	bool revives() const;
	bool hasEffect(EffectKind kind) const;
};

struct ItemDef {
	std::string id;
	std::string name;
	Targeting targeting = Targeting::OneAlly;
	std::vector<Effect> effects;
	bool field = false;
};

bool effectsRevive(const std::vector<Effect>& effects);
// The Teach effect of a field item, or null. Like travel, it acts on the party between battles only.
const Effect* teachEffect(const std::vector<Effect>& effects);
// The Travel effect of a field item or technique, or null. The game moves the party; core only takes the cost, and a
// battle refuses the command.
const Effect* travelEffect(const std::vector<Effect>& effects);

}
