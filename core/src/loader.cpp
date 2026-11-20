#include "rules/loader.h"

#include "loader_reader.h"
#include "rules/battle.h"
#include "rules/formulas.h"
#include "rules/names.h"
#include "rules/text.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace rules {

namespace {

using loader::Reader;
using Type = JsonValue::Type;

const std::vector<std::string> kFiles = {
	"rules.json", "statuses.json", "abilities.json", "items.json", "equipment.json", "party.json", "enemies.json", "combos.json", "difficulties.json"};

const char* const kStatKeys[] = {"hp", "tp", "strength", "mental", "agility", "dexterity", "defense"};

int* statField(Stats& stats, const std::string& key) {
	if (key == "hp") return &stats.maxHp;
	if (key == "tp") return &stats.maxTp;
	if (key == "strength") return &stats.strength;
	if (key == "mental") return &stats.mental;
	if (key == "agility") return &stats.agility;
	if (key == "dexterity") return &stats.dexterity;
	if (key == "defense") return &stats.defense;
	if (key == "critical") return &stats.critical;
	if (key == "evasion") return &stats.evasion;
	return nullptr;
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
	return std::find(list.begin(), list.end(), value) != list.end();
}

template <class Map, class Value>
void insertUnique(Map& map, const std::string& id, Value value, Reader& reader, const JsonValue& at) {
	if (map.count(id)) {
		reader.fail(at, "load.duplicate_id", {id});
		return;
	}
	map.emplace(id, std::move(value));
}

void parseStats(Reader& reader, const JsonValue& object, const char* key, Stats& out, bool growth) {
	const JsonValue* stats = reader.get(object, key, Type::Object, true);
	if (!stats) {
		return;
	}
	const int minimum = growth ? 0 : 1;
	reader.rejectUnknownKeys(*stats, {"hp", "tp", "strength", "mental", "agility", "dexterity", "defense", "level", "critical", "evasion"});
	reader.integer(*stats, "hp", out.maxHp, true, minimum, 99999);
	reader.integer(*stats, "tp", out.maxTp, true, 0, 9999);
	reader.integer(*stats, "strength", out.strength, true, minimum, 999);
	reader.integer(*stats, "mental", out.mental, true, minimum, 999);
	reader.integer(*stats, "agility", out.agility, true, minimum, 999);
	reader.integer(*stats, "dexterity", out.dexterity, true, minimum, 999);
	reader.integer(*stats, "defense", out.defense, true, 0, 999);
	reader.integer(*stats, "critical", out.critical, false, 0, growth ? 10 : 100);
	reader.integer(*stats, "evasion", out.evasion, false, 0, growth ? 10 : 100);
	if (!growth) {
		reader.integer(*stats, "level", out.level, false, 1, 99);
	} else if (stats->find("level")) {
		reader.fail(*stats->find("level"), "load.growth_has_no_level", {});
	}
}

void parseAffinities(Reader& reader, const JsonValue& object, Affinities& out) {
	const JsonValue* affinities = reader.get(object, "affinities", Type::Object, false);
	if (!affinities) {
		return;
	}
	for (std::size_t i = 0; i < affinities->keys.size(); ++i) {
		Element element = Element::None;
		if (!parseElement(affinities->keys[i], element) || element == Element::None) {
			reader.fail(affinities->values[i], "load.unknown_element", {affinities->keys[i]});
			continue;
		}
		int percent = 100;
		if (reader.integer(*affinities, affinities->keys[i].c_str(), percent, true, 0, 400)) {
			out[element] = percent;
		}
	}
}

bool readStatusRef(Reader& reader, const Database& db, const JsonValue& object, const char* key, std::string& out, bool required) {
	if (!reader.string(object, key, out, required)) {
		return false;
	}
	if (!db.status(out)) {
		reader.fail(*object.find(key), "load.names_missing_status", {out});
		return false;
	}
	if (out == kDefeatedStatus) {
		reader.fail(*object.find(key), "load.defeated_status_by_hit_points", {});
		return false;
	}
	return true;
}

void parseStatusResist(Reader& reader, const Database& db, const JsonValue& object, StatusResists& out) {
	const JsonValue* resists = reader.get(object, "status_resist", Type::Object, false);
	if (!resists) {
		return;
	}
	for (std::size_t i = 0; i < resists->keys.size(); ++i) {
		const std::string& id = resists->keys[i];
		if (!db.status(id)) {
			reader.fail(resists->values[i], "load.status_resist_names_missing_status", {id});
			continue;
		}
		if (id == kDefeatedStatus) {
			reader.fail(resists->values[i], "load.defeated_status_cannot_resisted", {});
			continue;
		}
		int percent = 0;
		if (reader.integer(*resists, id.c_str(), percent, true, 0, 100)) {
			out[id] = percent;
		}
	}
}

void rejectComboPower(Reader& reader, const JsonValue& entry) {
	if (const JsonValue* power = entry.find("power")) {
		reader.fail(*power, "load.combo_damage_has_no_power", {});
	}
}

// Items give a stat change its amount in `power`; abilities take it from the user. A combo's damage takes its power
// from the participants' components, so it has none of its own.
void parseEffects(Reader& reader, const Database& db, const JsonValue& object, bool item, bool combo, std::vector<Effect>& out) {
	const std::vector<const JsonValue*> entries = reader.objects(object, "effects", true);
	if (entries.empty() && object.find("effects")) {
		reader.fail(*object.find("effects"), "load.needs_at_least_one_effect", {});
	}
	for (const JsonValue* entry : entries) {
		Effect effect;
		std::string type;
		if (!reader.string(*entry, "type", type)) {
			continue;
		}
		if (!parseEffectKind(type, effect.kind)) {
			reader.fail(*entry->find("type"), "load.unknown_effect_type", {type});
			continue;
		}
		switch (effect.kind) {
		case EffectKind::PhysicalDamage:
			reader.rejectUnknownKeys(*entry, {"type", "power"});
			if (combo) {
				rejectComboPower(reader, *entry);
			} else {
				reader.integer(*entry, "power", effect.power, true, 0, 9999);
			}
			break;
		case EffectKind::Heal:
			reader.rejectUnknownKeys(*entry, {"type", "power"});
			reader.integer(*entry, "power", effect.power, true, 1, 9999);
			break;
		case EffectKind::ElementalDamage: {
			reader.rejectUnknownKeys(*entry, {"type", "power", "element"});
			if (combo) {
				rejectComboPower(reader, *entry);
			} else {
				reader.integer(*entry, "power", effect.power, true, 1, 9999);
			}
			std::string element;
			if (reader.string(*entry, "element", element) && (!parseElement(element, effect.element) || effect.element == Element::None)) {
				reader.fail(*entry->find("element"), "load.unknown_element", {element});
			}
			break;
		}
		case EffectKind::ApplyStatus:
			reader.rejectUnknownKeys(*entry, {"type", "status", "threshold"});
			readStatusRef(reader, db, *entry, "status", effect.status, true);
			reader.integer(*entry, "threshold", effect.threshold, false, 0, 255);
			break;
		case EffectKind::CureStatus:
			reader.rejectUnknownKeys(*entry, {"type", "status"});
			readStatusRef(reader, db, *entry, "status", effect.status, true);
			break;
		case EffectKind::ModifyStat: {
			if (item) {
				reader.rejectUnknownKeys(*entry, {"type", "stat", "direction", "threshold", "power"});
				reader.integer(*entry, "power", effect.power, true, 1, 999);
			} else {
				reader.rejectUnknownKeys(*entry, {"type", "stat", "direction", "threshold"});
			}
			std::string text;
			if (reader.string(*entry, "stat", text) && !parseStat(text, effect.stat)) {
				reader.fail(*entry->find("stat"), "load.unknown_stat", {text});
			}
			if (reader.string(*entry, "direction", text)) {
				if (text != "up" && text != "down") {
					reader.fail(*entry->find("direction"), "load.unknown_direction", {text});
				}
				effect.raises = text == "up";
			}
			if (reader.integer(*entry, "threshold", effect.threshold, false, 0, 255) && effect.raises) {
				reader.fail(*entry->find("threshold"), "load.only_lowering_has_threshold", {});
			}
			break;
		}
		case EffectKind::Revive:
			reader.rejectUnknownKeys(*entry, {"type", "power"});
			reader.integer(*entry, "power", effect.power, true, 1, 100);
			break;
		}
		out.push_back(effect);
	}
}

bool readTargeting(Reader& reader, const JsonValue& object, Targeting& out) {
	std::string text;
	if (!reader.string(object, "target", text)) {
		return false;
	}
	if (!parseTargeting(text, out)) {
		reader.fail(*object.find("target"), "load.unknown_target", {text});
		return false;
	}
	return true;
}

void checkReviveTargeting(Reader& reader, const JsonValue& at, Targeting targeting, const std::vector<Effect>& effects) {
	if (effectsRevive(effects) && targeting != Targeting::OneFallenAlly && targeting != Targeting::AllAllies) {
		reader.fail(at, "load.revive_needs_fallen_target", {});
	}
	if (targeting == Targeting::OneFallenAlly && !effectsRevive(effects)) {
		reader.fail(at, "load.one_fallen_ally_needs_revive", {});
	}
}

// Out of battle there are no foes and no rounds, so only effects that act on the party at once qualify.
void readFieldFlag(Reader& reader, const JsonValue& entry, Targeting targeting, const std::vector<Effect>& effects, bool& out) {
	reader.boolean(entry, "field", out);
	if (!out) {
		return;
	}
	const bool allySide = targeting == Targeting::OneAlly || targeting == Targeting::AllAllies || targeting == Targeting::Self || targeting == Targeting::OneFallenAlly;
	if (!allySide) {
		reader.fail(*entry.find("field"), "load.field_use_needs_party_target", {});
	}
	for (const Effect& effect : effects) {
		if (effect.kind != EffectKind::Heal && effect.kind != EffectKind::CureStatus && effect.kind != EffectKind::Revive) {
			reader.fail(*entry.find("field"), "load.field_use_allows_only_heal", {});
			return;
		}
	}
}

void parseRules(Reader& reader, const JsonValue& root, Database& db) {
	BattleRules& rules = db.battleRules;
	reader.rejectUnknownKeys(root, {"surprise_rounds", "rest_revives"});
	reader.integer(root, "surprise_rounds", rules.surpriseRounds, true, 0, 9);
	if (reader.get(root, "rest_revives", Type::Bool, true)) {
		reader.boolean(root, "rest_revives", rules.restRevives);
	}
}

void parseStatuses(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"statuses"});
	std::vector<std::pair<std::string, const JsonValue*>> removals;
	for (const JsonValue* entry : reader.objects(root, "statuses", true)) {
		StatusDef status;
		if (!reader.string(*entry, "id", status.id)) {
			continue;
		}
		reader.setContext("status '" + status.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "name", "action", "forced_target", "blocks_techniques", "wakes_on_damage", "stats_to_one", "removes",
			"duration", "enemy_duration", "recover_chance", "stacking", "persists_outside_battle", "counts_toward_defeat", "survives_defeat", "rest_cures"});
		reader.string(*entry, "name", status.name);
		std::string text;
		if (reader.string(*entry, "action", text, false) && !parseStatusAction(text, status.action)) {
			reader.fail(*entry->find("action"), "load.unknown_action", {text});
		}
		if (reader.string(*entry, "forced_target", text, false)) {
			if (status.action != StatusAction::Forced) {
				reader.fail(*entry->find("forced_target"), "load.only_forced_has_forced_target", {});
			} else if (!parseForcedTarget(text, status.forcedTarget)) {
				reader.fail(*entry->find("forced_target"), "load.unknown_forced_target", {text});
			}
		}
		if (reader.string(*entry, "stacking", text, false) && !parseStatusStacking(text, status.stacking)) {
			reader.fail(*entry->find("stacking"), "load.unknown_stacking", {text});
		}
		reader.boolean(*entry, "persists_outside_battle", status.persistsOutsideBattle);
		reader.boolean(*entry, "counts_toward_defeat", status.countsTowardDefeat);
		reader.boolean(*entry, "survives_defeat", status.survivesDefeat);
		reader.boolean(*entry, "rest_cures", status.restCures);
		reader.boolean(*entry, "blocks_techniques", status.blocksTechniques);
		reader.boolean(*entry, "wakes_on_damage", status.wakesOnDamage);
		for (const std::string& name : reader.strings(*entry, "stats_to_one", false)) {
			Stat stat = Stat::Strength;
			if (!parseStat(name, stat) || (stat != Stat::Agility && stat != Stat::Dexterity)) {
				reader.fail(*entry->find("stats_to_one"), "load.stats_to_one_agility_dexterity", {name});
			} else if (std::find(status.statsToOne.begin(), status.statsToOne.end(), stat) == status.statsToOne.end()) {
				status.statsToOne.push_back(stat);
			}
		}
		status.removes = reader.strings(*entry, "removes", false);
		if (!status.removes.empty() && !db.status(status.id)) {
			removals.emplace_back(status.id, entry->find("removes"));
		}
		reader.integer(*entry, "duration", status.duration, false, 0, 99);
		reader.integer(*entry, "enemy_duration", status.enemyDuration, false, 0, 99);
		reader.integer(*entry, "recover_chance", status.recoverChance, false, 0, 100);
		if (status.recoverChance > 0 && status.action != StatusAction::CannotAct) {
			reader.fail(*entry->find("recover_chance"), "load.only_cannot_act_has_recover_chance", {});
		}
		if (status.id == kDefeatedStatus && entry->keys.size() > 2) {
			reader.fail(*entry, "load.defeated_status_only_id_name", {});
		}
		insertUnique(db.statuses, status.id, status, reader, *entry);
		reader.setContext("");
	}
	if (!db.status(kDefeatedStatus)) {
		reader.fail(root, "load.defeated_status_required", {kDefeatedStatus});
	}
	// A status may remove one listed after it, so the names are checked once every status is known.
	for (const auto& [id, at] : removals) {
		reader.setContext("status '" + id + "'");
		for (const std::string& removed : db.statuses.at(id).removes) {
			if (!db.status(removed) || removed == kDefeatedStatus || removed == id) {
				reader.fail(*at, "load.removes_names_missing_status", {removed});
			}
		}
		reader.setContext("");
	}
}

