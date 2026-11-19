#include "rules/ability.h"
#include "rules/combatant.h"
#include "rules/combo.h"
#include "rules/database.h"
#include "rules/formulas.h"

namespace rules {

namespace {

template <class Map>
const typename Map::mapped_type* findIn(const Map& map, const std::string& id) {
	const auto it = map.find(id);
	return it == map.end() ? nullptr : &it->second;
}

}

bool effectsRevive(const std::vector<Effect>& effects) {
	for (const Effect& effect : effects) {
		if (effect.kind == EffectKind::Revive) {
			return true;
		}
	}
	return false;
}

bool AbilityDef::revives() const {
	return effectsRevive(effects);
}

bool AbilityDef::hasEffect(EffectKind kind) const {
	for (const Effect& effect : effects) {
		if (effect.kind == kind) {
			return true;
		}
	}
	return false;
}

bool Combatant::knows(const std::string& abilityId) const {
	if (abilityId == basicAttack) {
		return true;
	}
	for (const std::string& id : abilities) {
		if (id == abilityId) {
			return true;
		}
	}
	return false;
}

bool Combatant::hasStatus(const std::string& statusId) const {
	for (const ActiveStatus& status : statuses) {
		if (status.id == statusId) {
			return true;
		}
	}
	return false;
}

int Combatant::effective(Stat stat) const {
	int base = 0;
	switch (stat) {
	case Stat::Strength: base = stats.strength; break;
	case Stat::Mental: base = stats.mental; break;
	case Stat::Agility: base = stats.agility; break;
	case Stat::Dexterity: base = stats.dexterity; break;
	case Stat::Defense: base = stats.defense; break;
	}
	int percent = 0;
	for (const ActiveModifier& modifier : modifiers) {
		if (modifier.stat == stat) {
			percent += modifier.percent;
		}
	}
	return percent == 0 ? base : formulas::modifiedStat(base, percent);
}

int Combatant::affinity(Element element) const {
	const auto it = affinities.find(element);
	return it == affinities.end() ? 100 : it->second;
}

int Combatant::resistance(const std::string& statusId) const {
	const auto it = statusResist.find(statusId);
	return it == statusResist.end() ? 0 : it->second;
}

bool ComboLog::discover(const std::string& recipeId, std::int64_t playTimeMs, const std::vector<std::string>& characters) {
	if (isDiscovered(recipeId)) {
		return false;
	}
	entries_.push_back({recipeId, playTimeMs, characters});
	return true;
}

bool ComboLog::isDiscovered(const std::string& recipeId) const {
	return entry(recipeId) != nullptr;
}

const ComboLogEntry* ComboLog::entry(const std::string& recipeId) const {
	for (const ComboLogEntry& e : entries_) {
		if (e.recipe == recipeId) {
			return &e;
		}
	}
	return nullptr;
}

std::set<std::string> ComboLog::discovered() const {
	std::set<std::string> ids;
	for (const ComboLogEntry& e : entries_) {
		ids.insert(e.recipe);
	}
	return ids;
}

const StatusDef* Database::status(const std::string& id) const { return findIn(statuses, id); }
const AbilityDef* Database::ability(const std::string& id) const { return findIn(abilities, id); }
const ItemDef* Database::item(const std::string& id) const { return findIn(items, id); }
const EquipmentDef* Database::equipmentPiece(const std::string& id) const { return findIn(equipment, id); }
const CharacterDef* Database::character(const std::string& id) const { return findIn(characters, id); }
const EnemyDef* Database::enemy(const std::string& id) const { return findIn(enemies, id); }
const Encounter* Database::encounter(const std::string& id) const { return findIn(encounters, id); }
const DifficultyDef* Database::difficulty(const std::string& id) const { return findIn(difficulties, id); }

const Macro* Database::macro(const std::string& name) const {
	for (const Macro& m : macros) {
		if (m.name == name) {
			return &m;
		}
	}
	return nullptr;
}

}
