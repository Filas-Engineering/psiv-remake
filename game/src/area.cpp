#include "game/area.h"

#include "game/capabilities.h"
#include "game/data.h"
#include "rules/text.h"

#include "json_read.h"

#include <algorithm>
#include <cmath>

namespace game {

using rules::JsonValue;

const char* toString(AreaKind kind) {
	switch (kind) {
	case AreaKind::Town: return "town";
	case AreaKind::Field: return "field";
	case AreaKind::Dungeon: return "dungeon";
	case AreaKind::Vessel: return "vessel";
	}
	return "field";
}

const char* toString(ShopKind kind) {
	for (const rules::DataEnum& e : gameDataEnums()) {
		if (e.name == "shop_kind") {
			return e.values[static_cast<std::size_t>(kind)].c_str();
		}
	}
	return "general";
}

bool parseShopKind(const std::string& text, ShopKind& out) {
	const int index = gameEnumIndex("shop_kind", text);
	if (index >= 0) {
		out = static_cast<ShopKind>(index);
	}
	return index >= 0;
}

const char* toString(EncounterMode mode) {
	return mode == EncounterMode::Visible ? "visible" : "random";
}

bool parseEncounterMode(const std::string& text, EncounterMode& out) {
	const int index = gameEnumIndex("encounter_mode", text);
	if (index >= 0) {
		out = static_cast<EncounterMode>(index);
	}
	return index >= 0;
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
	if (!inside(cell) || rows[cell.y][cell.x] != '.' || closed.count(cell)) {
		return false;
	}
	return (!hookAt(cell, HookTrigger::Interact) || passable.count(cell)) && !shopAt(cell) && !restAt(cell) && !chestAt(cell) && !portAt(cell) && !helmAt(cell);
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

bool Hook::covers(Cell cell) const {
	return at == cell || std::find(cells.begin(), cells.end(), cell) != cells.end();
}

const Hook* Area::hookAt(Cell cell, HookTrigger trigger) const {
	for (const Hook& h : hooks) {
		if (h.trigger == trigger && h.covers(cell) && !h.tile) {
			return &h;
		}
	}
	return nullptr;
}

const Hook* Area::stepHookAt(Cell cell) const {
	if (const Hook* at = hookAt(cell, HookTrigger::Step)) {
		return at;
	}
	for (const Hook& h : hooks) {
		if (h.trigger == HookTrigger::Step && h.tile && inside(cell) && rows[cell.y][cell.x] == h.tile) {
			return &h;
		}
	}
	return nullptr;
}

bool portOpen(const Port& port, const World& world) {
	return (port.flag.empty() || world.flag(port.flag)) && (port.blockFlag.empty() || !world.flag(port.blockFlag));
}

bool exitOpen(const Exit& exit, const World& world) {
	return (exit.requireFlag.empty() || world.flag(exit.requireFlag)) && (exit.blockFlag.empty() || !world.flag(exit.blockFlag));
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

const Port* Area::portAt(Cell cell) const {
	for (const Port& p : ports) {
		if (p.at == cell) {
			return &p;
		}
	}
	return nullptr;
}

const Helm* Area::helmAt(Cell cell) const {
	for (const Helm& h : helms) {
		if (h.at == cell) {
			return &h;
		}
	}
	return nullptr;
}

bool Area::gangwayAt(Cell cell) const {
	return std::find(gangway.begin(), gangway.end(), cell) != gangway.end();
}

const Port* Area::port(const std::string& id) const {
	for (const Port& p : ports) {
		if (p.id == id) {
			return &p;
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

// A `step_ms`: whole milliseconds, or with a fraction (R108), kept to the microsecond in `us` and rounded in `ms`.
void stepTime(JsonReader& reader, const JsonValue& object, int& ms, std::int64_t& us, int min, int max) {
	double value = ms;
	reader.number(object, "step_ms", value, false, min, max);
	us = std::llround(value * 1000.0);
	ms = static_cast<int>(std::llround(value));
}

const JsonValue* array(JsonReader& reader, const JsonValue& object, const char* key) {
	return reader.field(object, key, JsonValue::Type::Array, false);
}

void readZone(JsonReader& reader, const JsonValue& z, Zone& zone) {
	reader.only(z, gameKeys("area", "zones[]"));
	zone.line = z.line;
	reader.text(z, "id", zone.id, true);
	reader.cellField(z, "from", zone.from, true);
	reader.cellField(z, "to", zone.to, true);
	if (const JsonValue* table = reader.field(z, "table", JsonValue::Type::Array, true)) {
		for (const JsonValue& t : table->items) {
			if (!t.is(JsonValue::Type::Object)) {
				reader.fail(t, "data.table_entry_object", {});
				continue;
			}
			reader.only(t, gameKeys("area", "zones[].table[]"));
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
		reader.only(*v, gameKeys("area", "zones[].visible"));
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
		reader.only(*v, gameKeys("area", "zones[].random"));
		reader.integer(*v, "grace_steps", r.graceSteps, false, 0, 1000);
		reader.integer(*v, "step_chance", r.stepChance, false, 1, 10000);
		reader.integer(*v, "vehicle_step_chance", r.vehicleStepChance, false, 1, 10000);
		reader.boolean(*v, "safe_near_exits", r.safeNearExits, false);
		reader.boolean(*v, "grace_after_battle", r.graceAfterBattle, false);
		if (r.graceAfterBattle && r.graceSteps == 0) {
			reader.fail(*v, "data.grace_after_battle_needs_grace", {});
		}
		reader.integer(*v, "min_steps", r.minSteps, false, 1, 1000);
		reader.integer(*v, "max_steps", r.maxSteps, false, 0, 1000);
		reader.integer(*v, "step_distance", r.stepDistance, false, 1, 100);
		if (r.maxSteps == 0 && r.stepChance == 0) {
			reader.fail(*v, "data.max_steps_zero_needs_chance", {});
		} else if (r.maxSteps != 0 && r.maxSteps < r.minSteps) {
			reader.fail(*v, "data.max_steps_below_min_steps", {});
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
	area.file = file;
	if (!json.is(JsonValue::Type::Object)) {
		reader.fail(json, "data.area_object", {});
		return rules::Result<Area>::failure(reader.problems(), reader.errors());
	}
	reader.only(json, gameKeys("area", ""));
	reader.text(json, "id", area.id, true);
	reader.text(json, "music", area.music, false);
	reader.text(json, "region", area.region, false);
	reader.text(json, "arrival", area.arrival, false);
	std::string kind = "field";
	reader.text(json, "kind", kind, false);
	if (const int index = gameEnumIndex("area_kind", kind); index >= 0) {
		area.kind = static_cast<AreaKind>(index);
	} else {
		reader.fail(json, "data.bad_area_kind", {});
	}
	std::string mode = "visible";
	reader.text(json, "encounter_mode", mode, false);
	if (!parseEncounterMode(mode, area.mode)) {
		reader.fail(json, "data.bad_encounter_mode", {});
	}
	stepTime(reader, json, area.stepMs, area.stepUs, 50, 5000);
	reader.boolean(json, "arrive_notice", area.arriveNotice, false);
	reader.texts(json, "map", area.rows, true);
	for (const std::string& row : area.rows) {
		if (row.size() != area.rows[0].size()) {
			reader.fail(json, "data.map_rows_differ", {});
			break;
		}
		if (row.find_first_not_of(kMapTiles) != std::string::npos) {
			reader.fail(json, "data.bad_map_character", {});
			break;
		}
	}
	for (std::size_t y = 0; y < area.rows.size(); ++y) {
		for (std::size_t x = 0; x < area.rows[y].size(); ++x) {
			if (area.rows[y][x] == '=') {
				area.counters.insert({static_cast<int>(x), static_cast<int>(y)});
			}
		}
	}
	if (const JsonValue* counters = reader.field(json, "counters", JsonValue::Type::Array, false)) {
		for (const JsonValue& at : counters->items) {
			Cell c;
			if (!reader.cell(at, c)) {
				continue;
			}
			// A counter is never walked on: a floor cell cannot be one.
			if (!area.inside(c) || area.rows[c.y][c.x] == '.') {
				reader.fail(at, "data.counter_not_solid", {std::to_string(c.x), std::to_string(c.y)});
				continue;
			}
			area.counters.insert(c);
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
				area.markerLines[markers->keys[i]] = markers->values[i].line;
				checkCell(markers->values[i], c, "marker '" + markers->keys[i] + "'");
			}
		}
	}
	if (const JsonValue* exits = array(reader, json, "exits")) {
		for (const JsonValue& e : exits->items) {
			reader.only(e, gameKeys("area", "exits[]"));
			Exit exit;
			exit.line = e.line;
			reader.text(e, "sound", exit.sound, false);
			reader.text(e, "require_flag", exit.requireFlag, false);
			reader.text(e, "block_flag", exit.blockFlag, false);
			std::string closedTile;
			reader.text(e, "closed_tile", closedTile, false);
			if (closedTile == "normal") {
				exit.closedTile = 0;
			} else if (closedTile.size() == 1 && std::string(kMapTiles).find(closedTile[0]) != std::string::npos) {
				exit.closedTile = closedTile[0];
			} else if (!closedTile.empty() && closedTile != "wall") {
				reader.fail(e, "data.bad_closed_tile", {closedTile});
			}
			if (!closedTile.empty() && exit.requireFlag.empty() && exit.blockFlag.empty()) {
				reader.fail(e, "data.closed_tile_never_closed", {});
			}
			reader.cellField(e, "at", exit.at, true);
			reader.text(e, "to", exit.toArea, true);
			reader.text(e, "marker", exit.toMarker, true);
			reader.boolean(e, "fall", exit.fall, false);
			checkCell(e, exit.at, "exit");
			area.exits.push_back(exit);
		}
	}
	if (const JsonValue* hooks = array(reader, json, "hooks")) {
		for (const JsonValue& h : hooks->items) {
			reader.only(h, gameKeys("area", "hooks[]"));
			Hook hook;
			hook.line = h.line;
			reader.boolean(h, "once", hook.once, false);
			std::string tile;
			reader.text(h, "tile", tile, false);
			if (!tile.empty() && (tile.size() != 1 || std::string(kMapTiles).find(tile[0]) == std::string::npos || tile[0] == '#')) {
				reader.fail(h, "data.bad_hook_tile", {tile});
			} else if (tile.size() == 1) {
				hook.tile = tile[0];
			}
			std::string solid = "always";
			reader.text(h, "solid", solid, false);
			const int solidIndex = gameEnumIndex("hook_solid", solid);
			if (solidIndex < 0) {
				reader.fail(h, "data.bad_hook_solid", {solid});
			}
			hook.solidWhileActive = solidIndex == 1;
			reader.text(h, "id", hook.id, true);
			reader.text(h, "event", hook.event, true);
			reader.text(h, "require_flag", hook.requireFlag, false);
			reader.text(h, "block_flag", hook.blockFlag, false);
			std::string trigger = "interact";
			reader.text(h, "trigger", trigger, false);
			const int triggerIndex = gameEnumIndex("hook_trigger", trigger);
			if (triggerIndex >= 0) {
				hook.trigger = static_cast<HookTrigger>(triggerIndex);
			} else {
				reader.fail(h, "data.bad_trigger", {});
			}
			if (hook.trigger == HookTrigger::Flag) {
				if (hook.blockFlag.empty() && !hook.once) {
					reader.fail(h, "data.flag_hook_needs_block_flag", {hook.id});
				}
			} else if (hook.trigger == HookTrigger::Touch) {
				reader.text(h, "actor", hook.actor, true);
			}
			if (hook.trigger != HookTrigger::Touch && h.is(JsonValue::Type::Object) && h.find("actor")) {
				reader.fail(h, "data.actor_touch_hooks_only", {hook.id});
			}
			if (hook.once && hook.trigger != HookTrigger::Flag) {
				reader.fail(h, "data.once_flag_hooks_only", {hook.id});
			}
			if (hook.tile && hook.trigger != HookTrigger::Step) {
				reader.fail(h, "data.tile_step_hooks_only", {hook.id});
			}
			if (hook.solidWhileActive && hook.trigger != HookTrigger::Interact) {
				reader.fail(h, "data.solid_interact_hooks_only", {hook.id});
			}
			reader.boolean(h, "across", hook.across, false);
			if ((hook.across || (h.is(JsonValue::Type::Object) && h.find("cells"))) && hook.trigger != HookTrigger::Interact) {
				reader.fail(h, "data.across_cells_interact_hooks_only", {hook.id});
			}
			if (h.is(JsonValue::Type::Object) && h.find("actor_at")) {
				Cell cell;
				reader.cellField(h, "actor_at", cell, true);
				hook.actorAt = cell;
				if (hook.trigger != HookTrigger::Interact) {
					reader.fail(h, "data.actor_at_interact_hooks_only", {hook.id});
				} else if (!area.inside(cell)) {
					reader.fail(h, "data.not_on_map", {"actor cell of hook '" + hook.id + "'"});
				}
			}
			if (hook.trigger == HookTrigger::Interact) {
				// A sign on a wall or a ladder up a cliff is used from the floor beside it (FLD-030), so an interact hook
				// may stand on any cell; the data check makes sure the party can reach one beside it.
				reader.cellField(h, "at", hook.at, true);
				if (!area.inside(hook.at)) {
					reader.fail(h, "data.not_on_map", {"hook '" + hook.id + "'"});
				}
				if (hook.actorAt && *hook.actorAt == hook.at) {
					reader.fail(h, "data.actor_at_on_hook", {hook.id});
				}
				if (const JsonValue* cells = reader.field(h, "cells", JsonValue::Type::Array, false)) {
					for (const JsonValue& at : cells->items) {
						Cell cell;
						if (!reader.cell(at, cell)) {
							continue;
						}
						if (!area.inside(cell)) {
							reader.fail(at, "data.not_on_map", {"cell of hook '" + hook.id + "'"});
						} else if (hook.covers(cell) || (hook.actorAt && *hook.actorAt == cell)) {
							reader.fail(at, "data.hook_cell_twice", {hook.id});
						}
						hook.cells.push_back(cell);
					}
				}
			} else if (hook.trigger == HookTrigger::Step && hook.tile) {
				if (h.is(JsonValue::Type::Object) && h.find("at")) {
					reader.fail(h, "data.tile_hook_has_no_cell", {hook.id});
				}
			} else if (hook.trigger == HookTrigger::Step) {
				reader.cellField(h, "at", hook.at, true);
				checkCell(h, hook.at, "hook '" + hook.id + "'");
			}
			area.hooks.push_back(hook);
		}
	}
	if (const JsonValue* shops = array(reader, json, "shops")) {
		for (const JsonValue& s : shops->items) {
			reader.only(s, gameKeys("area", "shops[]"));
			Shop shop;
			shop.line = s.line;
			reader.text(s, "id", shop.id, true);
			if (s.is(JsonValue::Type::Object) && reader.field(s, "at", JsonValue::Type::Array, false)) {
				shop.at.emplace();
				reader.cellField(s, "at", *shop.at, true);
				checkCell(s, *shop.at, "shop '" + shop.id + "'");
			}
			std::string shopKind = "general";
			reader.text(s, "kind", shopKind, false);
			if (!parseShopKind(shopKind, shop.kind)) {
				reader.fail(s, "data.bad_shop_kind", {shopKind});
			}
			if (const JsonValue* stock = reader.field(s, "stock", JsonValue::Type::Array, true)) {
				for (const JsonValue& item : stock->items) {
					StockEntry entry;
					if (item.is(JsonValue::Type::String) && !item.string.empty()) {
						entry.id = item.string;
					} else if (item.is(JsonValue::Type::Object)) {
						reader.only(item, gameKeys("area", "shops[].stock[]"));
						reader.text(item, "id", entry.id, true);
						reader.text(item, "require_flag", entry.requireFlag, false);
						reader.text(item, "block_flag", entry.blockFlag, false);
					} else {
						reader.fail(item, "data.stock_entry_shape", {});
						continue;
					}
					shop.stock.push_back(entry);
				}
			}
			reader.integer(s, "sell_percent", shop.sellPercent, false, 0, 100);
			reader.text(s, "require_flag", shop.requireFlag, false);
			reader.text(s, "block_flag", shop.blockFlag, false);
			reader.text(s, "keeper", shop.keeper, false);
			reader.text(s, "voice", shop.voice, false);
			area.shops.push_back(shop);
		}
	}
	if (const JsonValue* rests = array(reader, json, "rests")) {
		for (const JsonValue& r : rests->items) {
			reader.only(r, gameKeys("area", "rests[]"));
			RestPoint rest;
			rest.line = r.line;
			reader.text(r, "id", rest.id, true);
			if (r.is(JsonValue::Type::Object) && reader.field(r, "at", JsonValue::Type::Array, false)) {
				rest.at.emplace();
				reader.cellField(r, "at", *rest.at, true);
				checkCell(r, *rest.at, "rest point '" + rest.id + "'");
			}
			reader.integer(r, "cost", rest.cost, false, 0, 1000000);
			reader.text(r, "keeper", rest.keeper, false);
			reader.boolean(r, "per_member", rest.perMember, false);
			reader.text(r, "voice", rest.voice, false);
			area.rests.push_back(rest);
		}
	}
	if (const JsonValue* chests = array(reader, json, "chests")) {
		for (const JsonValue& c : chests->items) {
			reader.only(c, gameKeys("area", "chests[]"));
			Chest chest;
			chest.line = c.line;
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
	if (!area.arrival.empty() && !area.markers.count(area.arrival)) {
		reader.fail(json, "data.arrival_marker_missing", {area.arrival});
	}
	if (const JsonValue* way = reader.field(json, "dungeon_exit", JsonValue::Type::Object, false)) {
		reader.only(*way, gameKeys("area", "dungeon_exit"));
		DungeonExitTarget target;
		target.line = way->line;
		reader.text(*way, "area", target.area, true);
		reader.text(*way, "marker", target.marker, false);
		reader.cellField(*way, "at", target.at, false);
		if (target.marker.empty() == !way->find("at")) {
			reader.fail(*way, "data.dungeon_exit_needs_place", {});
		}
		std::string facing;
		reader.text(*way, "facing", facing, false);
		Facing parsed = Facing::South;
		if (!facing.empty() && !parseFacing(facing, parsed)) {
			reader.fail(*way, "data.bad_facing", {});
		} else if (!facing.empty()) {
			target.facing = parsed;
		}
		if (area.kind != AreaKind::Dungeon) {
			reader.fail(*way, "data.dungeon_exit_not_dungeon", {});
		}
		area.dungeonExit = target;
	}
	if (const JsonValue* ports = array(reader, json, "ports")) {
		for (const JsonValue& p : ports->items) {
			reader.only(p, gameKeys("area", "ports[]"));
			Port port;
			port.line = p.line;
			reader.text(p, "id", port.id, true);
			reader.cellField(p, "at", port.at, true);
			reader.text(p, "marker", port.marker, true);
			reader.text(p, "flag", port.flag, false);
			reader.text(p, "ship", port.ship, false);
			reader.text(p, "block_flag", port.blockFlag, false);
			if (!area.inside(port.at)) {
				reader.fail(p, "data.not_on_map", {"port '" + port.id + "'"});
			}
			if (!port.marker.empty() && !area.markers.count(port.marker)) {
				reader.fail(p, "data.port_marker_missing", {port.id, port.marker});
			}
			area.ports.push_back(port);
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
	if (const JsonValue* helms = array(reader, json, "helms")) {
		for (const JsonValue& h : helms->items) {
			reader.only(h, gameKeys("area", "helms[]"));
			Helm helm;
			helm.line = h.line;
			reader.text(h, "id", helm.id, true);
			reader.cellField(h, "at", helm.at, true);
			if (!area.inside(helm.at)) {
				reader.fail(h, "data.not_on_map", {"helm '" + helm.id + "'"});
			}
			area.helms.push_back(helm);
		}
	}
	if (const JsonValue* gangway = array(reader, json, "gangway")) {
		for (const JsonValue& g : gangway->items) {
			Cell c;
			if (reader.cell(g, c)) {
				checkCell(g, c, "gangway");
				area.gangway.push_back(c);
			}
		}
	}
	if ((!area.helms.empty() || !area.gangway.empty()) && area.kind != AreaKind::Vessel) {
		reader.fail(json, "data.helm_outside_vessel", {});
	}
	if (const JsonValue* changes = array(reader, json, "changes")) {
		for (const JsonValue& c : changes->items) {
			reader.only(c, gameKeys("area", "changes[]"));
			CellChange change;
			change.line = c.line;
			reader.text(c, "flag", change.flag, true);
			std::string tile = ".";
			reader.text(c, "tile", tile, true);
			if (tile.size() != 1 || std::string(kMapTiles).find(tile[0]) == std::string::npos) {
				reader.fail(c, "data.bad_map_character", {});
			} else {
				change.tile = tile[0];
			}
			if (const JsonValue* cells = reader.field(c, "cells", JsonValue::Type::Array, true)) {
				for (const JsonValue& at : cells->items) {
					Cell cell;
					if (reader.cell(at, cell)) {
						if (!area.inside(cell)) {
							reader.fail(at, "data.not_on_map", {"change of '" + change.flag + "'"});
						}
						change.cells.push_back(cell);
					}
				}
			}
			area.changes.push_back(change);
		}
	}
	if (const JsonValue* conveyors = array(reader, json, "conveyors")) {
		for (const JsonValue& c : conveyors->items) {
			reader.only(c, gameKeys("area", "conveyors[]"));
			Conveyor conveyor;
			conveyor.line = c.line;
			std::string direction;
			reader.text(c, "direction", direction, true);
			if (!direction.empty() && !parseFacing(direction, conveyor.direction)) {
				reader.fail(c, "data.bad_direction", {direction});
			}
			stepTime(reader, c, conveyor.stepMs, conveyor.stepUs, 16, 10000);
			reader.text(c, "require_flag", conveyor.requireFlag, false);
			reader.text(c, "block_flag", conveyor.blockFlag, false);
			if (const JsonValue* cells = reader.field(c, "cells", JsonValue::Type::Array, true)) {
				for (const JsonValue& at : cells->items) {
					Cell cell;
					if (reader.cell(at, cell)) {
						checkCell(at, cell, "conveyor");
						conveyor.cells.push_back(cell);
					}
				}
			}
			area.conveyors.push_back(conveyor);
		}
	}
	if (!reader.ok()) {
		return rules::Result<Area>::failure(reader.problems(), reader.errors());
	}
	return rules::Result<Area>::success(std::move(area));
}

}
