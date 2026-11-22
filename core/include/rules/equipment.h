#pragma once

#include "rules/stats.h"
#include "rules/types.h"

#include <optional>
#include <string>
#include <vector>

namespace rules {

struct EquipmentDef {
	std::string id;
	std::string name;
	EquipSlot slot = EquipSlot::Weapon;
	// Characters can equip a piece when its category is in their `equip` list.
	std::string category;
	Stats bonus = Stats::filled(0);
	Element element = Element::None;
	// 2 for a weapon that needs both hands: it is worn in the right hand and leaves the left one empty.
	int hands = 1;
	// Replaces the wearer's basic attack.
	std::string attack;
	std::vector<std::string> abilities;
	StatusResists statusResist;
	// An ability the wearer casts through the Item command at no cost, without using the piece up (P12).
	std::string use = {};
	// Any member who can use items casts `use` with the piece in the bag, at the item rule's flat power.
	bool useFromBag = false;
	// Percents that scale the wearer's affinity per element.
	Affinities affinities = {};
	// Affinities the wearer takes outright per element; they win over every piece's scaling, and the lowest wins.
	Affinities setAffinities = {};
	// The piece's own power, cast as the attacking stat of `use` in place of the wearer's stats or the flat item rule.
	std::optional<int> usePower;
	// Weapons: added to the wearer's attack stat only (`stats.attack`), where `bonus.strength` raises strength itself.
	int attackBonus = 0;
};

}
