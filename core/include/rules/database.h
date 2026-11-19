#pragma once

#include "rules/ability.h"
#include "rules/character.h"
#include "rules/combo.h"
#include "rules/enemy.h"
#include "rules/macro.h"
#include "rules/status.h"
#include "rules/types.h"

#include <map>
#include <string>
#include <vector>

namespace rules {

struct Database {
	std::map<std::string, StatusDef> statuses;
	std::map<std::string, AbilityDef> abilities;
	std::map<std::string, ItemDef> items;
	std::map<std::string, CharacterDef> characters;
	std::map<std::string, EnemyDef> enemies;
	std::map<std::string, Encounter> encounters;
	std::vector<ComboRecipe> combos;
	std::vector<Macro> macros;
	std::vector<std::string> lineup;
	Inventory startingInventory;

	const StatusDef* status(const std::string& id) const;
	const AbilityDef* ability(const std::string& id) const;
	const ItemDef* item(const std::string& id) const;
	const CharacterDef* character(const std::string& id) const;
	const EnemyDef* enemy(const std::string& id) const;
	const Encounter* encounter(const std::string& id) const;
	const Macro* macro(const std::string& name) const;
};

}
