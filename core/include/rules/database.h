#pragma once

#include "rules/ability.h"
#include "rules/battle_rules.h"
#include "rules/character.h"
#include "rules/combo.h"
#include "rules/difficulty.h"
#include "rules/enemy.h"
#include "rules/equipment.h"
#include "rules/macro.h"
#include "rules/status.h"
#include "rules/types.h"
#include "rules/vehicle.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rules {

struct Database {
	BattleRules battleRules;
	std::map<std::string, StatusDef> statuses;
	std::map<std::string, AbilityDef> abilities;
	std::map<std::string, ItemDef> items;
	std::map<std::string, EquipmentDef> equipment;
	std::map<std::string, CharacterDef> characters;
	std::map<std::string, VehicleDef> vehicles;
	std::map<std::string, EnemyDef> enemies;
	std::map<std::string, Encounter> encounters;
	std::vector<ComboRecipe> combos;
	std::vector<EnemyComboRecipe> enemyCombos;
	std::map<std::string, DifficultyDef> difficulties;
	std::vector<Macro> macros;
	std::vector<std::string> lineup;
	Inventory startingInventory;
	EquipmentBag startingEquipmentBag;
	// FNV-1a over the loaded file texts. Battle records carry it so a replay against other data is refused.
	std::uint64_t contentHash = 0;

	const StatusDef* status(const std::string& id) const;
	const AbilityDef* ability(const std::string& id) const;
	const ItemDef* item(const std::string& id) const;
	const EquipmentDef* equipmentPiece(const std::string& id) const;
	const CharacterDef* character(const std::string& id) const;
	const VehicleDef* vehicle(const std::string& id) const;
	const EnemyDef* enemy(const std::string& id) const;
	const Encounter* encounter(const std::string& id) const;
	const DifficultyDef* difficulty(const std::string& id) const;
	const Macro* macro(const std::string& name) const;
};

}
