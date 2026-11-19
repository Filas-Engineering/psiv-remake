#pragma once

#include "rules/character.h"
#include "rules/database.h"
#include "rules/message.h"
#include "rules/stats.h"

#include <string>
#include <vector>

namespace rules {

Stats statsAtLevel(const CharacterDef& character, int level);

// Level stats plus equipment bonuses.
Stats memberStats(const Database& db, const PartyMember& member);

// Starting abilities, abilities learned up to the member's level, then equipment-granted abilities.
std::vector<std::string> knownAbilities(const Database& db, const PartyMember& member);

// Full hit points, technique points and skill uses, wearing the character's starting equipment. The starting
// pieces come with the character; they are not taken from an equipment bag.
PartyMember makePartyMember(const Database& db, const CharacterDef& character, int level);

// Refills hit points (a fallen member only when rules.json `rest_revives`), technique points and skill uses,
// and cures the statuses whose data has `rest_cures`.
void rest(PartyMember& member, const Database& db);

// Each returns an empty message on success, otherwise why the change was refused.
Message canEquip(const Database& db, const std::string& characterId, const std::string& equipmentId);
// Takes the piece out of the bag; the piece worn in that slot before goes back into it.
Message equip(PartyMember& member, const Database& db, EquipmentBag& bag, const std::string& equipmentId);
// Puts the piece worn in that slot back into the bag.
void unequip(PartyMember& member, const Database& db, EquipmentBag& bag, EquipSlot slot);

struct LevelUp {
	int levelsGained = 0;
	std::vector<std::string> learned;
};

// Gained max hit points and technique points are added to the current values. A newly learned skill starts
// with full uses.
LevelUp grantExperience(PartyMember& member, const Database& db, int experience);

}
