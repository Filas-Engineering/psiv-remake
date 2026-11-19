#include "rules/progression.h"

#include "rules/formulas.h"

#include <algorithm>

namespace rules {

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
	s.level = gained + 1;
	return s;
}

PartyMember makePartyMember(const Database& db, const CharacterDef& character, int level) {
	PartyMember member;
	member.characterId = character.id;
	member.level = std::clamp(level, 1, formulas::kMaxLevel);
	rest(member, db);
	return member;
}

void rest(PartyMember& member, const Database& db) {
	const CharacterDef* character = db.character(member.characterId);
	if (!character) {
		return;
	}
	const Stats stats = statsAtLevel(*character, member.level);
	member.hp = stats.maxHp;
	member.tp = stats.maxTp;
	member.skillUses.clear();
	for (const std::string& id : character->abilities) {
		const AbilityDef* ability = db.ability(id);
		if (ability && ability->kind == AbilityKind::Skill) {
			member.skillUses[id] = ability->uses;
		}
	}
}

int grantExperience(PartyMember& member, const CharacterDef& character, int experience) {
	member.experience += std::max(0, experience);
	const Stats before = statsAtLevel(character, member.level);
	int gained = 0;
	while (member.level < formulas::kMaxLevel) {
		const int needed = formulas::experienceToNextLevel(member.level);
		if (member.experience < needed) {
			break;
		}
		member.experience -= needed;
		++member.level;
		++gained;
	}
	if (gained > 0) {
		const Stats after = statsAtLevel(character, member.level);
		if (member.hp > 0) {
			member.hp += after.maxHp - before.maxHp;
		}
		member.tp += after.maxTp - before.maxTp;
	}
	return gained;
}

}
