#include "rules/progression.h"

#include "rules/formulas.h"
#include "rules/names.h"

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

// A granted skill keeps its uses when the piece comes off, so swapping equipment never refills it.
void putOn(PartyMember& member, const Database& db, const EquipmentDef& piece, EquipSlot position) {
	member.equipment[position] = piece.id;
	for (const std::string& id : piece.abilities) {
		grantSkillUses(member, db, id);
	}
	clampResources(member, db);
}

}

Stats statsAtLevel(const CharacterDef& character, int level) {
	const int capped = std::clamp(level, 1, formulas::kMaxLevel);
	const int rows = static_cast<int>(character.levelStats.size());
	if (rows >= capped) {
		Stats s = character.levelStats[static_cast<std::size_t>(capped - 1)];
		s.level = capped;
		return s;
	}
	const int gained = rows > 0 ? capped - rows : capped - 1;
	const Stats& b = rows > 0 ? character.levelStats.back() : character.base;
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
	s.magicDefense = b.magicDefense + g.magicDefense * gained;
	s.level = capped;
	return s;
}

int experienceToNextLevel(const CharacterDef& character, int level) {
	if (level >= 1 && level <= static_cast<int>(character.levelExperience.size())) {
		return character.levelExperience[static_cast<std::size_t>(level - 1)];
	}
	return formulas::experienceToNextLevel(level);
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
		s.magicDefense += b.magicDefense;
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
	s.magicDefense = std::max(0, s.magicDefense);
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
	for (const std::string& id : member.taught) {
		addUnique(known, id);
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
		if (canEquip(db, character.id, id).empty()) {
			const EquipmentDef& piece = *db.equipmentPiece(id);
			putOn(member, db, piece, startingPosition(member.equipment, piece));
		}
	}
	rest(member, db);
	return member;
}

void rest(PartyMember& member, const Database& db) {
	const Stats stats = memberStats(db, member);
	if (member.hp > 0 || db.battleRules.restRevives) {
		member.hp = stats.maxHp;
	}
	member.tp = stats.maxTp;
	member.skillUses.clear();
	for (const std::string& id : knownAbilities(db, member)) {
		grantSkillUses(member, db, id);
	}
	member.statuses.erase(std::remove_if(member.statuses.begin(), member.statuses.end(), [&db](const ActiveStatus& active) {
		const StatusDef* def = db.status(active.id);
		return !def || def->restCures;
	}), member.statuses.end());
}

Message canEquip(const Database& db, const std::string& characterId, const std::string& equipmentId) {
	const CharacterDef* character = db.character(characterId);
	if (!character) {
		return {"equip.unknown_character", {characterId}};
	}
	const EquipmentDef* piece = db.equipmentPiece(equipmentId);
	if (!piece) {
		return {"equip.unknown_equipment", {equipmentId}};
	}
	const std::vector<std::string>& allowed = character->equipCategories;
	if (std::find(allowed.begin(), allowed.end(), piece->category) == allowed.end()) {
		return {"equip.wrong_category", {characterId, equipmentId}};
	}
	return {};
}

bool isHandPiece(const EquipmentDef& piece) {
	return piece.slot == EquipSlot::Weapon || piece.slot == EquipSlot::Shield;
}

EquipSlot defaultPosition(const EquipmentDef& piece) {
	return piece.slot == EquipSlot::Shield ? EquipSlot::LeftHand : piece.slot;
}

bool fitsPosition(const EquipmentDef& piece, EquipSlot position) {
	if (!isHandPiece(piece)) {
		return position == piece.slot;
	}
	return position == EquipSlot::Weapon || (position == EquipSlot::LeftHand && piece.hands == 1);
}

EquipSlot startingPosition(const std::map<EquipSlot, std::string>& worn, const EquipmentDef& piece) {
	const EquipSlot first = defaultPosition(piece);
	if (!isHandPiece(piece) || piece.hands == 2 || !worn.count(first)) {
		return first;
	}
	return first == EquipSlot::Weapon ? EquipSlot::LeftHand : EquipSlot::Weapon;
}

Message equip(PartyMember& member, const Database& db, EquipmentBag& bag, const std::string& equipmentId) {
	const EquipmentDef* piece = db.equipmentPiece(equipmentId);
	return equip(member, db, bag, equipmentId, piece ? defaultPosition(*piece) : EquipSlot::Weapon);
}

Message equip(PartyMember& member, const Database& db, EquipmentBag& bag, const std::string& equipmentId, EquipSlot position) {
	const Message refused = canEquip(db, member.characterId, equipmentId);
	if (!refused.empty()) {
		return refused;
	}
	const EquipmentDef& piece = *db.equipmentPiece(equipmentId);
	if (!fitsPosition(piece, position)) {
		return {"equip.wrong_position", {equipmentId, toString(position)}};
	}
	const auto held = bag.find(equipmentId);
	if (held == bag.end() || held->second <= 0) {
		return {"equip.not_in_bag", {equipmentId}};
	}
	if (--held->second == 0) {
		bag.erase(held);
	}
	std::vector<EquipSlot> freed = {position};
	if (piece.hands == 2) {
		freed.push_back(EquipSlot::LeftHand);
	}
	const auto right = member.equipment.find(EquipSlot::Weapon);
	if (position == EquipSlot::LeftHand && right != member.equipment.end()) {
		const EquipmentDef* holding = db.equipmentPiece(right->second);
		if (holding && holding->hands == 2) {
			freed.push_back(EquipSlot::Weapon);
		}
	}
	for (EquipSlot slot : freed) {
		const auto worn = member.equipment.find(slot);
		if (worn != member.equipment.end()) {
			++bag[worn->second];
			member.equipment.erase(worn);
		}
	}
	putOn(member, db, piece, position);
	return {};
}

void unequip(PartyMember& member, const Database& db, EquipmentBag& bag, EquipSlot slot) {
	const auto worn = member.equipment.find(slot);
	if (worn == member.equipment.end()) {
		return;
	}
	++bag[worn->second];
	member.equipment.erase(worn);
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
	while (member.level < formulas::kMaxLevel) {
		const int needed = experienceToNextLevel(*character, member.level);
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
	for (const LearnedAbility& learned : character->learns) {
		if (learned.level > startLevel && learned.level <= member.level) {
			result.learned.push_back(learned.ability);
			grantSkillUses(member, db, learned.ability);
		}
	}
	return result;
}

}
