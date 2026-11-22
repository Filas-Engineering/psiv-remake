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

// One side of a profile row: a text key of the game's tables, or a whole number shown as written.
struct ProfileText {
	std::string text;
	bool number = false;
};

struct ProfileRow {
	ProfileText key;
	ProfileText value;
};

struct CharacterDef {
	std::string id;
	std::string name;
	Stats base;
	Stats growth;
	// Optional tables in place of the curves: the experience each level up to the table's end costs (entry 0 takes
	// level 1 to 2), and the stats at each level (entry 0 is level 1). Past a table the curve and growth go on.
	std::vector<int> levelExperience = {};
	std::vector<Stats> levelStats = {};
	std::string basicAttack;
	std::vector<std::string> abilities;
	std::vector<LearnedAbility> learns;
	Affinities affinities;
	StatusResists statusResist;
	std::vector<std::string> equipCategories;
	std::vector<std::string> startingEquipment;
	// A machine is reached only by effects that name machines, and stands up after a won or escaped battle (P6).
	CreatureKind kind = CreatureKind::Living;
	// Instant-defeat effects can fell it (rules.json `instant_defeat_kinds` makes whole kinds so).
	bool vulnerable = false;
	// Techniques and skills events can teach it, which combination recipes count as abilities it can use.
	std::vector<std::string> teachable = {};
	// Presentation only: rows a status screen lists in this order (age, home, ...).
	std::vector<ProfileRow> profile = {};
	// party.json `skill_uses`: per skill, (level, uses) rows in rising level order; from a row's level the member's maximum
	// uses of that skill are its uses, in place of the skill's own `uses` (before the first row, the skill's own).
	std::map<std::string, std::vector<std::pair<int, int>>> skillUses = {};
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
	// Element affinities a permanent `grant_affinity` gave it, which stand in for the character's own before its pieces
	// apply.
	Affinities affinities = {};
};

}
