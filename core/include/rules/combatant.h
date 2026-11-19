#pragma once

#include "rules/stats.h"
#include "rules/types.h"

#include <map>
#include <string>
#include <vector>

namespace rules {

struct ActiveStatus {
	std::string id;
	// Rounds left; 0 lasts until cured.
	int remaining = 0;
};

struct ActiveModifier {
	Stat stat = Stat::Strength;
	int percent = 0;
	int remaining = 0;
};

struct Combatant {
	std::string defId;
	std::string name;
	Side side = Side::Party;
	// Group targeting hits every living combatant on the target's side with the same group.
	std::string group;
	Stats stats;
	int hp = 0;
	int tp = 0;
	std::string basicAttack;
	// Element the basic attack's physical damage takes, from the weapon.
	Element attackElement = Element::None;
	std::vector<std::string> abilities;
	std::map<std::string, int> skillUses;
	Affinities affinities;
	StatusResists statusResist;
	std::vector<ActiveStatus> statuses;
	std::vector<ActiveModifier> modifiers;
	bool defending = false;

	bool alive() const { return hp > 0; }
	bool knows(const std::string& abilityId) const;
	bool hasStatus(const std::string& statusId) const;
	int effective(Stat stat) const;
	int affinity(Element element) const;
	int resistance(const std::string& statusId) const;
};

}
