#include "game/data.h"

#include "game/save.h"

#include "events/compiler.h"
#include "events/glossary.h"
#include "rules/loader.h"

#include "json_read.h"

#include <algorithm>
#include <set>

namespace game {

using rules::JsonValue;

const LanguageTables& GameData::language(const std::string& code) const {
	for (const LanguageTables& l : languages) {
		if (l.code == code) {
			return l;
		}
	}
	return languages.front();
}

const Area* GameData::area(const std::string& id) const {
	auto it = areas.find(id);
	return it == areas.end() ? nullptr : &it->second;
}

const Boss* GameData::boss(const std::string& encounter) const {
	for (const Boss& b : config.bosses) {
		if (b.encounter == encounter) {
			return &b;
		}
	}
	return nullptr;
}

int GameData::price(const std::string& id) const {
	auto it = config.prices.find(id);
	return it == config.prices.end() ? -1 : it->second;
}

const Shop* GameData::shop(const std::string& id) const {
	for (const auto& [areaId, a] : areas) {
		if (const Shop* s = a.shop(id)) {
			return s;
		}
	}
	return nullptr;
}

const RestPoint* GameData::rest(const std::string& id) const {
	for (const auto& [areaId, a] : areas) {
		if (const RestPoint* r = a.rest(id)) {
			return r;
		}
	}
	return nullptr;
}

namespace {

std::uint64_t fnv(std::uint64_t hash, const std::string& text) {
	for (unsigned char c : text) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	return hash;
}

std::string join(const std::string& directory, const std::string& file) {
	return directory.empty() ? file : directory + "/" + file;
}

void readConfig(JsonReader& reader, const JsonValue& json, GameConfig& c) {
	reader.only(json, {"title", "core_data", "areas", "events", "glossary", "languages", "journal", "actors", "presentation", "flags",
		"vars", "clear_flag", "start", "join_levels", "prices", "item_cap", "money_cap", "tick_ms", "grace_ms", "round_ms",
		"autosave_slots", "manual_slots", "bosses"});
	reader.text(json, "title", c.title, true);
	reader.text(json, "core_data", c.coreData, true);
	reader.texts(json, "areas", c.areaFiles, true);
	reader.texts(json, "events", c.eventFiles, true);
	reader.text(json, "glossary", c.glossaryFile, true);
	if (const JsonValue* list = reader.field(json, "languages", JsonValue::Type::Array, true)) {
		for (const JsonValue& l : list->items) {
			reader.only(l, {"code", "strings", "text"});
			LanguageFiles files;
			reader.text(l, "code", files.code, true);
			reader.text(l, "strings", files.strings, true);
			reader.text(l, "text", files.text, true);
			for (const LanguageFiles& other : c.languages) {
				if (other.code == files.code) {
					reader.fail(l, "language '" + files.code + "' is listed twice");
				}
			}
			c.languages.push_back(files);
		}
		if (list->items.empty()) {
			reader.fail(*list, "list at least one language");
		}
	}
	reader.text(json, "journal", c.journalFile, true);
	reader.texts(json, "actors", c.actors, false);
	if (const JsonValue* p = reader.field(json, "presentation", JsonValue::Type::Object, false)) {
		reader.only(*p, {"audio", "cameras", "layouts", "animations", "portraits"});
		reader.texts(*p, "audio", c.presentation.audio, false);
		reader.texts(*p, "cameras", c.presentation.cameras, false);
		reader.texts(*p, "layouts", c.presentation.layouts, false);
		reader.texts(*p, "animations", c.presentation.animations, false);
		reader.texts(*p, "portraits", c.presentation.portraits, false);
	}
	reader.texts(json, "flags", c.flags, false);
	reader.counts(json, "vars", c.vars, false, -999999, 999999);
	reader.text(json, "clear_flag", c.clearFlag, false);
	if (const JsonValue* start = reader.field(json, "start", JsonValue::Type::Object, true)) {
		reader.only(*start, {"area", "marker", "facing", "lineup", "level", "money", "items"});
		reader.text(*start, "area", c.startArea, true);
		reader.text(*start, "marker", c.startMarker, true);
		std::string facing = "south";
		reader.text(*start, "facing", facing, false);
		if (!parseFacing(facing, c.startFacing)) {
			reader.fail(*start, "facing must be north, east, south or west");
		}
		reader.texts(*start, "lineup", c.startLineup, true);
		reader.integer(*start, "level", c.startLevel, false, 1, 50);
		reader.integer(*start, "money", c.startMoney, false, 0, 999999);
		reader.counts(*start, "items", c.startItems, false, 1, 99);
		if (c.startLineup.empty() || c.startLineup.size() > 5) {
			reader.fail(*start, "the starting lineup needs 1 to 5 members");
		}
	}
	reader.counts(json, "join_levels", c.joinLevels, false, 1, 50);
	reader.counts(json, "prices", c.prices, false, 1, 999999);
	reader.integer(json, "item_cap", c.itemCap, false, 1, 9999);
	reader.integer(json, "money_cap", c.moneyCap, false, 1, 99999999);
	reader.integer(json, "tick_ms", c.tickMs, false, 10, 1000);
	reader.integer(json, "grace_ms", c.graceMs, false, 0, 60000);
	reader.integer(json, "round_ms", c.roundMs, false, 0, 60000);
	reader.integer(json, "autosave_slots", c.autosaveSlots, false, 1, 9);
	reader.integer(json, "manual_slots", c.manualSlots, false, 1, 99);
	if (const JsonValue* bosses = reader.field(json, "bosses", JsonValue::Type::Array, false)) {
		for (const JsonValue& b : bosses->items) {
			reader.only(b, {"encounter", "flag"});
			Boss boss;
			reader.text(b, "encounter", boss.encounter, true);
			reader.text(b, "flag", boss.flag, true);
			c.bosses.push_back(boss);
		}
	}
}

void readRange(JsonReader& reader, const JsonValue& object, FlagRange& range) {
	reader.text(object, "after", range.after, false);
	reader.text(object, "until", range.until, false);
}

void readJournal(JsonReader& reader, const JsonValue& json, Journal& j) {
	reader.only(json, {"objectives", "jobs", "encyclopedia", "party_talk"});
	if (const JsonValue* list = reader.field(json, "objectives", JsonValue::Type::Array, false)) {
		for (const JsonValue& o : list->items) {
			reader.only(o, {"id", "text", "after", "until"});
			Objective objective;
			reader.text(o, "id", objective.id, true);
			reader.text(o, "text", objective.text, true);
			readRange(reader, o, objective.range);
			j.objectives.push_back(objective);
		}
	}
	if (const JsonValue* list = reader.field(json, "jobs", JsonValue::Type::Array, false)) {
		for (const JsonValue& o : list->items) {
			reader.only(o, {"id", "title", "available", "accepted", "completed"});
			Job job;
			reader.text(o, "id", job.id, true);
			reader.text(o, "title", job.title, true);
			reader.text(o, "available", job.availableFlag, false);
			reader.text(o, "accepted", job.acceptedFlag, true);
			reader.text(o, "completed", job.completedFlag, true);
			j.jobs.push_back(job);
		}
	}
	if (const JsonValue* list = reader.field(json, "encyclopedia", JsonValue::Type::Array, false)) {
		for (const JsonValue& o : list->items) {
			reader.only(o, {"id", "title", "text", "flag"});
			EncyclopediaEntry entry;
			reader.text(o, "id", entry.id, true);
			reader.text(o, "title", entry.title, true);
			reader.text(o, "text", entry.text, true);
			reader.text(o, "flag", entry.flag, true);
			j.encyclopedia.push_back(entry);
		}
	}
	if (const JsonValue* list = reader.field(json, "party_talk", JsonValue::Type::Array, false)) {
		for (const JsonValue& o : list->items) {
			reader.only(o, {"id", "area", "after", "until", "lines"});
			TalkSet set;
			reader.text(o, "id", set.id, true);
			reader.text(o, "area", set.area, false);
			readRange(reader, o, set.range);
			if (const JsonValue* lines = reader.field(o, "lines", JsonValue::Type::Array, true)) {
				for (const JsonValue& l : lines->items) {
					reader.only(l, {"speaker", "text"});
					TalkLine line;
					reader.text(l, "speaker", line.speaker, true);
					reader.text(l, "text", line.text, true);
					set.lines.push_back(line);
				}
				if (lines->items.empty()) {
					reader.fail(*lines, "party talk set '" + set.id + "' needs at least one line");
				}
			}
			j.partyTalk.push_back(set);
		}
	}
}

bool knownThing(const rules::Database& db, const std::string& id) {
	return db.item(id) || db.equipmentPiece(id);
}

events::Registry buildRegistry(const GameData& data, std::vector<std::string>& errors) {
	events::Registry r;
	r.file = "the game data";
	const GameConfig& c = data.config;
	for (const std::string& a : c.actors) {
		r.add(events::NameKind::actor, a);
	}
	for (const auto& [id, character] : data.db.characters) {
		r.add(events::NameKind::actor, id);
		r.add(events::NameKind::member, id);
	}
	for (const auto& [id, item] : data.db.items) {
		r.add(events::NameKind::item, id);
	}
	for (const auto& [id, piece] : data.db.equipment) {
		r.add(events::NameKind::item, id);
	}
	for (const auto& [id, encounter] : data.db.encounters) {
		r.add(events::NameKind::encounter, id);
	}
	for (const auto& [id, enemy] : data.db.enemies) {
		r.add(events::NameKind::enemy, id);
	}
	for (const std::string& n : c.presentation.audio) r.add(events::NameKind::audio, n);
	for (const std::string& n : c.presentation.cameras) r.add(events::NameKind::camera, n);
	for (const std::string& n : c.presentation.layouts) r.add(events::NameKind::layout, n);
	for (const std::string& n : c.presentation.animations) r.add(events::NameKind::animation, n);
	for (const std::string& n : c.presentation.portraits) r.add(events::NameKind::portrait, n);
	std::set<std::string> flags;
	auto declareFlag = [&](const std::string& name, const std::string& where) {
		if (!flags.insert(name).second) {
			errors.push_back(where + ": flag '" + name + "' is declared twice");
			return;
		}
		r.declare_flag(name);
	};
	for (const std::string& f : c.flags) {
		declareFlag(f, "game.json");
	}
	for (const auto& [name, initial] : c.vars) {
		if (flags.count(name)) {
			errors.push_back("game.json: '" + name + "' is declared as a flag and a variable");
			continue;
		}
		r.declare_var(name, false, initial);
	}
	std::set<std::string> shops;
	std::set<std::string> rests;
	for (const auto& [areaId, a] : data.areas) {
		r.add(events::NameKind::map, areaId);
		for (const auto& [marker, cell] : a.markers) {
			r.add_marker(areaId, marker);
		}
		for (const Hook& h : a.hooks) {
			r.add_event_map(areaId, h.event);
		}
		for (const Shop& s : a.shops) {
			if (!shops.insert(s.id).second) {
				errors.push_back("area " + areaId + ": shop id '" + s.id + "' is used by another shop");
			}
			r.add(events::NameKind::shop, s.id);
		}
		for (const RestPoint& p : a.rests) {
			if (!rests.insert(p.id).second) {
				errors.push_back("area " + areaId + ": rest point id '" + p.id + "' is used by another rest point");
			}
			r.add(events::NameKind::rest_point, p.id);
		}
		for (const Chest& ch : a.chests) {
			declareFlag(ch.flag, "area " + areaId + " chest '" + ch.id + "'");
		}
	}
	return r;
}

events::GameStrings buildGameStrings(const GameData& data) {
	events::GameStrings strings;
	strings.file = "the game data";
	for (const std::string& actor : data.config.actors) {
		strings.ids.emplace("speaker." + actor, 0);
	}
	const Journal& j = data.journal;
	for (const Objective& o : j.objectives) {
		strings.ids.emplace(o.text, 0);
	}
	for (const Job& job : j.jobs) {
		strings.ids.emplace(job.title, 0);
	}
	for (const EncyclopediaEntry& e : j.encyclopedia) {
		strings.ids.emplace(e.title, 0);
		strings.ids.emplace(e.text, 0);
	}
	for (const TalkSet& t : j.partyTalk) {
		for (const TalkLine& line : t.lines) {
			strings.ids.emplace(line.text, 0);
		}
	}
	return strings;
}

void checkReferences(const GameData& data, std::vector<std::string>& errors) {
	const rules::Database& db = data.db;
	const GameConfig& c = data.config;
	auto markerExists = [&](const std::string& area, const std::string& marker) {
		const Area* a = data.area(area);
		return a && a->markers.count(marker);
	};
	auto flagDeclared = [&](const std::string& where, const std::string& flag) {
		const events::Declaration* d = data.registry.find(flag);
		if (!flag.empty() && (!d || !d->is_flag)) {
			errors.push_back(where + ": flag '" + flag + "' is not declared");
		}
	};
	const LanguageTables& source = data.languages.front();
	auto textExists = [&](const std::string& where, const std::string& key) {
		if (!source.text.count(key)) {
			errors.push_back(where + ": text '" + key + "' is not in the text table");
		}
	};
	auto stringExists = [&](const std::string& where, const std::string& id) {
		if (!source.strings.find(id)) {
			errors.push_back(where + ": string '" + id + "' is not in the dialogue table");
		}
	};
	if (!markerExists(c.startArea, c.startMarker)) {
		errors.push_back("game.json: start marker '" + c.startArea + " " + c.startMarker + "' does not exist");
	}
	for (const std::string& id : c.startLineup) {
		if (!db.character(id)) {
			errors.push_back("game.json: unknown character '" + id + "' in the starting lineup");
		}
	}
	for (const auto& [id, level] : c.joinLevels) {
		if (!db.character(id)) {
			errors.push_back("game.json: unknown character '" + id + "' in join_levels");
		}
	}
	for (const auto& [id, count] : c.startItems) {
		if (!knownThing(db, id)) {
			errors.push_back("game.json: unknown item '" + id + "' in the starting items");
		}
	}
	for (const auto& [id, price] : c.prices) {
		if (!knownThing(db, id)) {
			errors.push_back("game.json: unknown item '" + id + "' in prices");
		}
	}
	for (const Boss& b : c.bosses) {
		if (!db.encounter(b.encounter)) {
			errors.push_back("game.json: unknown boss encounter '" + b.encounter + "'");
		}
		flagDeclared("game.json boss", b.flag);
	}
	flagDeclared("game.json clear_flag", c.clearFlag);
	for (const std::string& actor : c.actors) {
		if (db.character(actor)) {
			errors.push_back("game.json: actor '" + actor + "' is a character; characters are actors already");
		}
		stringExists("game.json actor", "speaker." + actor);
	}
	for (const auto& [areaId, area] : data.areas) {
		const std::string where = "area " + areaId;
		textExists(where, "area." + areaId);
		for (const Exit& e : area.exits) {
			if (!markerExists(e.toArea, e.toMarker)) {
				errors.push_back(where + ": exit leads to missing marker '" + e.toArea + " " + e.toMarker + "'");
			}
		}
		for (const Hook& h : area.hooks) {
			if (data.program.find_event(h.event) < 0) {
				errors.push_back(where + ": hook '" + h.id + "' runs unknown event '" + h.event + "'");
			}
			flagDeclared(where + " hook '" + h.id + "'", h.requireFlag);
			flagDeclared(where + " hook '" + h.id + "'", h.blockFlag);
		}
		for (const Shop& s : area.shops) {
			for (const std::string& id : s.stock) {
				if (!knownThing(db, id) || data.price(id) < 0) {
					errors.push_back(where + ": shop '" + s.id + "' stocks '" + id + "', which is unknown or has no price");
				}
			}
		}
		for (const Chest& ch : area.chests) {
			for (const auto& [id, n] : ch.items) {
				if (!db.item(id)) {
					errors.push_back(where + ": chest '" + ch.id + "' holds unknown item '" + id + "'");
				}
			}
			for (const auto& [id, n] : ch.equipment) {
				if (!db.equipmentPiece(id)) {
					errors.push_back(where + ": chest '" + ch.id + "' holds unknown equipment '" + id + "'");
				}
			}
		}
		for (const Zone& z : area.zones) {
			for (const TableEntry& t : z.table) {
				if (!db.encounter(t.encounter)) {
					errors.push_back(where + ": zone '" + z.id + "' lists unknown encounter '" + t.encounter + "'");
				}
			}
		}
	}
	const Journal& j = data.journal;
	for (const Objective& o : j.objectives) {
		stringExists("journal objective '" + o.id + "'", o.text);
		flagDeclared("journal objective '" + o.id + "'", o.range.after);
		flagDeclared("journal objective '" + o.id + "'", o.range.until);
	}
	for (const Job& job : j.jobs) {
		stringExists("journal job '" + job.id + "'", job.title);
		flagDeclared("journal job '" + job.id + "'", job.availableFlag);
		flagDeclared("journal job '" + job.id + "'", job.acceptedFlag);
		flagDeclared("journal job '" + job.id + "'", job.completedFlag);
	}
	for (const EncyclopediaEntry& e : j.encyclopedia) {
		stringExists("journal entry '" + e.id + "'", e.title);
		stringExists("journal entry '" + e.id + "'", e.text);
		flagDeclared("journal entry '" + e.id + "'", e.flag);
	}
	for (const TalkSet& t : j.partyTalk) {
		const std::string where = "journal party talk '" + t.id + "'";
		if (!t.area.empty() && !data.area(t.area)) {
			errors.push_back(where + ": unknown area '" + t.area + "'");
		}
		flagDeclared(where, t.range.after);
		flagDeclared(where, t.range.until);
		for (const TalkLine& line : t.lines) {
			if (!db.character(line.speaker)) {
				errors.push_back(where + ": speaker '" + line.speaker + "' is not a character");
			}
			stringExists(where, line.text);
		}
	}
	for (const std::string& key : sessionTextKeys()) {
		textExists("the session", key);
	}
	for (const events::Instruction& in : data.program.code) {
		const events::BattleRequest& b = in.battle;
		if (in.op != events::Op::battle || b.end != events::BattleEndKind::enemy_defeated) {
			continue;
		}
		const rules::Encounter* e = db.encounter(b.encounter);
		const bool inGroup = e && std::any_of(e->enemies.begin(), e->enemies.end(), [&](const rules::FormationSlot& s) { return s.enemy == b.end_enemy; });
		if (!inGroup) {
			errors.push_back(data.program.files[in.file] + ":" + std::to_string(in.source_line) + ": the scripted end names enemy '" + b.end_enemy +
				"', which is not in encounter '" + b.encounter + "'");
		}
	}
}

void addDiagnostics(const std::vector<events::Diagnostic>& diagnostics, std::vector<std::string>& errors) {
	for (const events::Diagnostic& d : diagnostics) {
		errors.push_back(events::format_diagnostic(d));
	}
}

}

rules::Result<GameData> loadGameData(const std::string& directory) {
	using Fail = rules::Result<GameData>;
	GameData data;
	std::string text;
	const std::string configPath = join(directory, "game.json");
	if (!readFile(configPath, text)) {
		return Fail::failure("cannot read " + configPath);
	}
	rules::Result<JsonValue> json = rules::parseJson(text);
	if (!json.ok()) {
		return Fail::failure(configPath + ": " + json.error);
	}
	JsonReader reader(configPath);
	readConfig(reader, *json.value, data.config);
	if (!reader.ok()) {
		return Fail::failure(reader.errors());
	}
	std::uint64_t hash = fnv(1469598103934665603ULL, text);

	const std::string coreDirectory = join(directory, data.config.coreData);
	rules::Result<rules::Database> db = rules::loadDatabaseFromDirectory(coreDirectory);
	if (!db.ok()) {
		return Fail::failure(db.error);
	}
	data.db = std::move(*db.value);

	std::vector<std::string> errors;
	rules::TextTable coreTable;
	rules::Result<rules::TextTable> coreText = rules::loadTextTable(join(coreDirectory, "text_en.json"));
	if (!coreText.ok()) {
		errors.push_back(coreText.error);
	} else {
		coreTable = std::move(*coreText.value);
		for (const std::string& key : rules::messageKeys(data.db)) {
			if (!coreTable.count(key)) {
				errors.push_back(data.config.coreData + "/text_en.json: missing message key '" + key + "'");
			}
		}
	}
	std::vector<events::Diagnostic> diagnostics;
	for (const LanguageFiles& files : data.config.languages) {
		LanguageTables tables;
		tables.code = files.code;
		tables.text = coreTable;
		const std::string textPath = join(directory, files.text);
		if (!readFile(textPath, text)) {
			errors.push_back("cannot read " + textPath);
		} else {
			hash = fnv(hash, text);
			rules::Result<rules::TextTable> own = rules::parseTextTable(text);
			if (!own.ok()) {
				errors.push_back(files.text + ": " + own.error);
			} else {
				for (const auto& [key, value] : *own.value) {
					if (!tables.text.emplace(key, value).second) {
						errors.push_back(files.text + ": key '" + key + "' is already in core's text table");
					}
				}
			}
		}
		const std::string stringsPath = join(directory, files.strings);
		if (!readFile(stringsPath, text)) {
			errors.push_back("cannot read " + stringsPath);
		} else {
			hash = fnv(hash, text);
			if (events::load_string_table(files.strings, text, tables.strings, diagnostics) && tables.strings.language != files.code) {
				errors.push_back(files.strings + ": the table is '" + tables.strings.language + "', but game.json lists it for '" + files.code + "'");
			}
		}
		data.languages.push_back(std::move(tables));
	}
	for (std::size_t i = 1; i < data.languages.size(); ++i) {
		const rules::TextTable& first = data.languages.front().text;
		const rules::TextTable& other = data.languages[i].text;
		for (const auto& [key, value] : first) {
			if (!other.count(key)) {
				errors.push_back(data.config.languages[i].text + ": key '" + key + "' of the '" + data.languages.front().code + "' table is missing");
			}
		}
		for (const auto& [key, value] : other) {
			if (!first.count(key)) {
				errors.push_back(data.config.languages[i].text + ": key '" + key + "' is not in the '" + data.languages.front().code + "' table");
			}
		}
	}

	for (const std::string& file : data.config.areaFiles) {
		const std::string path = join(directory, file);
		if (!readFile(path, text)) {
			errors.push_back("cannot read " + path);
			continue;
		}
		hash = fnv(hash, text);
		rules::Result<Area> area = parseArea(file, text);
		if (!area.ok()) {
			errors.push_back(area.error);
			continue;
		}
		if (data.areas.count(area.value->id)) {
			errors.push_back(file + ": duplicate area id '" + area.value->id + "'");
			continue;
		}
		data.areas[area.value->id] = std::move(*area.value);
	}

	const std::string journalPath = join(directory, data.config.journalFile);
	if (!readFile(journalPath, text)) {
		errors.push_back("cannot read " + journalPath);
	} else {
		hash = fnv(hash, text);
		rules::Result<JsonValue> parsed = rules::parseJson(text);
		JsonReader journal(data.config.journalFile);
		if (!parsed.ok()) {
			errors.push_back(data.config.journalFile + ": " + parsed.error);
		} else {
			readJournal(journal, *parsed.value, data.journal);
			if (!journal.ok()) {
				errors.push_back(journal.errors());
			}
		}
	}

	const std::string glossaryPath = join(directory, data.config.glossaryFile);
	if (!readFile(glossaryPath, text)) {
		errors.push_back("cannot read " + glossaryPath);
	} else {
		events::load_glossary(data.config.glossaryFile, text, data.glossary, diagnostics);
	}
	addDiagnostics(diagnostics, errors);
	if (!errors.empty()) {
		std::string all;
		for (const std::string& e : errors) {
			all += (all.empty() ? "" : "\n") + e;
		}
		return Fail::failure(all);
	}

	data.registry = buildRegistry(data, errors);
	data.gameStrings = buildGameStrings(data);
	std::vector<events::Source> sources;
	for (const std::string& file : data.config.eventFiles) {
		const std::string path = join(directory, file);
		if (!readFile(path, text)) {
			errors.push_back("cannot read " + path);
			continue;
		}
		sources.push_back({file, text});
	}
	events::CompileOptions options;
	options.registry = &data.registry;
	events::CompileResult compiled = events::compile(sources, options);
	// The strict check of FORMAT.md: warnings fail too.
	addDiagnostics(compiled.diagnostics, errors);
	if (compiled.ok()) {
		data.program = std::move(compiled.program);
		addDiagnostics(events::check_inline_text(data.program), errors);
		std::vector<events::StringTable> tables;
		for (const LanguageTables& l : data.languages) {
			tables.push_back(l.strings);
		}
		addDiagnostics(events::check_strings(data.program, tables, data.gameStrings), errors);
		addDiagnostics(events::check_names(data.program, data.glossary, tables), errors);
	}

	if (errors.empty()) {
		checkReferences(data, errors);
	}
	if (!errors.empty()) {
		std::string all;
		for (const std::string& e : errors) {
			all += (all.empty() ? "" : "\n") + e;
		}
		return Fail::failure(all);
	}
	hash ^= data.db.contentHash;
	hash = hash * 1099511628211ULL ^ data.program.fingerprint;
	data.contentHash = hash;
	return Fail::success(std::move(data));
}

std::string registryText(const events::Registry& registry) {
	std::string out = "# Built from the game data by game_cli --registry.\n";
	const std::pair<events::NameKind, const char*> kinds[] = {
		{events::NameKind::actor, "actor"}, {events::NameKind::marker, "marker"}, {events::NameKind::camera, "camera"},
		{events::NameKind::layout, "layout"}, {events::NameKind::audio, "audio"}, {events::NameKind::animation, "animation"},
		{events::NameKind::portrait, "portrait"}, {events::NameKind::item, "item"}, {events::NameKind::member, "member"},
		{events::NameKind::encounter, "encounter"}, {events::NameKind::map, "map"}, {events::NameKind::shop, "shop"},
		{events::NameKind::rest_point, "rest"}, {events::NameKind::enemy, "enemy"},
	};
	for (const auto& [kind, word] : kinds) {
		auto it = registry.names.find(kind);
		if (it == registry.names.end() || it->second.empty()) {
			continue;
		}
		std::string names;
		for (const std::string& name : it->second) {
			// Markers with a map are written on map_markers lines below.
			if (kind != events::NameKind::marker || !registry.marker_maps.count(name)) {
				names += " " + name;
			}
		}
		if (!names.empty()) {
			out += word + names + "\n";
		}
	}
	std::map<std::string, std::vector<std::string>> byMap;
	for (const auto& [marker, maps] : registry.marker_maps) {
		for (const std::string& map : maps) {
			byMap[map].push_back(marker);
		}
	}
	for (const auto& [map, markers] : byMap) {
		out += "map_markers " + map;
		for (const std::string& marker : markers) {
			out += " " + marker;
		}
		out += "\n";
	}
	byMap.clear();
	for (const auto& [event, maps] : registry.event_maps) {
		for (const std::string& map : maps) {
			byMap[map].push_back(event);
		}
	}
	for (const auto& [map, list] : byMap) {
		out += "event_map " + map;
		for (const std::string& event : list) {
			out += " " + event;
		}
		out += "\n";
	}
	for (const events::Declaration& d : registry.declarations) {
		out += std::string(d.is_flag ? "flag " : "var ") + d.name;
		if (d.initial != 0) {
			out += d.is_flag ? " default true" : " default " + std::to_string(d.initial);
		}
		out += "\n";
	}
	return out;
}

std::string gameStringsText(const events::GameStrings& strings) {
	std::string out = "# String IDs the game shows itself, built from the game data by game_cli --used.\n";
	for (const auto& [id, line] : strings.ids) {
		out += id + "\n";
	}
	return out;
}

}
