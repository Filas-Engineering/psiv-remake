#pragma once

#include "rules/types.h"

#include <string>
#include <vector>

namespace rules {

struct Effect {
	EffectKind kind = EffectKind::PhysicalDamage;
	// Damage and heal strength; for Revive, the percent of max hit points restored.
	int power = 0;
	Element element = Element::None;
	std::string status;
	// ApplyStatus: the target resists when the status score is at or below this.
	int threshold = 0;
	Stat stat = Stat::Strength;
	int percent = 0;
	int duration = 0;
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

}
