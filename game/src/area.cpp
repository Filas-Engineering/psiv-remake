#include "game/area.h"

#include "game/data.h"
#include "rules/text.h"

#include "json_read.h"

namespace game {

using rules::JsonValue;

const char* toString(AreaKind kind) {
	switch (kind) {
	case AreaKind::Town: return "town";
	case AreaKind::Field: return "field";
	case AreaKind::Dungeon: return "dungeon";
	}
	return "field";
}

const char* toString(EncounterMode mode) {
	return mode == EncounterMode::Visible ? "visible" : "random";
}

bool parseEncounterMode(const std::string& text, EncounterMode& out) {
	if (text == "visible") {
		out = EncounterMode::Visible;
		return true;
	}
	if (text == "random") {
		out = EncounterMode::Random;
		return true;
	}
	return false;
}

bool Zone::contains(Cell cell) const {
	return cell.x >= from.x && cell.x <= to.x && cell.y >= from.y && cell.y <= to.y;
}

int Area::width() const {
	return rows.empty() ? 0 : static_cast<int>(rows[0].size());
}

int Area::height() const {
	return static_cast<int>(rows.size());
}

bool Area::inside(Cell cell) const {
	return cell.x >= 0 && cell.y >= 0 && cell.x < width() && cell.y < height();
}

bool Area::walkable(Cell cell) const {
	if (!inside(cell) || rows[cell.y][cell.x] != '.') {
		return false;
	}
	return !hookAt(cell, HookTrigger::Interact) && !shopAt(cell) && !restAt(cell) && !chestAt(cell);
}

const Zone* Area::zoneAt(Cell cell) const {
	for (const Zone& z : zones) {
		if (z.contains(cell)) {
			return &z;
		}
	}
	return nullptr;
}

const Zone* Area::zone(const std::string& id) const {
	for (const Zone& z : zones) {
		if (z.id == id) {
			return &z;
		}
	}
	return nullptr;
}

const Exit* Area::exitAt(Cell cell) const {
	for (const Exit& e : exits) {
		if (e.at == cell) {
			return &e;
		}
	}
	return nullptr;
}

const Hook* Area::hookAt(Cell cell, HookTrigger trigger) const {
	for (const Hook& h : hooks) {
		if (h.trigger == trigger && h.at == cell) {
			return &h;
		}
	}
	return nullptr;
}

const Shop* Area::shopAt(Cell cell) const {
	for (const Shop& s : shops) {
		if (s.at == cell) {
			return &s;
		}
	}
	return nullptr;
}

const RestPoint* Area::restAt(Cell cell) const {
	for (const RestPoint& r : rests) {
		if (r.at == cell) {
			return &r;
		}
	}
	return nullptr;
}

const Chest* Area::chestAt(Cell cell) const {
	for (const Chest& c : chests) {
		if (c.at == cell) {
			return &c;
		}
	}
	return nullptr;
}

const Shop* Area::shop(const std::string& id) const {
	for (const Shop& s : shops) {
		if (s.id == id) {
			return &s;
		}
	}
	return nullptr;
}

const RestPoint* Area::rest(const std::string& id) const {
	for (const RestPoint& r : rests) {
		if (r.id == id) {
			return &r;
		}
	}
	return nullptr;
}

namespace {

const JsonValue* array(JsonReader& reader, const JsonValue& object, const char* key) {
	return reader.field(object, key, JsonValue::Type::Array, false);
}

void readZone(JsonReader& reader, const JsonValue& z, Zone& zone) {
	reader.only(z, {"id", "from", "to", "table", "visible", "random"});
	reader.text(z, "id", zone.id, true);
	reader.cellField(z, "from", zone.from, true);
	reader.cellField(z, "to", zone.to, true);
	if (const JsonValue* table = reader.field(z, "table", JsonValue::Type::Array, true)) {
		for (const JsonValue& t : table->items) {
			if (!t.is(JsonValue::Type::Object)) {
				reader.fail(t, "data.table_entry_object", {});
				continue;
			}
			reader.only(t, {"encounter", "weight"});
			TableEntry entry;
			reader.text(t, "encounter", entry.encounter, true);
			reader.integer(t, "weight", entry.weight, false, 1, 1000);
			zone.table.push_back(entry);
		}
		if (table->items.empty()) {
			reader.fail(*table, "data.zone_needs_table_entry", {zone.id});
		}
	}
	if (const JsonValue* v = reader.field(z, "visible", JsonValue::Type::Object, false)) {
		VisibleRules& r = zone.visible;
		reader.only(*v, {"spawns", "cap", "detection_radius", "leash_radius", "chase_speed_percent", "chase_ms", "wander_speed_percent", "wander_percent"});
		if (const JsonValue* spawns = reader.field(*v, "spawns", JsonValue::Type::Array, true)) {
			for (const JsonValue& s : spawns->items) {
				Cell c;
				if (reader.cell(s, c)) {
					r.spawns.push_back(c);
				}
			}
		}
		reader.integer(*v, "cap", r.cap, false, 0, 16);
		reader.integer(*v, "detection_radius", r.detectionRadius, false, 0, 32);
		reader.integer(*v, "leash_radius", r.leashRadius, false, 0, 32);
		reader.integer(*v, "chase_speed_percent", r.chaseSpeedPercent, false, 10, 400);
		reader.integer(*v, "chase_ms", r.chaseMs, false, 0, 60000);
		reader.integer(*v, "wander_speed_percent", r.wanderSpeedPercent, false, 10, 400);
		reader.integer(*v, "wander_percent", r.wanderPercent, false, 0, 100);
	}
	if (const JsonValue* v = reader.field(z, "random", JsonValue::Type::Object, false)) {
		RandomRules& r = zone.random;
		reader.only(*v, {"min_steps", "max_steps", "step_distance", "party_first_percent", "enemy_first_percent"});
		reader.integer(*v, "min_steps", r.minSteps, false, 1, 1000);
		reader.integer(*v, "max_steps", r.maxSteps, false, 1, 1000);
		reader.integer(*v, "step_distance", r.stepDistance, false, 1, 100);
		reader.integer(*v, "party_first_percent", r.partyFirstPercent, false, 0, 100);
		reader.integer(*v, "enemy_first_percent", r.enemyFirstPercent, false, 0, 100);
		if (r.maxSteps < r.minSteps) {
			reader.fail(*v, "data.max_steps_below_min_steps", {});
		}
		if (r.partyFirstPercent + r.enemyFirstPercent > 100) {
			reader.fail(*v, "data.opening_percents_over_100", {});
		}
	}
}

}

rules::Result<Area> parseArea(const std::string& file, const std::string& text) {
	rules::Result<JsonValue> parsed = rules::parseJson(text);
	if (!parsed.ok()) {
		std::vector<rules::Problem> problems = parsed.problems;
		for (rules::Problem& p : problems) {
			p.file = file;
		}
		return rules::Result<Area>::failure(problems, rules::formatProblems(problems, problemText()));
	}
	const JsonValue& json = *parsed.value;
	JsonReader reader(file);
	Area area;
	if (!json.is(JsonValue::Type::Object)) {
		reader.fail(json, "data.area_object", {});
		return rules::Result<Area>::failure(reader.problems(), reader.errors());
	}
	reader.only(json, {"id", "kind", "encounter_mode", "step_ms", "map", "markers", "exits", "hooks", "shops", "rests", "chests", "zones"});
	reader.text(json, "id", area.id, true);
	std::string kind = "field";
	reader.text(json, "kind", kind, false);
	if (kind == "town") {
		area.kind = AreaKind::Town;
	} else if (kind == "dungeon") {
		area.kind = AreaKind::Dungeon;
	} else if (kind != "field") {
		reader.fail(json, "data.bad_area_kind", {});
	}
	std::string mode = "visible";
	reader.text(json, "encounter_mode", mode, false);
	if (!parseEncounterMode(mode, area.mode)) {
		reader.fail(json, "data.bad_encounter_mode", {});
	}
	reader.integer(json, "step_ms", area.stepMs, false, 50, 5000);
	reader.texts(json, "map", area.rows, true);
	for (const std::string& row : area.rows) {
		if (row.size() != area.rows[0].size()) {
			reader.fail(json, "data.map_rows_differ", {});
			break;
		}
		if (row.find_first_not_of(".#~") != std::string::npos) {
			reader.fail(json, "data.bad_map_character", {});
			break;
		}
	}
	auto checkCell = [&](const JsonValue& at, Cell c, const std::string& what) {
		if (!area.inside(c) || area.rows[c.y][c.x] != '.') {
			reader.fail(at, "data.not_on_floor", {what});
		}
	};

	if (const JsonValue* markers = reader.field(json, "markers", JsonValue::Type::Object, true)) {
		for (std::size_t i = 0; i < markers->keys.size(); ++i) {
			Cell c;
			if (reader.cell(markers->values[i], c)) {
				area.markers[markers->keys[i]] = c;
				checkCell(markers->values[i], c, "marker '" + markers->keys[i] + "'");
			}
		}
	}
	if (const JsonValue* exits = array(reader, json, "exits")) {
		for (const JsonValue& e : exits->items) {
			reader.only(e, {"at", "to", "marker"});
			Exit exit;
			reader.cellField(e, "at", exit.at, true);
			reader.text(e, "to", exit.toArea, true);
			reader.text(e, "marker", exit.toMarker, true);
			checkCell(e, exit.at, "exit");
			area.exits.push_back(exit);
		}
	}
	if (const JsonValue* hooks = array(reader, json, "hooks")) {
		for (const JsonValue& h : hooks->items) {
			reader.only(h, {"id", "at", "trigger", "event", "require_flag", "block_flag"});
			Hook hook;
			reader.text(h, "id", hook.id, true);
			reader.text(h, "event", hook.event, true);
			reader.text(h, "require_flag", hook.requireFlag, false);
			reader.text(h, "block_flag", hook.blockFlag, false);
			std::string trigger = "interact";
			reader.text(h, "trigger", trigger, false);
			if (trigger == "step") {
				hook.trigger = HookTrigger::Step;
			} else if (trigger == "load") {
				hook.trigger = HookTrigger::Load;
			} else if (trigger == "flag") {
				hook.trigger = HookTrigger::Flag;
				if (hook.blockFlag.empty()) {
					reader.fail(h, "data.flag_hook_needs_block_flag", {hook.id});
				}
			} else if (trigger != "interact") {
				reader.fail(h, "data.bad_trigger", {});
			}
			if (hook.trigger == HookTrigger::Interact || hook.trigger == HookTrigger::Step) {
				reader.cellField(h, "at", hook.at, true);
				checkCell(h, hook.at, "hook '" + hook.id + "'");
			}
			area.hooks.push_back(hook);
		}
	}
	if (const JsonValue* shops = array(reader, json, "shops")) {
		for (const JsonValue& s : shops->items) {
			reader.only(s, {"id", "at", "stock", "sell_percent"});
			Shop shop;
			reader.text(s, "id", shop.id, true);
			if (s.is(JsonValue::Type::Object) && reader.field(s, "at", JsonValue::Type::Array, false)) {
				shop.at.emplace();
				reader.cellField(s, "at", *shop.at, true);
				checkCell(s, *shop.at, "shop '" + shop.id + "'");
			}
			reader.texts(s, "stock", shop.stock, true);
			reader.integer(s, "sell_percent", shop.sellPercent, false, 0, 100);
			area.shops.push_back(shop);
		}
	}
	if (const JsonValue* rests = array(reader, json, "rests")) {
		for (const JsonValue& r : rests->items) {
			reader.only(r, {"id", "at", "cost"});
			RestPoint rest;
			reader.text(r, "id", rest.id, true);
			if (r.is(JsonValue::Type::Object) && reader.field(r, "at", JsonValue::Type::Array, false)) {
				rest.at.emplace();
				reader.cellField(r, "at", *rest.at, true);
				checkCell(r, *rest.at, "rest point '" + rest.id + "'");
			}
			reader.integer(r, "cost", rest.cost, false, 0, 1000000);
			area.rests.push_back(rest);
		}
	}
	if (const JsonValue* chests = array(reader, json, "chests")) {
		for (const JsonValue& c : chests->items) {
			reader.only(c, {"id", "at", "flag", "items", "equipment", "money"});
			Chest chest;
			reader.text(c, "id", chest.id, true);
			reader.cellField(c, "at", chest.at, true);
			reader.text(c, "flag", chest.flag, true);
			reader.counts(c, "items", chest.items, false, 1, 99);
			reader.counts(c, "equipment", chest.equipment, false, 1, 99);
			reader.integer(c, "money", chest.money, false, 0, 1000000);
			checkCell(c, chest.at, "chest '" + chest.id + "'");
			area.chests.push_back(chest);
		}
	}
	if (const JsonValue* zones = array(reader, json, "zones")) {
		for (const JsonValue& z : zones->items) {
			Zone zone;
			readZone(reader, z, zone);
			for (Cell s : zone.visible.spawns) {
				checkCell(z, s, "spawn point of zone '" + zone.id + "'");
				if (!zone.contains(s)) {
					reader.fail(z, "data.spawn_outside_zone", {zone.id});
				}
			}
			area.zones.push_back(zone);
		}
	}
	if (!reader.ok()) {
		return rules::Result<Area>::failure(reader.problems(), reader.errors());
	}
	return rules::Result<Area>::success(std::move(area));
}

}
