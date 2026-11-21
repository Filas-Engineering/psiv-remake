#pragma once

#include "rules/stats.h"
#include "rules/status.h"
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
	// A machine is reached only by effects that name machines, and stands up after a won or escaped battle (P6).
	CreatureKind kind = CreatureKind::Living;
};

// The persistent state of a party member between battles. The host owns it.
struct PartyMember {
	std::string characterId;
	// The name the player gave the member; empty shows the character's data name. Battle combatants take it.
	std::string name;
	int level = 1;
	int experience = 0;
	int hp = 0;
	int tp = 0;
	std::map<std::string, int> skillUses;
	std::map<EquipSlot, std::string> equipment;
	// Only statuses that persist outside battle.
	std::vector<ActiveStatus> statuses = {};
	// Abilities learned for good outside the level table, from a story event or a found object (P8).
	std::vector<std::string> taught = {};
};

}