void parseAbilities(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"abilities"});
	for (const JsonValue* entry : reader.objects(root, "abilities", true)) {
		AbilityDef ability;
		if (!reader.string(*entry, "id", ability.id)) {
			continue;
		}
		reader.setContext("ability '" + ability.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "name", "kind", "cost", "uses", "target", "effects", "field"});
		reader.string(*entry, "name", ability.name);
		std::string kind;
		if (reader.string(*entry, "kind", kind) && !parseAbilityKind(kind, ability.kind)) {
			reader.fail(*entry->find("kind"), "load.unknown_kind", {kind});
		}
		if (ability.kind == AbilityKind::Technique) {
			reader.integer(*entry, "cost", ability.cost, true, 0, 999);
		} else if (entry->find("cost")) {
			reader.fail(*entry->find("cost"), "load.only_techniques_have_cost", {});
		}
		if (ability.kind == AbilityKind::Skill) {
			reader.integer(*entry, "uses", ability.uses, true, 1, 99);
		} else if (entry->find("uses")) {
			reader.fail(*entry->find("uses"), "load.only_skills_have_uses", {});
		}
		readTargeting(reader, *entry, ability.targeting);
		parseEffects(reader, db, *entry, false, ability.kind == AbilityKind::Combo, ability.effects);
		checkReviveTargeting(reader, *entry, ability.targeting, ability.effects);
		readFieldFlag(reader, *entry, ability.targeting, ability.effects, ability.field);
		if (ability.field && ability.kind != AbilityKind::Technique && ability.kind != AbilityKind::Skill) {
			reader.fail(*entry->find("field"), "load.only_techniques_skills_field", {});
		}
		insertUnique(db.abilities, ability.id, ability, reader, *entry);
		reader.setContext("");
	}
}

