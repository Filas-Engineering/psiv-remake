#include "game/data.h"

#include "game/save.h"

#include "events/compiler.h"
#include "rules/loader.h"

#include "json_read.h"

namespace game {

using rules::JsonValue;

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
	reader.only(json, {"title", "core_data", "areas", "events", "glossary", "speakers", "start", "join_levels", "prices", "item_cap", "money_cap", "tick_ms", "grace_ms", "round_ms", "autosave_slots", "manual_slots", "bosses"});
	reader.text(json, "title", c.title, true);
	reader.text(json, "core_data", c.coreData, true);
	reader.texts(json, "areas", c.areaFiles, true);
	reader.texts(json, "events", c.eventFiles, true);
	reader.text(json, "glossary", c.glossaryFile, false);
	if (const JsonValue* speakers = reader.field(json, "speakers", JsonValue::Type::Object, false)) {
		for (std::size_t i = 0; i < speakers->keys.size(); ++i) {
			if (!speakers->values[i].is(JsonValue::Type::String)) {
				reader.fail(speakers->values[i], "speaker names are text");
				continue;
			}
			c.speakers[speakers->keys[i]] = speakers->values[i].string;
		}
	}
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

bool knownThing(const rules::Database& db, const std::string& id) {
	return db.item(id) || db.equipmentPiece(id);
}

void checkReferences(const GameData& data, std::vector<std::string>& errors) {
	const rules::Database& db = data.db;
	const GameConfig& c = data.config;
	auto markerExists = [&](const std::string& area, const std::string& marker) {
		const Area* a = data.area(area);
		return a && a->markers.count(marker);
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
	}
	for (const auto& [areaId, area] : data.areas) {
		const std::string where = "area " + areaId + ": ";
		for (const Exit& e : area.exits) {
			if (!markerExists(e.toArea, e.toMarker)) {
				errors.push_back(where + "exit leads to missing marker '" + e.toArea + " " + e.toMarker + "'");
			}
		}
		for (const Hook& h : area.hooks) {
			if (data.program.find_event(h.event) < 0) {
				errors.push_back(where + "hook '" + h.id + "' runs unknown event '" + h.event + "'");
			}
		}
		for (const Shop& s : area.shops) {
			for (const std::string& id : s.stock) {
				if (!knownThing(db, id) || data.price(id) < 0) {
					errors.push_back(where + "shop '" + s.id + "' stocks '" + id + "', which is unknown or has no price");
				}
			}
		}
		for (const Chest& ch : area.chests) {
			for (const auto& [id, n] : ch.items) {
				if (!db.item(id)) {
					errors.push_back(where + "chest '" + ch.id + "' holds unknown item '" + id + "'");
				}
			}
			for (const auto& [id, n] : ch.equipment) {
				if (!db.equipmentPiece(id)) {
					errors.push_back(where + "chest '" + ch.id + "' holds unknown equipment '" + id + "'");
				}
			}
		}
		for (const Zone& z : area.zones) {
			for (const TableEntry& t : z.table) {
				if (!db.encounter(t.encounter)) {
					errors.push_back(where + "zone '" + z.id + "' lists unknown encounter '" + t.encounter + "'");
				}
			}
		}
	}
	// The events compiler does not know the game's content, so the references EVT-003 asks to check at
	// compile time are checked here.
	for (const events::Instruction& in : data.program.code) {
		const std::string where = data.program.files[in.file] + ":" + std::to_string(in.source_line) + ": ";
		switch (in.op) {
		case events::Op::battle:
			if (!db.encounter(in.battle.encounter)) {
				errors.push_back(where + "unknown encounter '" + in.battle.encounter + "'");
			}
			break;
		case events::Op::give:
		case events::Op::take:
			if (!knownThing(db, in.name)) {
				errors.push_back(where + "unknown item '" + in.name + "'");
			}
			break;
		case events::Op::party_add:
		case events::Op::party_remove:
			if (!db.character(in.name)) {
				errors.push_back(where + "unknown character '" + in.name + "'");
			}
			break;
		case events::Op::action:
			if (in.action.type == events::ActionType::warp && !markerExists(in.action.subject, in.action.target)) {
				errors.push_back(where + "warp to missing marker '" + in.action.subject + " " + in.action.target + "'");
			}
			break;
		case events::Op::say:
			if (in.dialogue.text.is_id) {
				errors.push_back(where + "string ids need a string table, which the game does not load");
			}
			break;
		default:
			break;
		}
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

	rules::Result<rules::Database> db = rules::loadDatabaseFromDirectory(join(directory, data.config.coreData));
	if (!db.ok()) {
		return Fail::failure(db.error);
	}
	data.db = std::move(*db.value);

	std::vector<std::string> errors;
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

	std::vector<events::Source> sources;
	for (const std::string& file : data.config.eventFiles) {
		const std::string path = join(directory, file);
		if (!readFile(path, text)) {
			errors.push_back("cannot read " + path);
			continue;
		}
		sources.push_back({file, text});
	}
	events::CompileResult compiled = events::compile(sources);
	for (const events::Diagnostic& d : compiled.diagnostics) {
		if (d.severity == events::Severity::error) {
			errors.push_back(events::format_diagnostic(d));
		}
	}
	if (compiled.ok()) {
		data.program = std::move(compiled.program);
	}

	if (!data.config.glossaryFile.empty()) {
		const std::string path = join(directory, data.config.glossaryFile);
		std::vector<events::Diagnostic> diagnostics;
		if (!readFile(path, text)) {
			errors.push_back("cannot read " + path);
		} else if (!events::load_glossary(data.config.glossaryFile, text, data.glossary, diagnostics)) {
			for (const events::Diagnostic& d : diagnostics) {
				errors.push_back(events::format_diagnostic(d));
			}
		}
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

}
