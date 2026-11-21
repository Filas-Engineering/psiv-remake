#include "rules/ability.h"
#include "rules/combatant.h"
#include "rules/combo.h"
#include "rules/database.h"

#include <algorithm>

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

const Effect* travelEffect(const std::vector<Effect>& effects) {
	for (const Effect& effect : effects) {
		if (effect.kind == EffectKind::Travel) {
			return &effect;
		}
	}
	return nullptr;
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

int Combatant::base(Stat stat) const {
	switch (stat) {
	case Stat::Strength: return stats.strength;
	case Stat::Mental: return stats.mental;
	case Stat::Agility: return stats.agility;
	case Stat::Dexterity: return stats.dexterity;
	case Stat::Defense: return stats.defense;
	}
	return 0;
}

int Combatant::effective(Stat stat) const {
	if (std::find(pinned.begin(), pinned.end(), stat) != pinned.end()) {
		return 1;
	}
	for (const ActiveModifier& modifier : modifiers) {
		if (modifier.stat == stat) {
			return base(stat) + modifier.amount;
		}
	}
	return base(stat);
}

void Combatant::setModifier(Stat stat, int amount) {
	for (ActiveModifier& modifier : modifiers) {
		if (modifier.stat == stat) {
			modifier.amount = amount;
			return;
		}
	}
	modifiers.push_back({stat, amount});
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
const VehicleDef* Database::vehicle(const std::string& id) const { return findIn(vehicles, id); }
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
