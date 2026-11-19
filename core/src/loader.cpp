#include "rules/loader.h"

#include "loader_reader.h"
#include "rules/battle.h"
#include "rules/names.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace rules {

namespace {

using loader::Reader;
using Type = JsonValue::Type;

const char* const kFiles[] = {"statuses.json", "abilities.json", "items.json", "party.json", "enemies.json", "combos.json"};

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
	reader.rejectUnknownKeys(*stats, {"hp", "tp", "strength", "mental", "agility", "dexterity", "defense", "level"});
	reader.integer(*stats, "hp", out.maxHp, true, minimum, 99999);
	reader.integer(*stats, "tp", out.maxTp, true, 0, 9999);
	reader.integer(*stats, "strength", out.strength, true, minimum, 999);
	reader.integer(*stats, "mental", out.mental, true, minimum, 999);
	reader.integer(*stats, "agility", out.agility, true, minimum, 999);
	reader.integer(*stats, "dexterity", out.dexterity, true, minimum, 999);
	reader.integer(*stats, "defense", out.defense, true, 0, 999);
	if (!growth) {
		reader.integer(*stats, "level", out.level, false, 1, 99);
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
	if (effectsRevive(effects) && targeting != Targeting::OneAlly && targeting != Targeting::AllAllies) {
		reader.fail(at, "revive effects need target one_ally or all_allies");
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
		reader.rejectUnknownKeys(*entry, {"id", "name", "skips_turn", "blocks_techniques", "wakes_on_damage", "tick_damage_percent", "duration", "recover_chance"});
		reader.string(*entry, "name", status.name);
		reader.boolean(*entry, "skips_turn", status.skipsTurn);
		reader.boolean(*entry, "blocks_techniques", status.blocksTechniques);
		reader.boolean(*entry, "wakes_on_damage", status.wakesOnDamage);
		reader.integer(*entry, "tick_damage_percent", status.tickDamagePercent, false, 0, 100);
		reader.integer(*entry, "duration", status.duration, false, 0, 99);
		reader.integer(*entry, "recover_chance", status.recoverChance, false, 0, 100);
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
		reader.rejectUnknownKeys(*entry, {"id", "name", "kind", "cost", "uses", "target", "effects"});
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
		reader.rejectUnknownKeys(*entry, {"id", "name", "target", "effects"});
		reader.string(*entry, "name", item.name);
		readTargeting(reader, *entry, item.targeting);
		parseEffects(reader, db, *entry, item.effects);
		checkReviveTargeting(reader, *entry, item.targeting, item.effects);
		insertUnique(db.items, item.id, item, reader, *entry);
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

void parseCharacters(Reader& reader, const JsonValue& root, Database& db) {
	for (const JsonValue* entry : reader.objects(root, "characters", true)) {
		CharacterDef character;
		if (!reader.string(*entry, "id", character.id)) {
			continue;
		}
		reader.setContext("character '" + character.id + "'");
		reader.rejectUnknownKeys(*entry, {"id", "name", "base", "growth", "basic_attack", "abilities", "affinities"});
		reader.string(*entry, "name", character.name);
		parseStats(reader, *entry, "base", character.base, false);
		parseStats(reader, *entry, "growth", character.growth, true);
		readBasicAttack(reader, db, *entry, character.basicAttack);
		readAbilityList(reader, db, *entry, character.abilities, false);
		parseAffinities(reader, *entry, character.affinities);
		insertUnique(db.characters, character.id, character, reader, *entry);
		reader.setContext("");
	}
}

void parseMacroStep(Reader& reader, const Database& db, const JsonValue& entry, MacroStep& step) {
	reader.rejectUnknownKeys(entry, {"member", "command", "id", "target", "target_member"});
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
	if (step.kind == CommandKind::Ability && reader.string(entry, "id", step.id)) {
		const AbilityDef* ability = db.ability(step.id);
		if (!ability) {
			reader.fail(*entry.find("id"), "names missing ability '" + step.id + "'");
		} else if (member && std::find(member->abilities.begin(), member->abilities.end(), step.id) == member->abilities.end()) {
			reader.fail(*entry.find("id"), "character '" + step.member + "' does not know ability '" + step.id + "'");
		}
	} else if (step.kind == CommandKind::Item && reader.string(entry, "id", step.id) && !db.item(step.id)) {
		reader.fail(*entry.find("id"), "names missing item '" + step.id + "'");
	}
	reader.integer(entry, "target", step.enemySlot, false, 0, kMaxEnemies - 1);
	if (reader.string(entry, "target_member", step.allyMember, false) && !db.character(step.allyMember)) {
		reader.fail(*entry.find("target_member"), "names missing character '" + step.allyMember + "'");
	}
}

void parseParty(Reader& reader, const JsonValue& root, Database& db) {
	reader.rejectUnknownKeys(root, {"characters", "lineup", "inventory", "macros"});
	parseCharacters(reader, root, db);

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

void parseAiRule(Reader& reader, const Database& db, const EnemyDef& enemy, const JsonValue& entry, AiRule& rule) {
	reader.rejectUnknownKeys(entry, {"ability", "weight", "when"});
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
		reader.rejectUnknownKeys(*entry, {"id", "name", "stats", "basic_attack", "abilities", "affinities", "ai", "experience", "currency"});
		reader.string(*entry, "name", enemy.name);
		parseStats(reader, *entry, "stats", enemy.stats, false);
		readBasicAttack(reader, db, *entry, enemy.basicAttack);
		readAbilityList(reader, db, *entry, enemy.abilities, false);
		parseAffinities(reader, *entry, enemy.affinities);
		for (const JsonValue* ruleEntry : reader.objects(*entry, "ai", false)) {
			AiRule rule;
			parseAiRule(reader, db, enemy, *ruleEntry, rule);
			enemy.ai.push_back(rule);
		}
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
		reader.rejectUnknownKeys(*entry, {"id", "enemies"});
		encounter.enemies = reader.strings(*entry, "enemies", true);
		const JsonValue* list = entry->find("enemies");
		if (list && (encounter.enemies.empty() || encounter.enemies.size() > static_cast<std::size_t>(kMaxEnemies))) {
			reader.fail(*list, "needs 1 to " + std::to_string(kMaxEnemies) + " enemies");
		}
		for (const std::string& id : encounter.enemies) {
			if (!db.enemy(id)) {
				reader.fail(*list, "names missing enemy '" + id + "'");
			}
		}
		insertUnique(db.encounters, encounter.id, encounter, reader, *entry);
		reader.setContext("");
	}
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
		reader.rejectUnknownKeys(*entry, {"id", "abilities", "result"});
		if (!ids.insert(combo.id).second) {
			reader.fail(*entry, "duplicate id '" + combo.id + "'");
		}
		combo.abilities = reader.strings(*entry, "abilities", true);
		const JsonValue* list = entry->find("abilities");
		if (list && (combo.abilities.size() < 2 || combo.abilities.size() > static_cast<std::size_t>(kMaxPartySize))) {
			reader.fail(*list, "needs 2 to " + std::to_string(kMaxPartySize) + " abilities");
		}
		for (const std::string& id : combo.abilities) {
			const AbilityDef* ability = db.ability(id);
			if (!ability) {
				reader.fail(*list, "names missing ability '" + id + "'");
			} else if (ability->kind != AbilityKind::Technique && ability->kind != AbilityKind::Skill) {
				reader.fail(*list, "ability '" + id + "' must be a technique or a skill");
			}
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

using FileParser = void (*)(Reader&, const JsonValue&, Database&);

}

Result<Database> loadDatabaseFromTexts(const std::map<std::string, std::string>& files) {
	// Order matters: each file may reference the ones before it.
	const FileParser parsers[] = {parseStatuses, parseAbilities, parseItems, parseParty, parseEnemies, parseCombos};
	Database db;
	std::vector<std::string> errors;
	for (std::size_t i = 0; i < std::size(kFiles); ++i) {
		const std::string name = kFiles[i];
		const auto it = files.find(name);
		if (it == files.end()) {
			errors.push_back(name + ": missing file");
			continue;
		}
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
	for (const char* name : kFiles) {
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