void parseItems(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"items"});
	for (const JsonValue* entry : reader.objects(root, "items", true)) {
		ItemDef item;
		if (!reader.string(*entry, "id", item.id)) {
			continue;
		}
		reader.setContext("item '" + item.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "name", "target", "effects", "field"});
		reader.string(*entry, "name", item.name);
		readTargeting(reader, *entry, item.targeting);
		parseEffects(reader, db, *entry, true, false, item.effects);
		checkReviveTargeting(reader, *entry, item.targeting, item.effects);
		readFieldFlag(reader, *entry, item.targeting, item.effects, item.field);
		insertUnique(db.items, item.id, item, reader, *entry);
		reader.setContext("");
	}
}

void parseEquipment(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"equipment"});
	for (const JsonValue* entry : reader.objects(root, "equipment", true)) {
		EquipmentDef piece;
		if (!reader.string(*entry, "id", piece.id)) {
			continue;
		}
		reader.setContext("equipment '" + piece.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "name", "slot", "category", "stats", "element", "attack", "abilities", "status_resist"});
		reader.string(*entry, "name", piece.name);
		std::string slot;
		if (reader.string(*entry, "slot", slot) && !parseEquipSlot(slot, piece.slot)) {
			reader.fail(*entry->find("slot"), "load.unknown_slot", {slot});
		}
		reader.string(*entry, "category", piece.category);
		if (const JsonValue* stats = reader.get(*entry, "stats", Type::Object, false)) {
			for (std::size_t i = 0; i < stats->keys.size(); ++i) {
				int* field = statField(piece.bonus, stats->keys[i]);
				if (!field) {
					reader.fail(stats->values[i], "load.unknown_stat", {stats->keys[i]});
					continue;
				}
				reader.integer(*stats, stats->keys[i].c_str(), *field, true, -999, 999);
			}
		}
		const bool weapon = piece.slot == EquipSlot::Weapon;
		std::string element;
		if (reader.string(*entry, "element", element, false)) {
			if (!weapon) {
				reader.fail(*entry->find("element"), "load.only_weapons_have_element", {});
			} else if (!parseElement(element, piece.element) || piece.element == Element::None) {
				reader.fail(*entry->find("element"), "load.unknown_element", {element});
			}
		}
		if (reader.string(*entry, "attack", piece.attack, false)) {
			const AbilityDef* attack = db.ability(piece.attack);
			if (!weapon) {
				reader.fail(*entry->find("attack"), "load.only_weapons_have_attack", {});
			} else if (!attack) {
				reader.fail(*entry->find("attack"), "load.names_missing_ability", {piece.attack});
			} else if (attack->kind != AbilityKind::Attack) {
				reader.fail(*entry->find("attack"), "load.attack_have_kind_attack", {piece.attack});
			}
		}
		piece.abilities = reader.strings(*entry, "abilities", false);
		for (const std::string& id : piece.abilities) {
			const AbilityDef* ability = db.ability(id);
			if (!ability) {
				reader.fail(*entry->find("abilities"), "load.names_missing_ability", {id});
			} else if (ability->kind != AbilityKind::Technique && ability->kind != AbilityKind::Skill) {
				reader.fail(*entry->find("abilities"), "load.ability_technique_skill", {id});
			}
		}
		parseStatusResist(reader, db, *entry, piece.statusResist);
		insertUnique(db.equipment, piece.id, piece, reader, *entry);
		reader.setContext("");
	}
}

