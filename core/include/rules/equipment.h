#pragma once

#include "rules/stats.h"
#include "rules/types.h"

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
	// Replaces the wearer's basic attack.
	std::string attack;
	std::vector<std::string> abilities;
	StatusResists statusResist;
};

}
