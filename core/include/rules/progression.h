#pragma once

#include "rules/character.h"
#include "rules/equipment.h"
#include "rules/database.h"
#include "rules/message.h"
#include "rules/stats.h"

#include <map>
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
// Weapons and shields (P10). A hand piece is worn in either hand: the right hand is the Weapon position, the left
// hand LeftHand. A two-handed weapon is worn in the right hand only, with the left hand empty.
bool isHandPiece(const EquipmentDef& piece);
bool fitsPosition(const EquipmentDef& piece, EquipSlot position);
// Where equip puts a piece when no position is named: a weapon in the right hand, a shield in the left hand, any
// other piece in its slot.
EquipSlot defaultPosition(const EquipmentDef& piece);
// The default position, or the other hand when a one-handed piece's default is taken: how starting equipment is put on.
EquipSlot startingPosition(const std::map<EquipSlot, std::string>& worn, const EquipmentDef& piece);
// Takes the piece out of the bag and puts it on at its default position; the piece worn there goes back into the bag.
Message equip(PartyMember& member, const Database& db, EquipmentBag& bag, const std::string& equipmentId);
// The same at a position the piece fits (`equip.wrong_position` otherwise). A two-handed weapon also sends the left
// hand's piece back, and a piece put in the left hand sends back a two-handed weapon held in the right.
Message equip(PartyMember& member, const Database& db, EquipmentBag& bag, const std::string& equipmentId, EquipSlot position);
// Puts the piece worn at that position back into the bag.
void unequip(PartyMember& member, const Database& db, EquipmentBag& bag, EquipSlot slot);

struct LevelUp {
	int levelsGained = 0;
	std::vector<std::string> learned;
};

// Gained max hit points and technique points are added to the current values. A newly learned skill starts
// with full uses.
LevelUp grantExperience(PartyMember& member, const Database& db, int experience);

}
