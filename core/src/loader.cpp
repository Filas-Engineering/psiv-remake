#include "rules/loader.h"

#include "loader_reader.h"
#include "rules/battle.h"
#include "rules/formulas.h"
#include "rules/names.h"

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
		reader.fail(at, "duplicate id '" + id + "'");
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
		reader.fail(*stats->find("level"), "growth has no level");
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
			reader.fail(affinities->values[i], "unknown element '" + affinities->keys[i] + "'");
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
		reader.fail(*object.find(key), "names missing status '" + out + "'");
		return false;
	}
	if (out == kDefeatedStatus) {
		reader.fail(*object.find(key), "the defeated status is applied by hit points, not by effects");
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
			reader.fail(resists->values[i], "status_resist names missing status '" + id + "'");
			continue;
		}
		if (id == kDefeatedStatus) {
			reader.fail(resists->values[i], "the defeated status cannot be resisted");
			continue;
		}
		int percent = 0;
		if (reader.integer(*resists, id.c_str(), percent, true, 0, 100)) {
			out[id] = percent;
		}
	}
}

void parseEffects(Reader& reader, const Database& db, const JsonValue& object, std::vector<Effect>& out) {
	const std::vector<const JsonValue*> entries = reader.objects(object, "effects", true);
	if (entries.empty() && object.find("effects")) {
		reader.fail(*object.find("effects"), "needs at least one effect");
	}
	for (const JsonValue* entry : entries) {
		Effect effect;
		std::string type;
		if (!reader.string(*entry, "type", type)) {
			continue;
		}
		if (!parseEffectKind(type, effect.kind)) {
			reader.fail(*entry->find("type"), "unknown effect type '" + type + "'");
			continue;
		}
		switch (effect.kind) {
		case EffectKind::PhysicalDamage:
			reader.rejectUnknownKeys(*entry, {"type", "power", "critical"});
			reader.integer(*entry, "power", effect.power, true, 1, 9999);
			reader.integer(*entry, "critical", effect.critical, false, 0, 100);
			break;
		case EffectKind::Heal:
			reader.rejectUnknownKeys(*entry, {"type", "power"});
			reader.integer(*entry, "power", effect.power, true, 1, 9999);
			break;
		case EffectKind::ElementalDamage: {
			reader.rejectUnknownKeys(*entry, {"type", "power", "element"});
			reader.integer(*entry, "power", effect.power, true, 1, 9999);
			std::string element;
			if (reader.string(*entry, "element", element) && (!parseElement(element, effect.element) || effect.element == Element::None)) {
				reader.fail(*entry->find("element"), "unknown element '" + element + "'");
			}
			break;
		}
		case EffectKind::ApplyStatus:
			reader.rejectUnknownKeys(*entry, {"type", "status", "chance"});
			readStatusRef(reader, db, *entry, "status", effect.status, true);
			reader.integer(*entry, "chance", effect.chance, false, 1, 100);
			break;
		case EffectKind::CureStatus:
			reader.rejectUnknownKeys(*entry, {"type", "status"});
			readStatusRef(reader, db, *entry, "status", effect.status, true);
			break;
		case EffectKind::ModifyStat: {
			reader.rejectUnknownKeys(*entry, {"type", "stat", "percent", "duration"});
			std::string stat;
			if (reader.string(*entry, "stat", stat) && !parseStat(stat, effect.stat)) {
				reader.fail(*entry->find("stat"), "unknown stat '" + stat + "'");
			}
			if (reader.integer(*entry, "percent", effect.percent, true, -90, 200) && effect.percent == 0) {
				reader.fail(*entry->find("percent"), "percent cannot be 0");
			}
			reader.integer(*entry, "duration", effect.duration, true, 1, 99);
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
		reader.fail(*object.find("target"), "unknown target '" + text + "'");
		return false;
	}
	return true;
}

void checkReviveTargeting(Reader& reader, const JsonValue& at, Targeting targeting, const std::vector<Effect>& effects) {
	if (effectsRevive(effects) && targeting != Targeting::OneFallenAlly && targeting != Targeting::AllAllies) {
		reader.fail(at, "revive effects need target one_fallen_ally or all_allies");
	}
	if (targeting == Targeting::OneFallenAlly && !effectsRevive(effects)) {
		reader.fail(at, "target one_fallen_ally needs a revive effect");
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
		reader.fail(*entry.find("field"), "field use needs a party target (one_ally, all_allies, self or one_fallen_ally)");
	}
	for (const Effect& effect : effects) {
		if (effect.kind != EffectKind::Heal && effect.kind != EffectKind::CureStatus && effect.kind != EffectKind::Revive) {
			reader.fail(*entry.find("field"), "field use allows only heal, cure_status and revive effects");
			return;
		}
	}
}

void parseRules(Reader& reader, const JsonValue& root, Database& db) {
	BattleRules& rules = db.battleRules;
	reader.rejectUnknownKeys(root, {"surprise_rounds", "critical_damage_percent", "experience_split", "escape", "rest_revives"});
	reader.integer(root, "surprise_rounds", rules.surpriseRounds, true, 0, 9);
	reader.integer(root, "critical_damage_percent", rules.criticalDamagePercent, true, 100, 400);
	if (reader.get(root, "rest_revives", Type::Bool, true)) {
		reader.boolean(root, "rest_revives", rules.restRevives);
	}
	std::string split;
	if (reader.string(root, "experience_split", split) && !parseExperienceSplit(split, rules.experienceSplit)) {
		reader.fail(*root.find("experience_split"), "unknown experience split '" + split + "'");
	}
	if (const JsonValue* escape = reader.get(root, "escape", Type::Object, true)) {
		reader.rejectUnknownKeys(*escape, {"base_chance", "agility_factor", "min_chance", "max_chance"});
		reader.integer(*escape, "base_chance", rules.escape.baseChance, true, 0, 100);
		reader.integer(*escape, "agility_factor", rules.escape.agilityFactor, true, 0, 100);
		reader.integer(*escape, "min_chance", rules.escape.minChance, true, 0, 100);
		reader.integer(*escape, "max_chance", rules.escape.maxChance, true, 0, 100);
		if (rules.escape.minChance > rules.escape.maxChance) {
			reader.fail(*escape, "min_chance is above max_chance");
		}
	}
}

void parseStatuses(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"statuses"});
	for (const JsonValue* entry : reader.objects(root, "statuses", true)) {
		StatusDef status;
		if (!reader.string(*entry, "id", status.id)) {
			continue;
		}
		reader.setContext("status '" + status.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "name", "action", "forced_target", "blocks_techniques", "wakes_on_damage", "tick_damage_percent", "duration",
			"recover_chance", "stacking", "persists_outside_battle", "counts_toward_defeat", "rest_cures"});
		reader.string(*entry, "name", status.name);
		std::string text;
		if (reader.string(*entry, "action", text, false) && !parseStatusAction(text, status.action)) {
			reader.fail(*entry->find("action"), "unknown action '" + text + "'");
		}
		if (reader.string(*entry, "forced_target", text, false)) {
			if (status.action != StatusAction::Forced) {
				reader.fail(*entry->find("forced_target"), "only a forced action has a forced_target");
			} else if (!parseForcedTarget(text, status.forcedTarget)) {
				reader.fail(*entry->find("forced_target"), "unknown forced_target '" + text + "'");
			}
		}
		if (reader.string(*entry, "stacking", text, false) && !parseStatusStacking(text, status.stacking)) {
			reader.fail(*entry->find("stacking"), "unknown stacking '" + text + "'");
		}
		reader.boolean(*entry, "persists_outside_battle", status.persistsOutsideBattle);
		reader.boolean(*entry, "counts_toward_defeat", status.countsTowardDefeat);
		reader.boolean(*entry, "rest_cures", status.restCures);
		reader.boolean(*entry, "blocks_techniques", status.blocksTechniques);
		reader.boolean(*entry, "wakes_on_damage", status.wakesOnDamage);
		reader.integer(*entry, "tick_damage_percent", status.tickDamagePercent, false, 0, 100);
		reader.integer(*entry, "duration", status.duration, false, 0, 99);
		reader.integer(*entry, "recover_chance", status.recoverChance, false, 0, 100);
		if (status.recoverChance > 0 && status.action != StatusAction::CannotAct) {
			reader.fail(*entry->find("recover_chance"), "only a cannot_act status has a recover_chance");
		}
		if (status.id == kDefeatedStatus && entry->keys.size() > 2) {
			reader.fail(*entry, "the defeated status has only an id and a name");
		}
		insertUnique(db.statuses, status.id, status, reader, *entry);
		reader.setContext("");
	}
	if (!db.status(kDefeatedStatus)) {
		reader.fail(root, std::string("a status with id '") + kDefeatedStatus + "' is required");
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
			reader.fail(*entry->find("kind"), "unknown kind '" + kind + "'");
		}
		if (ability.kind == AbilityKind::Technique) {
			reader.integer(*entry, "cost", ability.cost, true, 0, 999);
		} else if (entry->find("cost")) {
			reader.fail(*entry->find("cost"), "only techniques have a cost");
		}
		if (ability.kind == AbilityKind::Skill) {
			reader.integer(*entry, "uses", ability.uses, true, 1, 99);
		} else if (entry->find("uses")) {
			reader.fail(*entry->find("uses"), "only skills have uses");
		}
		readTargeting(reader, *entry, ability.targeting);
		parseEffects(reader, db, *entry, ability.effects);
		checkReviveTargeting(reader, *entry, ability.targeting, ability.effects);
		readFieldFlag(reader, *entry, ability.targeting, ability.effects, ability.field);
		if (ability.field && ability.kind != AbilityKind::Technique && ability.kind != AbilityKind::Skill) {
			reader.fail(*entry->find("field"), "only techniques and skills can be used outside battle");
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
		parseEffects(reader, db, *entry, item.effects);
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
			reader.fail(*entry->find("slot"), "unknown slot '" + slot + "'");
		}
		reader.string(*entry, "category", piece.category);
		if (const JsonValue* stats = reader.get(*entry, "stats", Type::Object, false)) {
			for (std::size_t i = 0; i < stats->keys.size(); ++i) {
				int* field = statField(piece.bonus, stats->keys[i]);
				if (!field) {
					reader.fail(stats->values[i], "unknown stat '" + stats->keys[i] + "'");
					continue;
				}
				reader.integer(*stats, stats->keys[i].c_str(), *field, true, -999, 999);
			}
		}
		const bool weapon = piece.slot == EquipSlot::Weapon;
		std::string element;
		if (reader.string(*entry, "element", element, false)) {
			if (!weapon) {
				reader.fail(*entry->find("element"), "only weapons have an element");
			} else if (!parseElement(element, piece.element) || piece.element == Element::None) {
				reader.fail(*entry->find("element"), "unknown element '" + element + "'");
			}
		}
		if (reader.string(*entry, "attack", piece.attack, false)) {
			const AbilityDef* attack = db.ability(piece.attack);
			if (!weapon) {
				reader.fail(*entry->find("attack"), "only weapons have an attack");
			} else if (!attack) {
				reader.fail(*entry->find("attack"), "names missing ability '" + piece.attack + "'");
			} else if (attack->kind != AbilityKind::Attack) {
				reader.fail(*entry->find("attack"), "attack '" + piece.attack + "' must have kind attack");
			}
		}
		piece.abilities = reader.strings(*entry, "abilities", false);
		for (const std::string& id : piece.abilities) {
			const AbilityDef* ability = db.ability(id);
			if (!ability) {
				reader.fail(*entry->find("abilities"), "names missing ability '" + id + "'");
			} else if (ability->kind != AbilityKind::Technique && ability->kind != AbilityKind::Skill) {
				reader.fail(*entry->find("abilities"), "ability '" + id + "' must be a technique or a skill");
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
		reader.fail(*entry.find("basic_attack"), "names missing ability '" + out + "'");
	} else if (ability->kind != AbilityKind::Attack) {
		reader.fail(*entry.find("basic_attack"), "basic attack '" + out + "' must have kind attack");
	}
}

