#include "rules/progression.h"

#include "rules/formulas.h"

#include <algorithm>

namespace rules {

namespace {

void addUnique(std::vector<std::string>& list, const std::string& id) {
	if (std::find(list.begin(), list.end(), id) == list.end()) {
		list.push_back(id);
	}
}

void grantSkillUses(PartyMember& member, const Database& db, const std::string& abilityId) {
	const AbilityDef* ability = db.ability(abilityId);
	if (ability && ability->kind == AbilityKind::Skill && !member.skillUses.count(abilityId)) {
		member.skillUses[abilityId] = ability->uses;
	}
}

void clampResources(PartyMember& member, const Database& db) {
	const Stats stats = memberStats(db, member);
	member.hp = std::min(member.hp, stats.maxHp);
	member.tp = std::min(member.tp, stats.maxTp);
}

}

Stats statsAtLevel(const CharacterDef& character, int level) {
	const int gained = std::clamp(level, 1, formulas::kMaxLevel) - 1;
	const Stats& b = character.base;
	const Stats& g = character.growth;
	Stats s;
	s.maxHp = b.maxHp + g.maxHp * gained;
	s.maxTp = b.maxTp + g.maxTp * gained;
	s.strength = b.strength + g.strength * gained;
	s.mental = b.mental + g.mental * gained;
	s.agility = b.agility + g.agility * gained;
	s.dexterity = b.dexterity + g.dexterity * gained;
	s.defense = b.defense + g.defense * gained;
	s.critical = b.critical + g.critical * gained;
	s.evasion = b.evasion + g.evasion * gained;
	s.level = gained + 1;
	return s;
}

Stats memberStats(const Database& db, const PartyMember& member) {
	const CharacterDef* character = db.character(member.characterId);
	if (!character) {
		return Stats();
	}
	Stats s = statsAtLevel(*character, member.level);
	for (const auto& [slot, id] : member.equipment) {
		const EquipmentDef* piece = db.equipmentPiece(id);
		if (!piece) {
			continue;
		}
		const Stats& b = piece->bonus;
		s.maxHp += b.maxHp;
		s.maxTp += b.maxTp;
		s.strength += b.strength;
		s.mental += b.mental;
		s.agility += b.agility;
		s.dexterity += b.dexterity;
		s.defense += b.defense;
		s.critical += b.critical;
		s.evasion += b.evasion;
	}
	s.maxHp = std::max(1, s.maxHp);
	s.maxTp = std::max(0, s.maxTp);
	s.strength = std::max(1, s.strength);
	s.mental = std::max(1, s.mental);
	s.agility = std::max(1, s.agility);
	s.dexterity = std::max(1, s.dexterity);
	s.defense = std::max(0, s.defense);
	s.critical = std::clamp(s.critical, 0, 100);
	s.evasion = std::clamp(s.evasion, 0, 100);
	return s;
}

std::vector<std::string> knownAbilities(const Database& db, const PartyMember& member) {
	std::vector<std::string> known;
	const CharacterDef* character = db.character(member.characterId);
	if (!character) {
		return known;
	}
	for (const std::string& id : character->abilities) {
		addUnique(known, id);
	}
	for (const LearnedAbility& learned : character->learns) {
		if (learned.level <= member.level) {
			addUnique(known, learned.ability);
		}
	}
	for (const auto& [slot, id] : member.equipment) {
		if (const EquipmentDef* piece = db.equipmentPiece(id)) {
			for (const std::string& ability : piece->abilities) {
				addUnique(known, ability);
			}
		}
	}
	return known;
}

PartyMember makePartyMember(const Database& db, const CharacterDef& character, int level) {
	PartyMember member;
	member.characterId = character.id;
	member.level = std::clamp(level, 1, formulas::kMaxLevel);
	for (const std::string& id : character.startingEquipment) {
		equip(member, db, id);
	}
	rest(member, db);
	return member;
}

void rest(PartyMember& member, const Database& db) {
	const Stats stats = memberStats(db, member);
	member.hp = stats.maxHp;
	member.tp = stats.maxTp;
	member.skillUses.clear();
	for (const std::string& id : knownAbilities(db, member)) {
		grantSkillUses(member, db, id);
	}
}

std::string canEquip(const Database& db, const std::string& characterId, const std::string& equipmentId) {
	const CharacterDef* character = db.character(characterId);
	if (!character) {
		return "unknown character '" + characterId + "'";
	}
	const EquipmentDef* piece = db.equipmentPiece(equipmentId);
	if (!piece) {
		return "unknown equipment '" + equipmentId + "'";
	}
	const std::vector<std::string>& allowed = character->equipCategories;
	if (std::find(allowed.begin(), allowed.end(), piece->category) == allowed.end()) {
		return character->name + " cannot equip " + piece->name;
	}
	return "";
}

// A granted skill keeps its uses when the piece comes off, so swapping equipment never refills it.
std::string equip(PartyMember& member, const Database& db, const std::string& equipmentId) {
	const std::string error = canEquip(db, member.characterId, equipmentId);
	if (!error.empty()) {
		return error;
	}
	const EquipmentDef& piece = *db.equipmentPiece(equipmentId);
	member.equipment[piece.slot] = equipmentId;
	for (const std::string& id : piece.abilities) {
		grantSkillUses(member, db, id);
	}
	clampResources(member, db);
	return "";
}

void unequip(PartyMember& member, const Database& db, EquipSlot slot) {
	member.equipment.erase(slot);
	clampResources(member, db);
}

LevelUp grantExperience(PartyMember& member, const Database& db, int experience) {
	LevelUp result;
	const CharacterDef* character = db.character(member.characterId);
	if (!character) {
		return result;
	}
	member.experience += std::max(0, experience);
	const int startLevel = member.level;
	const Stats before = statsAtLevel(*character, member.level);
	while (member.level < formulas::kMaxLevel) {
		const int needed = formulas::experienceToNextLevel(member.level);
		if (member.experience < needed) {
			break;
		}
		member.experience -= needed;
		++member.level;
		++result.levelsGained;
	}
	if (result.levelsGained == 0) {
		return result;
	}
	const Stats after = statsAtLevel(*character, member.level);
	if (member.hp > 0) {
		member.hp += after.maxHp - before.maxHp;
	}
	member.tp += after.maxTp - before.maxTp;
	for (const LearnedAbility& learned : character->learns) {
		if (learned.level > startLevel && learned.level <= member.level) {
			result.learned.push_back(learned.ability);
			grantSkillUses(member, db, learned.ability);
		}
	}
	return result;
}

}