void readBasicAttack(Reader& reader, const Database& db, const JsonValue& entry, std::string& out) {
	if (!reader.string(entry, "basic_attack", out)) {
		return;
	}
	const AbilityDef* ability = db.ability(out);
	if (!ability) {
		reader.fail(*entry.find("basic_attack"), "load.names_missing_ability", {out});
	} else if (ability->kind != AbilityKind::Attack) {
		reader.fail(*entry.find("basic_attack"), "load.basic_attack_have_kind_attack", {out});
	}
}

void readAbilityList(Reader& reader, const Database& db, const JsonValue& entry, std::vector<std::string>& out, bool required) {
	out = reader.strings(entry, "abilities", required);
	for (const std::string& id : out) {
		const AbilityDef* ability = db.ability(id);
		if (!ability) {
			reader.fail(*entry.find("abilities"), "load.names_missing_ability", {id});
		} else if (ability->kind != AbilityKind::Technique && ability->kind != AbilityKind::Skill) {
			reader.fail(*entry.find("abilities"), "load.ability_technique_skill", {id});
		}
	}
}

void parseLearns(Reader& reader, const Database& db, const JsonValue& entry, CharacterDef& character) {
	for (const JsonValue* learnEntry : reader.objects(entry, "learns", false)) {
		LearnedAbility learned;
		reader.rejectUnknownKeys(*learnEntry, {"level", "ability"});
		reader.integer(*learnEntry, "level", learned.level, true, 2, formulas::kMaxLevel);
		if (!reader.string(*learnEntry, "ability", learned.ability)) {
			continue;
		}
		const AbilityDef* ability = db.ability(learned.ability);
		if (!ability) {
			reader.fail(*learnEntry, "load.learns_missing_ability", {learned.ability});
		} else if (ability->kind != AbilityKind::Technique && ability->kind != AbilityKind::Skill) {
			reader.fail(*learnEntry, "load.learned_ability_technique_skill", {learned.ability});
		}
		bool known = contains(character.abilities, learned.ability);
		for (const LearnedAbility& other : character.learns) {
			known = known || other.ability == learned.ability;
		}
		if (known) {
			reader.fail(*learnEntry, "load.ability_already_known_learned", {learned.ability});
		}
		character.learns.push_back(learned);
	}
}

void parseCharacterEquipment(Reader& reader, const Database& db, const JsonValue& entry, CharacterDef& character) {
	character.equipCategories = reader.strings(entry, "equip", false);
	for (const std::string& category : character.equipCategories) {
		bool used = false;
		for (const auto& [id, piece] : db.equipment) {
			used = used || piece.category == category;
		}
		if (!used) {
			reader.fail(*entry.find("equip"), "load.no_equipment_has_category", {category});
		}
	}
	character.startingEquipment = reader.strings(entry, "equipment", false);
	std::set<EquipSlot> slots;
	for (const std::string& id : character.startingEquipment) {
		const EquipmentDef* piece = db.equipmentPiece(id);
		if (!piece) {
			reader.fail(*entry.find("equipment"), "load.names_missing_equipment", {id});
			continue;
		}
		if (!contains(character.equipCategories, piece->category)) {
			reader.fail(*entry.find("equipment"), "load.cannot_equip_category", {id, piece->category});
		}
		if (!slots.insert(piece->slot).second) {
			reader.fail(*entry.find("equipment"), "load.two_pieces_for_slot", {std::string(toString(piece->slot))});
		}
	}
}

// Every ability the character can ever use: starting, learned, or granted by equipment it may wear.
bool canEverKnow(const Database& db, const CharacterDef& character, const std::string& abilityId) {
	if (contains(character.abilities, abilityId)) {
		return true;
	}
	for (const LearnedAbility& learned : character.learns) {
		if (learned.ability == abilityId) {
			return true;
		}
	}
	for (const auto& [id, piece] : db.equipment) {
		if (contains(character.equipCategories, piece.category) && contains(piece.abilities, abilityId)) {
			return true;
		}
	}
	return false;
}

void parseCharacters(Reader& reader, const JsonValue& root, Database& db) {
	for (const JsonValue* entry : reader.objects(root, "characters", true)) {
		CharacterDef character;
		if (!reader.string(*entry, "id", character.id)) {
			continue;
		}
		reader.setContext("character '" + character.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "name", "base", "growth", "basic_attack", "abilities", "learns", "affinities", "status_resist", "equip", "equipment"});
		reader.string(*entry, "name", character.name);
		parseStats(reader, *entry, "base", character.base, false);
		parseStats(reader, *entry, "growth", character.growth, true);
		readBasicAttack(reader, db, *entry, character.basicAttack);
		readAbilityList(reader, db, *entry, character.abilities, false);
		parseLearns(reader, db, *entry, character);
		parseAffinities(reader, *entry, character.affinities);
		parseStatusResist(reader, db, *entry, character.statusResist);
		parseCharacterEquipment(reader, db, *entry, character);
		insertUnique(db.characters, character.id, character, reader, *entry);
		reader.setContext("");
	}
}

void parseVehicles(Reader& reader, const JsonValue& root, Database& db) {
	for (const JsonValue* entry : reader.objects(root, "vehicles", false)) {
		VehicleDef vehicle;
		if (!reader.string(*entry, "id", vehicle.id)) {
			continue;
		}
		reader.setContext("vehicle '" + vehicle.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "name", "stats", "basic_attack", "abilities", "affinities", "status_resist"});
		reader.string(*entry, "name", vehicle.name);
		parseStats(reader, *entry, "stats", vehicle.stats, false);
		readBasicAttack(reader, db, *entry, vehicle.basicAttack);
		readAbilityList(reader, db, *entry, vehicle.abilities, false);
		for (const std::string& id : vehicle.abilities) {
			const AbilityDef* ability = db.ability(id);
			if (ability && ability->kind != AbilityKind::Skill) {
				reader.fail(*entry->find("abilities"), "load.vehicle_abilities_skills", {id});
			}
		}
		parseAffinities(reader, *entry, vehicle.affinities);
		parseStatusResist(reader, db, *entry, vehicle.statusResist);
		if (db.character(vehicle.id)) {
			reader.fail(*entry, "load.vehicle_id_also_character_id", {vehicle.id});
		}
		insertUnique(db.vehicles, vehicle.id, vehicle, reader, *entry);
		reader.setContext("");
	}
}