void readAbilityList(Reader& reader, const Database& db, const JsonValue& entry, std::vector<std::string>& out, bool required) {
	out = reader.strings(entry, "abilities", required);
	for (const std::string& id : out) {
		const AbilityDef* ability = db.ability(id);
		if (!ability) {
			reader.fail(*entry.find("abilities"), "names missing ability '" + id + "'");
		} else if (ability->kind != AbilityKind::Technique && ability->kind != AbilityKind::Skill) {
			reader.fail(*entry.find("abilities"), "ability '" + id + "' must be a technique or a skill");
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
			reader.fail(*learnEntry, "learns missing ability '" + learned.ability + "'");
		} else if (ability->kind != AbilityKind::Technique && ability->kind != AbilityKind::Skill) {
			reader.fail(*learnEntry, "learned ability '" + learned.ability + "' must be a technique or a skill");
		}
		bool known = contains(character.abilities, learned.ability);
		for (const LearnedAbility& other : character.learns) {
			known = known || other.ability == learned.ability;
		}
		if (known) {
			reader.fail(*learnEntry, "ability '" + learned.ability + "' is already known or learned");
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
			reader.fail(*entry.find("equip"), "no equipment has category '" + category + "'");
		}
	}
	character.startingEquipment = reader.strings(entry, "equipment", false);
	std::set<EquipSlot> slots;
	for (const std::string& id : character.startingEquipment) {
		const EquipmentDef* piece = db.equipmentPiece(id);
		if (!piece) {
			reader.fail(*entry.find("equipment"), "names missing equipment '" + id + "'");
			continue;
		}
		if (!contains(character.equipCategories, piece->category)) {
			reader.fail(*entry.find("equipment"), "cannot equip '" + id + "' (category '" + piece->category + "')");
		}
		if (!slots.insert(piece->slot).second) {
			reader.fail(*entry.find("equipment"), "more than one piece for slot '" + std::string(toString(piece->slot)) + "'");
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
		reader.rejectUnknownKeys(*entry, {"id", "name", "stats", "basic_attack", "abilities", "affinities", "status_resist", "crew_damage_percent"});
		reader.string(*entry, "name", vehicle.name);
		parseStats(reader, *entry, "stats", vehicle.stats, false);
		readBasicAttack(reader, db, *entry, vehicle.basicAttack);
		readAbilityList(reader, db, *entry, vehicle.abilities, false);
		parseAffinities(reader, *entry, vehicle.affinities);
		parseStatusResist(reader, db, *entry, vehicle.statusResist);
		reader.integer(*entry, "crew_damage_percent", vehicle.crewDamagePercent, false, 0, 100);
		if (db.character(vehicle.id)) {
			reader.fail(*entry, "vehicle id '" + vehicle.id + "' is also a character id");
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
			reader.fail(*target, "unknown target '" + target->string + "'");
		}
		return;
	}
	if (!target->is(Type::Object)) {
		reader.fail(*target, "target must be \"self\", \"all\", {\"enemy\": slot} or {\"member\": character}");
		return;
	}
	reader.rejectUnknownKeys(*target, {"enemy", "member"});
	if (target->keys.size() != 1) {
		reader.fail(*target, "target needs exactly one of 'enemy' or 'member'");
		return;
	}
	if (target->find("enemy")) {
		out.kind = MacroTargetKind::Enemy;
		reader.integer(*target, "enemy", out.enemySlot, true, 0, kMaxEnemies - 1);
	} else if (target->find("member")) {
		out.kind = MacroTargetKind::Member;
		if (reader.string(*target, "member", out.member) && !db.character(out.member)) {
			reader.fail(*target, "names missing character '" + out.member + "'");
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
		reader.fail(*entry.find("member"), "names missing character '" + step.member + "'");
	}
	std::string command;
	if (reader.string(entry, "command", command) && (!parseCommandKind(command, step.kind) || step.kind == CommandKind::None)) {
		reader.fail(*entry.find("command"), "unknown command '" + command + "'");
		return;
	}
	Targeting targeting = Targeting::Self;
	if (step.kind == CommandKind::Ability && reader.string(entry, "id", step.id)) {
		const AbilityDef* ability = db.ability(step.id);
		if (!ability) {
			reader.fail(*entry.find("id"), "names missing ability '" + step.id + "'");
		} else {
			targeting = ability->targeting;
			if (member && !canEverKnow(db, *member, step.id)) {
				reader.fail(*entry.find("id"), "character '" + step.member + "' does not know ability '" + step.id + "'");
			}
		}
	} else if (step.kind == CommandKind::Item && reader.string(entry, "id", step.id)) {
		if (const ItemDef* item = db.item(step.id)) {
			targeting = item->targeting;
		} else {
			reader.fail(*entry.find("id"), "names missing item '" + step.id + "'");
		}
	} else if (entry.find("id") && step.kind != CommandKind::Ability && step.kind != CommandKind::Item) {
		reader.fail(*entry.find("id"), "only ability and item steps have an id");
	}
	parseMacroTarget(reader, db, entry, step.target);
	if (entry.find("target") && !targetFits(step.target.kind, step.kind, targeting)) {
		reader.fail(*entry.find("target"), "target '" + std::string(toString(step.target.kind)) + "' does not fit this command");
	}
}

void parseParty(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"characters", "vehicles", "lineup", "inventory", "equipment_bag", "macros"});
	parseCharacters(reader, root, db);
	parseVehicles(reader, root, db);

	db.lineup = reader.strings(root, "lineup", true);
	const JsonValue* lineup = root.find("lineup");
	if (lineup && (db.lineup.empty() || db.lineup.size() > static_cast<std::size_t>(kMaxPartySize))) {
		reader.fail(*lineup, "lineup needs 1 to " + std::to_string(kMaxPartySize) + " members");
	}
	std::set<std::string> seen;
	for (const std::string& id : db.lineup) {
		if (!db.character(id)) {
			reader.fail(*lineup, "lineup names missing character '" + id + "'");
		}
		if (!seen.insert(id).second) {
			reader.fail(*lineup, "lineup lists '" + id + "' twice");
		}
	}

	if (const JsonValue* inventory = reader.get(root, "inventory", Type::Object, false)) {
		for (std::size_t i = 0; i < inventory->keys.size(); ++i) {
			const std::string& id = inventory->keys[i];
			if (!db.item(id)) {
				reader.fail(inventory->values[i], "inventory names missing item '" + id + "'");
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
				reader.fail(bag->values[i], "equipment_bag names missing equipment '" + id + "'");
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
			reader.fail(*entry, "duplicate macro name '" + macro.name + "'");
		}
		std::set<std::string> members;
		for (const JsonValue* stepEntry : reader.objects(*entry, "steps", true)) {
			MacroStep step;
			parseMacroStep(reader, db, *stepEntry, step);
			if (!step.member.empty() && !members.insert(step.member).second) {
				reader.fail(*stepEntry, "character '" + step.member + "' has more than one step");
			}
			macro.steps.push_back(step);
		}
		db.macros.push_back(macro);
		reader.setContext("");
	}
}

void parseAiTarget(Reader& reader, const Database& db, const JsonValue& entry, AiRule& rule) {
	std::string text;
	if (!reader.string(entry, "target", text, false)) {
		if (entry.find("target_status")) {
			reader.fail(*entry.find("target_status"), "target_status needs target has_status or lacks_status");
		}
		return;
	}
	if (!parseAiTargetRule(text, rule.target.rule)) {
		reader.fail(*entry.find("target"), "unknown AI target rule '" + text + "'");
		return;
	}
	const bool needsStatus = rule.target.rule == AiTargetRule::HasStatus || rule.target.rule == AiTargetRule::LacksStatus;
	if (needsStatus) {
		if (reader.string(entry, "target_status", rule.target.status) && !db.status(rule.target.status)) {
			reader.fail(*entry.find("target_status"), "AI target names missing status '" + rule.target.status + "'");
		}
	} else if (entry.find("target_status")) {
		reader.fail(*entry.find("target_status"), "target_status needs target has_status or lacks_status");
	}
	const AbilityDef* ability = db.ability(rule.ability);
	if (!ability || rule.target.rule == AiTargetRule::Default) {
		return;
	}
	const bool foe = ability->targeting == Targeting::OneEnemy || ability->targeting == Targeting::EnemyGroup;
	const bool ally = ability->targeting == Targeting::OneAlly || ability->targeting == Targeting::OneFallenAlly;
	if (!foe && !ally) {
		reader.fail(*entry.find("target"), "AI target rules need a single-target ability, not '" + std::string(toString(ability->targeting)) + "'");
	} else if (rule.target.rule == AiTargetRule::LastAttacker && !foe) {
		reader.fail(*entry.find("target"), "last_attacker needs an ability aimed at foes");
	}
}

void parseAiRule(Reader& reader, const Database& db, const EnemyDef& enemy, const JsonValue& entry, AiRule& rule) {
	reader.rejectUnknownKeys(entry, {"ability", "weight", "when", "target", "target_status"});
	if (reader.string(entry, "ability", rule.ability)) {
		const bool known = rule.ability == enemy.basicAttack
			|| std::find(enemy.abilities.begin(), enemy.abilities.end(), rule.ability) != enemy.abilities.end();
		if (!db.ability(rule.ability)) {
			reader.fail(*entry.find("ability"), "AI names missing ability '" + rule.ability + "'");
		} else if (!known) {
			reader.fail(*entry.find("ability"), "AI uses ability '" + rule.ability + "' that is not in the enemy's abilities");
		}
	}
	reader.integer(entry, "weight", rule.weight, false, 1, 1000);
	parseAiTarget(reader, db, entry, rule);
	const JsonValue* when = reader.get(entry, "when", Type::Object, false);
	if (!when) {
		return;
	}
	reader.rejectUnknownKeys(*when, {"type", "value", "status"});
	std::string type;
	if (reader.string(*when, "type", type) && !parseAiConditionKind(type, rule.condition.kind)) {
		reader.fail(*when->find("type"), "unknown AI condition '" + type + "'");
		return;
	}
	switch (rule.condition.kind) {
	case AiConditionKind::SelfHpBelow:
	case AiConditionKind::AllyHpBelow:
		reader.integer(*when, "value", rule.condition.value, true, 1, 100);
		break;
	case AiConditionKind::SelfHasStatus:
	case AiConditionKind::FoeHasStatus:
		if (reader.string(*when, "status", rule.condition.status) && !db.status(rule.condition.status)) {
			reader.fail(*when->find("status"), "AI condition names missing status '" + rule.condition.status + "'");
		}
		break;
	case AiConditionKind::Always:
	case AiConditionKind::AllyDefeated:
		break;
	}
}

void parseEnemies(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"enemies", "encounters"});
	for (const JsonValue* entry : reader.objects(root, "enemies", true)) {
		EnemyDef enemy;
		if (!reader.string(*entry, "id", enemy.id)) {
			continue;
		}
		reader.setContext("enemy '" + enemy.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "name", "stats", "basic_attack", "abilities", "affinities", "status_resist", "ai", "experience", "currency", "drops"});
		reader.string(*entry, "name", enemy.name);
		parseStats(reader, *entry, "stats", enemy.stats, false);
		readBasicAttack(reader, db, *entry, enemy.basicAttack);
		readAbilityList(reader, db, *entry, enemy.abilities, false);
		parseAffinities(reader, *entry, enemy.affinities);
		parseStatusResist(reader, db, *entry, enemy.statusResist);
		for (const JsonValue* ruleEntry : reader.objects(*entry, "ai", false)) {
			AiRule rule;
			parseAiRule(reader, db, enemy, *ruleEntry, rule);
			enemy.ai.push_back(rule);
		}
		reader.integer(*entry, "experience", enemy.experience, true, 0, 999999);
		reader.integer(*entry, "currency", enemy.currency, true, 0, 999999);
		for (const JsonValue* dropEntry : reader.objects(*entry, "drops", false)) {
			ItemDrop drop;
			reader.rejectUnknownKeys(*dropEntry, {"item", "chance", "count"});
			if (reader.string(*dropEntry, "item", drop.item) && !db.item(drop.item)) {
				reader.fail(*dropEntry->find("item"), "drops missing item '" + drop.item + "'");
			}
			reader.integer(*dropEntry, "chance", drop.chance, true, 1, 100);
			reader.integer(*dropEntry, "count", drop.count, false, 1, 99);
			enemy.drops.push_back(drop);
		}
		insertUnique(db.enemies, enemy.id, enemy, reader, *entry);
		reader.setContext("");
	}
	for (const JsonValue* entry : reader.objects(root, "encounters", false)) {
		Encounter encounter;
		if (!reader.string(*entry, "id", encounter.id)) {
			continue;
		}
		reader.setContext("encounter '" + encounter.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "enemies", "escape"});
		reader.boolean(*entry, "escape", encounter.escapeAllowed);
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
				reader.fail(item, "entries of 'enemies' must be enemy ids or {\"enemy\", \"group\"} objects");
				continue;
			}
			if (!slot.enemy.empty() && !db.enemy(slot.enemy)) {
				reader.fail(item, "names missing enemy '" + slot.enemy + "'");
			}
			if (slot.group.empty()) {
				slot.group = slot.enemy;
			}
			encounter.enemies.push_back(slot);
		}
		if (encounter.enemies.empty() || encounter.enemies.size() > static_cast<std::size_t>(kMaxEnemies)) {
			reader.fail(*list, "needs 1 to " + std::to_string(kMaxEnemies) + " enemies");
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
					reader.fail(item, "names missing character '" + character + "'");
				} else if (!ability.empty() && !canEverKnow(db, *def, ability)) {
					reader.fail(item, "character '" + character + "' can never use '" + ability + "'");
				}
			}
		} else {
			reader.fail(item, "entries of 'abilities' must be ability ids or {\"ability\", \"character\"} objects");
			continue;
		}
		const AbilityDef* def = db.ability(ability);
		if (!ability.empty() && !def) {
			reader.fail(item, "names missing ability '" + ability + "'");
		} else if (def && def->kind != AbilityKind::Technique && def->kind != AbilityKind::Skill) {
			reader.fail(item, "ability '" + ability + "' must be a technique or a skill");
		}
		combo.abilities.push_back(ability);
		combo.characters.push_back(character);
	}
	if (combo.abilities.size() < 2 || combo.abilities.size() > static_cast<std::size_t>(kMaxPartySize)) {
		reader.fail(*list, "needs 2 to " + std::to_string(kMaxPartySize) + " abilities");
	}
	std::set<std::string> tied;
	for (const std::string& character : combo.characters) {
		if (!character.empty() && !tied.insert(character).second) {
			reader.fail(*list, "character '" + character + "' is tied to more than one ability");
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
			reader.fail(*entry, "duplicate id '" + combo.id + "'");
		}
		parseComboComponents(reader, db, *entry, combo);
		reader.boolean(*entry, "ordered", combo.ordered);
		std::set<std::string> used;
		if (combo.abilities.size() >= 2 && !castable(db, combo, 0, used)) {
			reader.fail(*entry, "no set of different characters can use these abilities together");
		}
		std::vector<std::string> sorted = combo.abilities;
		std::sort(sorted.begin(), sorted.end());
		if (!sorted.empty() && !recipes.insert(sorted).second) {
			reader.fail(*entry, "another combo already uses the same abilities");
		}
		if (reader.string(*entry, "result", combo.result)) {
			const AbilityDef* result = db.ability(combo.result);
			if (!result) {
				reader.fail(*entry->find("result"), "names missing ability '" + combo.result + "'");
			} else if (result->kind != AbilityKind::Combo) {
				reader.fail(*entry->find("result"), "result '" + combo.result + "' must have kind combo");
			}
		}
		db.combos.push_back(combo);
		reader.setContext("");
	}
}

