#pragma once

#include "rules/stats.h"
#include "rules/types.h"

#include <map>
#include <string>
#include <vector>

namespace rules {

struct LearnedAbility {
	int level = 2;
	std::string ability;
};

struct CharacterDef {
	std::string id;
	std::string name;
	Stats base;
	Stats growth;
	std::string basicAttack;
	std::vector<std::string> abilities;
	std::vector<LearnedAbility> learns;
	Affinities affinities;
	StatusResists statusResist;
	std::vector<std::string> equipCategories;
	std::vector<std::string> startingEquipment;
};

// The persistent state of a party member between battles. The host owns it.
struct PartyMember {
	std::string characterId;
	int level = 1;
	int experience = 0;
	int hp = 0;
	int tp = 0;
	std::map<std::string, int> skillUses;
	std::map<EquipSlot, std::string> equipment;
};

}