bool targetFits(MacroTargetKind target, CommandKind command, Targeting targeting) {
	if (target == MacroTargetKind::Default) {
		return true;
	}
	switch (command) {
	case CommandKind::Attack:
		return target == MacroTargetKind::Enemy || target == MacroTargetKind::All;
	case CommandKind::Ability:
	case CommandKind::Item:
		switch (targeting) {
		case Targeting::OneEnemy:
		case Targeting::EnemyGroup:
			return target == MacroTargetKind::Enemy;
		case Targeting::AllEnemies:
		case Targeting::AllAllies:
			return target == MacroTargetKind::All;
		case Targeting::OneAlly:
		case Targeting::OneFallenAlly:
			return target == MacroTargetKind::Self || target == MacroTargetKind::Member;
		case Targeting::Self:
			return target == MacroTargetKind::Self;
		case Targeting::RandomEnemy:
			return false;
		}
		return false;
	case CommandKind::None:
	case CommandKind::Defend:
	case CommandKind::Escape:
		return false;
	}
	return false;
}

void parseMacroTarget(Reader& reader, const Database& db, const JsonValue& entry, MacroTarget& out) {
	const JsonValue* target = entry.find("target");
	if (!target) {
		return;
	}
	if (target->is(Type::String)) {
		if (target->string == "self") {
			out.kind = MacroTargetKind::Self;
		} else if (target->string == "all") {
			out.kind = MacroTargetKind::All;
		} else {
			reader.fail(*target, "load.unknown_target", {target->string});
		}
		return;
	}
	if (!target->is(Type::Object)) {
		reader.fail(*target, "load.target_shape", {});
		return;
	}
	reader.rejectUnknownKeys(*target, {"enemy", "member"});
	if (target->keys.size() != 1) {
		reader.fail(*target, "load.target_needs_enemy_or_member", {});
		return;
	}
	if (target->find("enemy")) {
		out.kind = MacroTargetKind::Enemy;
		reader.integer(*target, "enemy", out.enemySlot, true, 0, kMaxEnemies - 1);
	} else if (target->find("member")) {
		out.kind = MacroTargetKind::Member;
		if (reader.string(*target, "member", out.member) && !db.character(out.member)) {
			reader.fail(*target, "load.names_missing_character", {out.member});
		}
	}
}

void parseMacroStep(Reader& reader, const Database& db, const JsonValue& entry, MacroStep& step) {
	reader.rejectUnknownKeys(entry, {"member", "command", "id", "target"});
	if (!reader.string(entry, "member", step.member)) {
		return;
	}
	const CharacterDef* member = db.character(step.member);
	if (!member) {
		reader.fail(*entry.find("member"), "load.names_missing_character", {step.member});
	}
	std::string command;
	if (reader.string(entry, "command", command) && (!parseCommandKind(command, step.kind) || step.kind == CommandKind::None)) {
		reader.fail(*entry.find("command"), "load.unknown_command", {command});
		return;
	}
	Targeting targeting = Targeting::Self;
	if (step.kind == CommandKind::Ability && reader.string(entry, "id", step.id)) {
		const AbilityDef* ability = db.ability(step.id);
		if (!ability) {
			reader.fail(*entry.find("id"), "load.names_missing_ability", {step.id});
		} else {
			targeting = ability->targeting;
			if (member && !canEverKnow(db, *member, step.id)) {
				reader.fail(*entry.find("id"), "load.character_does_not_know_ability", {step.member, step.id});
			}
		}
	} else if (step.kind == CommandKind::Item && reader.string(entry, "id", step.id)) {
		if (const ItemDef* item = db.item(step.id)) {
			targeting = item->targeting;
		} else {
			reader.fail(*entry.find("id"), "load.names_missing_item", {step.id});
		}
	} else if (entry.find("id") && step.kind != CommandKind::Ability && step.kind != CommandKind::Item) {
		reader.fail(*entry.find("id"), "load.only_ability_item_steps_have_id", {});
	}
	parseMacroTarget(reader, db, entry, step.target);
	if (entry.find("target") && !targetFits(step.target.kind, step.kind, targeting)) {
		reader.fail(*entry.find("target"), "load.target_does_not_fit_command", {std::string(toString(step.target.kind))});
	}
}

void parseParty(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"characters", "vehicles", "lineup", "inventory", "equipment_bag", "macros"});
	parseCharacters(reader, root, db);
	parseVehicles(reader, root, db);

	db.lineup = reader.strings(root, "lineup", true);
	const JsonValue* lineup = root.find("lineup");
	if (lineup && (db.lineup.empty() || db.lineup.size() > static_cast<std::size_t>(kMaxPartySize))) {
		reader.fail(*lineup, "load.lineup_needs_1_members", {std::to_string(kMaxPartySize)});
	}
	std::set<std::string> seen;
	for (const std::string& id : db.lineup) {
		if (!db.character(id)) {
			reader.fail(*lineup, "load.lineup_names_missing_character", {id});
		}
		if (!seen.insert(id).second) {
			reader.fail(*lineup, "load.lineup_lists_twice", {id});
		}
	}

	if (const JsonValue* inventory = reader.get(root, "inventory", Type::Object, false)) {
		for (std::size_t i = 0; i < inventory->keys.size(); ++i) {
			const std::string& id = inventory->keys[i];
			if (!db.item(id)) {
				reader.fail(inventory->values[i], "load.inventory_names_missing_item", {id});
				continue;
			}
			int count = 0;
			if (reader.integer(*inventory, id.c_str(), count, true, 0, 99)) {
				db.startingInventory[id] = count;
			}
		}
	}

	if (const JsonValue* bag = reader.get(root, "equipment_bag", Type::Object, false)) {
		for (std::size_t i = 0; i < bag->keys.size(); ++i) {
			const std::string& id = bag->keys[i];
			if (!db.equipmentPiece(id)) {
				reader.fail(bag->values[i], "load.equipment_bag_names_missing_equipment", {id});
				continue;
			}
			int count = 0;
			if (reader.integer(*bag, id.c_str(), count, true, 0, 99)) {
				db.startingEquipmentBag[id] = count;
			}
		}
	}

	for (const JsonValue* entry : reader.objects(root, "macros", false)) {
		Macro macro;
		if (!reader.string(*entry, "name", macro.name)) {
			continue;
		}
		reader.setContext("macro '" + macro.name + "'");
		reader.rejectUnknownKeys(*entry, {"name", "steps"});
		if (db.macro(macro.name)) {
			reader.fail(*entry, "load.duplicate_macro_name", {macro.name});
		}
		std::set<std::string> members;
		for (const JsonValue* stepEntry : reader.objects(*entry, "steps", true)) {
			MacroStep step;
			parseMacroStep(reader, db, *stepEntry, step);
			if (!step.member.empty() && !members.insert(step.member).second) {
				reader.fail(*stepEntry, "load.character_has_two_steps", {step.member});
			}
			macro.steps.push_back(step);
		}
		db.macros.push_back(macro);
		reader.setContext("");
	}
}