void parseEnemyOverlay(Reader& reader, const Database& db, const JsonValue& entry, EnemyOverlay& overlay) {
	reader.rejectUnknownKeys(entry, {"id", "stats_percent", "add_abilities", "ai", "status_resist"});
	if (!reader.string(entry, "id", overlay.enemy)) {
		return;
	}
	const EnemyDef* base = db.enemy(overlay.enemy);
	if (!base) {
		reader.fail(entry, "names missing enemy '" + overlay.enemy + "'");
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
				reader.fail(*entry.find("add_abilities"), "names missing ability '" + id + "'");
			} else if (ability->kind != AbilityKind::Technique && ability->kind != AbilityKind::Skill) {
				reader.fail(*entry.find("add_abilities"), "ability '" + id + "' must be a technique or a skill");
			}
			overlaid.abilities.push_back(id);
		}
	}
	if (reader.get(entry, "ai", Type::Array, false)) {
		std::vector<AiRule> rules;
		for (const JsonValue* ruleEntry : reader.objects(entry, "ai", false)) {
			AiRule rule;
			parseAiRule(reader, db, overlaid, *ruleEntry, rule);
			rules.push_back(rule);
		}
		overlay.ai = rules;
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
				reader.fail(*abilityEntry, "names missing ability '" + overlay.ability + "'");
				continue;
			}
			reader.integer(*abilityEntry, "power_percent", overlay.powerPercent, false, 1, 500);
			int value = 0;
			if (abilityEntry->find("cost")) {
				if (ability->kind != AbilityKind::Technique) {
					reader.fail(*abilityEntry->find("cost"), "only techniques have a cost");
				} else if (reader.integer(*abilityEntry, "cost", value, true, 0, 999)) {
					overlay.cost = value;
				}
			}
			if (abilityEntry->find("uses")) {
				if (ability->kind != AbilityKind::Skill) {
					reader.fail(*abilityEntry->find("uses"), "only skills have uses");
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
	std::vector<std::string> errors;
	for (std::size_t i = 0; i < kFiles.size(); ++i) {
		const std::string& name = kFiles[i];
		const auto it = files.find(name);
		if (it == files.end()) {
			errors.push_back(name + ": missing file");
			continue;
		}
		db.contentHash = fnv1a(fnv1a(db.contentHash, name), it->second);
		Result<JsonValue> json = parseJson(it->second);
		if (!json.ok()) {
			errors.push_back(name + ": " + json.error);
			continue;
		}
		if (!json.value->is(Type::Object)) {
			errors.push_back(name + ": the top-level value must be an object");
			continue;
		}
		Reader reader(name, errors);
		parsers[i](reader, *json.value, db);
	}
	if (!errors.empty()) {
		std::string message;
		for (const std::string& error : errors) {
			message += (message.empty() ? "" : "\n") + error;
		}
		return Result<Database>::failure(message);
	}
	return Result<Database>::success(std::move(db));
}

Result<Database> loadDatabaseFromDirectory(const std::string& directory) {
	std::map<std::string, std::string> files;
	for (const std::string& name : kFiles) {
		const std::string path = directory + "/" + name;
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			return Result<Database>::failure(path + ": cannot open file");
		}
		std::ostringstream text;
		text << in.rdbuf();
		files[name] = text.str();
	}
	return loadDatabaseFromTexts(files);
}

}
