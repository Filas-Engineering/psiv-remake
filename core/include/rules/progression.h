#pragma once

#include "rules/character.h"
#include "rules/database.h"
#include "rules/stats.h"

#include <string>
#include <vector>

namespace rules {

Stats statsAtLevel(const CharacterDef& character, int level);

// Level stats plus equipment bonuses.
Stats memberStats(const Database& db, const PartyMember& member);

// Starting abilities, abilities learned up to the member's level, then equipment-granted abilities.
std::vector<std::string> knownAbilities(const Database& db, const PartyMember& member);

// Full hit points, technique points and skill uses, wearing the character's starting equipment.
PartyMember makePartyMember(const Database& db, const CharacterDef& character, int level);

void rest(PartyMember& member, const Database& db);

// Each returns an empty string on success, otherwise why the change was refused.
std::string canEquip(const Database& db, const std::string& characterId, const std::string& equipmentId);
std::string equip(PartyMember& member, const Database& db, const std::string& equipmentId);
void unequip(PartyMember& member, const Database& db, EquipSlot slot);

struct LevelUp {
	int levelsGained = 0;
	std::vector<std::string> learned;
};

// Gained max hit points and technique points are added to the current values. A newly learned skill starts
// with full uses.
LevelUp grantExperience(PartyMember& member, const Database& db, int experience);

}