bool enemyKnows(const EnemyDef& enemy, const std::string& ability) {
	return ability == enemy.basicAttack || contains(enemy.abilities, ability);
}

void parseAiSlots(Reader& reader, const EnemyDef& enemy, const JsonValue& entry, std::vector<std::string>& out) {
	out = reader.strings(entry, "slots", true);
	const JsonValue* list = entry.find("slots");
	if (list && out.size() != static_cast<std::size_t>(kAiSlots)) {
		reader.fail(*list, "load.needs_slots", {std::to_string(kAiSlots)});
	}
	std::set<std::string> unknown;
	for (const std::string& id : out) {
		if (!enemyKnows(enemy, id) && unknown.insert(id).second) {
			reader.fail(*list, "load.ai_ability_not_in_enemy", {id});
		}
	}
}

void parseAiOverride(Reader& reader, const Database& db, const EnemyDef& enemy, const JsonValue& entry, AiOverride& out) {
	reader.rejectUnknownKeys(entry, {"when", "ability", "target", "target_status"});
	std::string text;
	if (reader.string(entry, "when", text) && !parseAiConditionKind(text, out.when)) {
		reader.fail(*entry.find("when"), "load.unknown_ai_condition", {text});
	}
	if (reader.string(entry, "ability", out.ability) && !enemyKnows(enemy, out.ability)) {
		reader.fail(*entry.find("ability"), "load.ai_ability_not_in_enemy", {out.ability});
	}
	if (!reader.string(entry, "target", text, false)) {
		if (entry.find("target_status")) {
			reader.fail(*entry.find("target_status"), "load.target_status_needs_status_rule", {});
		}
		return;
	}
	if (!parseAiTargetRule(text, out.target.rule)) {
		reader.fail(*entry.find("target"), "load.unknown_ai_target_rule", {text});
		return;
	}
	const bool needsStatus = out.target.rule == AiTargetRule::HasStatus || out.target.rule == AiTargetRule::LacksStatus;
	if (needsStatus) {
		if (reader.string(entry, "target_status", out.target.status) && !db.status(out.target.status)) {
			reader.fail(*entry.find("target_status"), "load.ai_target_names_missing_status", {out.target.status});
		}
	} else if (entry.find("target_status")) {
		reader.fail(*entry.find("target_status"), "load.target_status_needs_status_rule", {});
	}
	const AbilityDef* ability = db.ability(out.ability);
	if (!ability || out.target.rule == AiTargetRule::Default) {
		return;
	}
	const bool foe = ability->targeting == Targeting::OneEnemy || ability->targeting == Targeting::EnemyGroup;
	const bool ally = ability->targeting == Targeting::OneAlly || ability->targeting == Targeting::OneFallenAlly;
	if (!foe && !ally) {
		reader.fail(*entry.find("target"), "load.ai_target_rule_needs_single_target", {std::string(toString(ability->targeting))});
	} else if (out.target.rule == AiTargetRule::LastAttacker && !foe) {
		reader.fail(*entry.find("target"), "load.last_attacker_needs_foe_ability", {});
	}
}

std::vector<AiOverride> parseAiOverrides(Reader& reader, const Database& db, const EnemyDef& enemy, const JsonValue& entry) {
	std::vector<AiOverride> overrides;
	for (const JsonValue* overrideEntry : reader.objects(entry, "overrides", false)) {
		AiOverride override;
		parseAiOverride(reader, db, enemy, *overrideEntry, override);
		overrides.push_back(override);
	}
	if (overrides.size() > static_cast<std::size_t>(kMaxAiOverrides)) {
		reader.fail(*entry.find("overrides"), "load.too_many_overrides", {std::to_string(kMaxAiOverrides)});
	}
	return overrides;
}

