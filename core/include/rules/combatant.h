#pragma once

#include "rules/stats.h"
#include "rules/status.h"
#include "rules/types.h"

#include <map>
#include <string>
#include <vector>

namespace rules {

// A stat change: the battle value is the base plus this amount, until the battle ends.
struct ActiveModifier {
	Stat stat = Stat::Strength;
	int amount = 0;
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
	// At most one per stat.
	std::vector<ActiveModifier> modifiers;
	// Stats a current status holds at 1, whatever the modifiers say.
	std::vector<Stat> pinned;
	bool defending = false;
	// The foe that last damaged this combatant, for the last_attacker AI target rule.
	int lastAttacker = -1;
	// Enemies: what landed damage since an AI override last read these (BAT-091).
	bool hitByAttack = false;
	bool hitByAbility = false;
	bool hitByTechnique = false;
	// A party member riding a vehicle in a vehicle battle: it takes no command and cannot be targeted.
	bool aboard = false;

	bool alive() const { return hp > 0; }
	bool knows(const std::string& abilityId) const;
	bool hasStatus(const std::string& statusId) const;
	int base(Stat stat) const;
	int effective(Stat stat) const;
	// Replaces any earlier change to that stat.
	void setModifier(Stat stat, int amount);
	int affinity(Element element) const;
	int resistance(const std::string& statusId) const;
};

}
