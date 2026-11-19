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
	Stats stats;
	int hp = 0;
	int tp = 0;
	std::string basicAttack;
	std::vector<std::string> abilities;
	std::map<std::string, int> skillUses;
	Affinities affinities;
	std::vector<ActiveStatus> statuses;
	std::vector<ActiveModifier> modifiers;
	bool defending = false;

	bool alive() const { return hp > 0; }
	bool knows(const std::string& abilityId) const;
	bool hasStatus(const std::string& statusId) const;
	int effective(Stat stat) const;
	int affinity(Element element) const;
};

}