void parseEnemies(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"enemies", "encounters"});
	for (const JsonValue* entry : reader.objects(root, "enemies", true)) {
		EnemyDef enemy;
		if (!reader.string(*entry, "id", enemy.id)) {
			continue;
		}
		reader.setContext("enemy '" + enemy.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "name", "stats", "basic_attack", "abilities", "affinities", "status_resist", "slots", "overrides", "experience", "currency"});
		reader.string(*entry, "name", enemy.name);
		parseStats(reader, *entry, "stats", enemy.stats, false);
		readBasicAttack(reader, db, *entry, enemy.basicAttack);
		readAbilityList(reader, db, *entry, enemy.abilities, false);
		parseAffinities(reader, *entry, enemy.affinities);
		parseStatusResist(reader, db, *entry, enemy.statusResist);
		parseAiSlots(reader, enemy, *entry, enemy.slots);
		enemy.overrides = parseAiOverrides(reader, db, enemy, *entry);
		reader.integer(*entry, "experience", enemy.experience, true, 0, 999999);
		reader.integer(*entry, "currency", enemy.currency, true, 0, 999999);
		insertUnique(db.enemies, enemy.id, enemy, reader, *entry);
		reader.setContext("");
	}
	for (const JsonValue* entry : reader.objects(root, "encounters", false)) {
		Encounter encounter;
		if (!reader.string(*entry, "id", encounter.id)) {
			continue;
		}
		reader.setContext("encounter '" + encounter.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "enemies", "escape", "awareness", "escape_rating", "drop"});
		reader.boolean(*entry, "escape", encounter.escapeAllowed);
		if (const JsonValue* drop = reader.get(*entry, "drop", Type::Object, false)) {
			reader.rejectUnknownKeys(*drop, {"item", "rate"});
			if (reader.string(*drop, "item", encounter.drop.item) && !db.item(encounter.drop.item)) {
				reader.fail(*drop->find("item"), "load.drop_missing_item", {encounter.drop.item});
			}
			reader.integer(*drop, "rate", encounter.drop.rate, true, 0, 127);
		}
		const bool awarenessGiven = reader.integer(*entry, "awareness", encounter.awareness, false, 0, 255);
		const bool escapeRatingGiven = reader.integer(*entry, "escape_rating", encounter.escapeRating, false, 0, 255);
		const JsonValue* list = reader.get(*entry, "enemies", Type::Array, true);
		if (!list) {
			reader.setContext("");
			continue;
		}
		for (const JsonValue& item : list->items) {
			FormationSlot slot;
			if (item.is(Type::String) && !item.string.empty()) {
				slot.enemy = item.string;
			} else if (item.is(Type::Object)) {
				reader.rejectUnknownKeys(item, {"enemy", "group"});
				reader.string(item, "enemy", slot.enemy);
				reader.string(item, "group", slot.group, false);
			} else {
				reader.fail(item, "load.encounter_enemy_entry_shape", {});
				continue;
			}
			if (!slot.enemy.empty() && !db.enemy(slot.enemy)) {
				reader.fail(item, "load.names_missing_enemy", {slot.enemy});
			}
			if (slot.group.empty()) {
				slot.group = slot.enemy;
			}
			encounter.enemies.push_back(slot);
		}
		if (encounter.enemies.empty() || encounter.enemies.size() > static_cast<std::size_t>(kMaxEnemies)) {
			reader.fail(*list, "load.needs_1_enemies", {std::to_string(kMaxEnemies)});
		}
		int highestAgility = 0;
		for (const FormationSlot& slot : encounter.enemies) {
			if (const EnemyDef* enemy = db.enemy(slot.enemy)) {
				highestAgility = std::max(highestAgility, enemy->stats.agility);
			}
		}
		if (!awarenessGiven) {
			encounter.awareness = highestAgility;
		}
		if (!escapeRatingGiven) {
			encounter.escapeRating = highestAgility;
		}
		insertUnique(db.encounters, encounter.id, encounter, reader, *entry);
		reader.setContext("");
	}
}

// A component is an ability id, or {"ability": id, "character": id} to tie it to one character.
void parseComboComponents(Reader& reader, const Database& db, const JsonValue& entry, ComboRecipe& combo) {
	const JsonValue* list = reader.get(entry, "abilities", Type::Array, true);
	if (!list) {
		return;
	}
	for (const JsonValue& item : list->items) {
		std::string ability;
		std::string character;
		if (item.is(Type::String) && !item.string.empty()) {
			ability = item.string;
		} else if (item.is(Type::Object)) {
			reader.rejectUnknownKeys(item, {"ability", "character"});
			reader.string(item, "ability", ability);
			if (reader.string(item, "character", character)) {
				const CharacterDef* def = db.character(character);
				if (!def) {
					reader.fail(item, "load.names_missing_character", {character});
				} else if (!ability.empty() && !canEverKnow(db, *def, ability)) {
					reader.fail(item, "load.character_can_never_use", {character, ability});
				}
			}
		} else {
			reader.fail(item, "load.combo_ability_entry_shape", {});
			continue;
		}
		const AbilityDef* def = db.ability(ability);
		if (!ability.empty() && !def) {
			reader.fail(item, "load.names_missing_ability", {ability});
		} else if (def && def->kind != AbilityKind::Technique && def->kind != AbilityKind::Skill) {
			reader.fail(item, "load.ability_technique_skill", {ability});
		}
		combo.abilities.push_back(ability);
		combo.characters.push_back(character);
	}
	if (combo.abilities.size() < 2 || combo.abilities.size() > static_cast<std::size_t>(kMaxComboParticipants)) {
		reader.fail(*list, "load.needs_2_abilities", {std::to_string(kMaxComboParticipants)});
	}
	std::set<std::string> tied;
	for (const std::string& character : combo.characters) {
		if (!character.empty() && !tied.insert(character).second) {
			reader.fail(*list, "load.character_tied_twice", {character});
		}
	}
}

// Whether different characters can take components `from` onward, each one able to use its ability.
bool castable(const Database& db, const ComboRecipe& combo, std::size_t from, std::set<std::string>& used) {
	if (from == combo.abilities.size()) {
		return true;
	}
	for (const auto& [id, character] : db.characters) {
		const bool allowed = combo.characters[from].empty() || combo.characters[from] == id;
		if (allowed && !used.count(id) && canEverKnow(db, character, combo.abilities[from])) {
			used.insert(id);
			if (castable(db, combo, from + 1, used)) {
				return true;
			}
			used.erase(id);
		}
	}
	return false;
}

void parseCombos(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"combos"});
	std::set<std::string> ids;
	std::set<std::vector<std::string>> recipes;
	for (const JsonValue* entry : reader.objects(root, "combos", true)) {
		ComboRecipe combo;
		if (!reader.string(*entry, "id", combo.id)) {
			continue;
		}
		reader.setContext("combo '" + combo.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "abilities", "ordered", "result"});
		if (!ids.insert(combo.id).second) {
			reader.fail(*entry, "load.duplicate_id", {combo.id});
		}
		parseComboComponents(reader, db, *entry, combo);
		reader.boolean(*entry, "ordered", combo.ordered);
		std::set<std::string> used;
		if (combo.abilities.size() >= 2 && !castable(db, combo, 0, used)) {
			reader.fail(*entry, "load.no_characters_can_combine", {});
		}
		std::vector<std::string> sorted = combo.abilities;
		std::sort(sorted.begin(), sorted.end());
		if (!sorted.empty() && !recipes.insert(sorted).second) {
			reader.fail(*entry, "load.combo_abilities_taken", {});
		}
		if (reader.string(*entry, "result", combo.result)) {
			const AbilityDef* result = db.ability(combo.result);
			if (!result) {
				reader.fail(*entry->find("result"), "load.names_missing_ability", {combo.result});
			} else if (result->kind != AbilityKind::Combo) {
				reader.fail(*entry->find("result"), "load.result_have_kind_combo", {combo.result});
			}
		}
		db.combos.push_back(combo);
		reader.setContext("");
	}
}

void parseEnemyOverlay(Reader& reader, const Database& db, const JsonValue& entry, EnemyOverlay& overlay) {
	reader.rejectUnknownKeys(entry, {"id", "stats_percent", "add_abilities", "slots", "overrides", "status_resist"});
	if (!reader.string(entry, "id", overlay.enemy)) {
		return;
	}
	const EnemyDef* base = db.enemy(overlay.enemy);
	if (!base) {
		reader.fail(entry, "load.names_missing_enemy", {overlay.enemy});
		return;
	}
	if (const JsonValue* stats = reader.get(entry, "stats_percent", Type::Object, false)) {
		reader.rejectUnknownKeys(*stats, {"hp", "tp", "strength", "mental", "agility", "dexterity", "defense"});
		for (const char* key : kStatKeys) {
			// The spec caps how far a preset may move hit points, attack and defense.
			const bool capped = std::string(key) == "hp" || std::string(key) == "strength" || std::string(key) == "defense";
			reader.integer(*stats, key, *statField(overlay.statsPercent, key), false, capped ? 80 : 50, capped ? 120 : 200);
		}
	}
	EnemyDef overlaid = *base;
	if (entry.find("add_abilities")) {
		overlay.addAbilities = reader.strings(entry, "add_abilities", false);
		for (const std::string& id : overlay.addAbilities) {
			const AbilityDef* ability = db.ability(id);
			if (!ability) {
				reader.fail(*entry.find("add_abilities"), "load.names_missing_ability", {id});
			} else if (ability->kind != AbilityKind::Technique && ability->kind != AbilityKind::Skill) {
				reader.fail(*entry.find("add_abilities"), "load.ability_technique_skill", {id});
			}
			overlaid.abilities.push_back(id);
		}
	}
	if (entry.find("slots")) {
		overlay.slots.emplace();
		parseAiSlots(reader, overlaid, entry, *overlay.slots);
	}
	if (entry.find("overrides")) {
		overlay.overrides = parseAiOverrides(reader, db, overlaid, entry);
	}
	parseStatusResist(reader, db, entry, overlay.statusResist);
}

void parseDifficulties(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"difficulties"});
	for (const JsonValue* entry : reader.objects(root, "difficulties", true)) {
		DifficultyDef difficulty;
		if (!reader.string(*entry, "id", difficulty.id)) {
			continue;
		}
		reader.setContext("difficulty '" + difficulty.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "name", "reward_percent", "abilities", "enemies"});
		reader.string(*entry, "name", difficulty.name);
		reader.integer(*entry, "reward_percent", difficulty.rewardPercent, false, 1, 1000);
		for (const JsonValue* abilityEntry : reader.objects(*entry, "abilities", false)) {
			AbilityOverlay overlay;
			reader.rejectUnknownKeys(*abilityEntry, {"id", "power_percent", "cost", "uses"});
			if (!reader.string(*abilityEntry, "id", overlay.ability)) {
				continue;
			}
			const AbilityDef* ability = db.ability(overlay.ability);
			if (!ability) {
				reader.fail(*abilityEntry, "load.names_missing_ability", {overlay.ability});
				continue;
			}
			reader.integer(*abilityEntry, "power_percent", overlay.powerPercent, false, 1, 500);
			int value = 0;
			if (abilityEntry->find("cost")) {
				if (ability->kind != AbilityKind::Technique) {
					reader.fail(*abilityEntry->find("cost"), "load.only_techniques_have_cost", {});
				} else if (reader.integer(*abilityEntry, "cost", value, true, 0, 999)) {
					overlay.cost = value;
				}
			}
			if (abilityEntry->find("uses")) {
				if (ability->kind != AbilityKind::Skill) {
					reader.fail(*abilityEntry->find("uses"), "load.only_skills_have_uses", {});
				} else if (reader.integer(*abilityEntry, "uses", value, true, 1, 99)) {
					overlay.uses = value;
				}
			}
			difficulty.abilities.push_back(overlay);
		}
		for (const JsonValue* enemyEntry : reader.objects(*entry, "enemies", false)) {
			EnemyOverlay overlay;
			parseEnemyOverlay(reader, db, *enemyEntry, overlay);
			difficulty.enemies.push_back(overlay);
		}
		insertUnique(db.difficulties, difficulty.id, difficulty, reader, *entry);
		reader.setContext("");
	}
}

std::uint64_t fnv1a(std::uint64_t hash, const std::string& text) {
	for (char c : text) {
		hash ^= static_cast<unsigned char>(c);
		hash *= 1099511628211ULL;
	}
	hash ^= 0xFF;
	return hash * 1099511628211ULL;
}

using FileParser = void (*)(Reader&, const JsonValue&, Database&);

}

const std::vector<std::string>& dataFileNames() {
	return kFiles;
}

Result<Database> loadDatabaseFromTexts(const std::map<std::string, std::string>& files) {
	// Order matters: each file may reference the ones before it.
	const FileParser parsers[] = {parseRules, parseStatuses, parseAbilities, parseItems, parseEquipment, parseParty, parseEnemies, parseCombos, parseDifficulties};
	static_assert(std::size(parsers) == 9, "one parser per data file");
	Database db;
	db.contentHash = 14695981039346656037ULL;
	std::vector<Problem> problems;
	for (std::size_t i = 0; i < kFiles.size(); ++i) {
		const std::string& name = kFiles[i];
		const auto it = files.find(name);
		if (it == files.end()) {
			problems.push_back({name, 0, "", {"load.missing_file", {}}});
			continue;
		}
		db.contentHash = fnv1a(fnv1a(db.contentHash, name), it->second);
		Result<JsonValue> json = parseJson(it->second);
		if (!json.ok()) {
			for (Problem problem : json.problems) {
				problem.file = name;
				problems.push_back(problem);
			}
			continue;
		}
		if (!json.value->is(Type::Object)) {
			problems.push_back({name, json.value->line, "", {"load.top_level_object", {}}});
			continue;
		}
		Reader reader(name, problems);
		parsers[i](reader, *json.value, db);
	}
	if (!problems.empty()) {
		std::string text = formatProblems(problems, problemText());
		return Result<Database>::failure(std::move(problems), std::move(text));
	}
	return Result<Database>::success(std::move(db));
}

Result<Database> loadDatabaseFromDirectory(const std::string& directory) {
	std::map<std::string, std::string> files;
	for (const std::string& name : kFiles) {
		const std::string path = directory + "/" + name;
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			Problem problem{path, 0, "", {"load.cannot_open", {}}};
			return Result<Database>::failure({problem}, formatProblem(problem, problemText()));
		}
		std::ostringstream text;
		text << in.rdbuf();
		files[name] = text.str();
	}
	return loadDatabaseFromTexts(files);
}

}
