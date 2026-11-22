#include "game/check.h"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <map>
#include <set>

namespace game {

namespace {

std::string join(const std::string& directory, const std::string& file) {
	return directory.empty() ? file : directory + "/" + file;
}

std::string cellText(Cell c) {
	return "[" + std::to_string(c.x) + ", " + std::to_string(c.y) + "]";
}

class Found {
public:
	explicit Found(std::vector<rules::Problem>& out) : out_(out) {}

	void add(const std::string& file, int line, const char* key, std::vector<std::string> params) {
		out_.push_back({file, line, "", {key, std::move(params)}});
	}

private:
	std::vector<rules::Problem>& out_;
};

// What stands on a solid cell. A point is used from a floor cell beside it (interact hooks, shops, rest points, chests
// with a cell, ports and helms), so the party must reach one; a cell that is only solid (where an actor stands apart
// from its talk point, or a solid actor's home) blocks the way and is never used.
struct Solid {
	std::string what;
	int line = 0;
	bool point = true;
};

// An object the party uses: the cells it is used at (a hook's further cells, and under `interact: "nearby"` the cell
// its actor stands on behind a counter) and whether it is used across a counter (game/README.md, "Interaction").
struct Use {
	std::string what;
	int line = 0;
	std::vector<Cell> cells;
	bool across = false;
};

void checkArea(const Area& area, const std::set<std::string>& entrances, const GameConfig& config, Found& found) {
	const std::string& file = area.file;
	const std::vector<std::string>& actors = config.actors;
	const std::vector<FieldVehicle>& vehicles = config.vehicles;
	std::map<Cell, Solid> solids;
	auto addSolid = [&](Cell c, const std::string& what, int line, bool point = true) {
		auto [it, added] = solids.emplace(c, Solid{what, line, point});
		if (!added) {
			found.add(file, line, "check.cell_shared", {what, it->second.what, cellText(c)});
		}
	};
	std::vector<Use> uses;
	auto addPoint = [&](Cell c, const std::string& what, int line) {
		addSolid(c, what, line);
		uses.push_back({what, line, {c}, config.interactAcross});
	};
	std::set<std::string> hookIds;
	for (const Hook& h : area.hooks) {
		if (!hookIds.insert(h.id).second) {
			found.add(file, h.line, "check.hook_id_taken", {h.id});
		}
		if (h.trigger == HookTrigger::Interact) {
			const std::string what = "hook '" + h.id + "'";
			addPoint(h.at, what, h.line);
			for (Cell c : h.cells) {
				addSolid(c, what, h.line);
				uses.back().cells.push_back(c);
			}
			if (h.actorAt && config.interactReach == InteractReach::Nearby) {
				uses.back().cells.push_back(*h.actorAt);
			}
			uses.back().across = uses.back().across || h.across;
		}
	}
	for (const Hook& h : area.hooks) {
		if (h.trigger == HookTrigger::Interact && h.actorAt) {
			addSolid(*h.actorAt, "actor '" + h.id + "'", h.line, false);
		}
	}
	for (const ActorDef& actor : config.actorDefs) {
		const auto marker = area.markers.find(actor.marker);
		if (actor.area == area.id && actor.solid && !actor.path && (actor.at || marker != area.markers.end())) {
			addSolid(actor.at ? *actor.at : marker->second, "actor '" + actor.id + "'", 0, false);
		}
	}
	for (const Shop& s : area.shops) {
		if (s.at) {
			addPoint(*s.at, "shop '" + s.id + "'", s.line);
		}
		if (s.stock.empty()) {
			found.add(file, s.line, "check.shop_empty", {s.id});
		}
	}
	for (const RestPoint& r : area.rests) {
		if (r.at) {
			addPoint(*r.at, "rest point '" + r.id + "'", r.line);
		}
	}
	for (const Chest& c : area.chests) {
		addPoint(c.at, "chest '" + c.id + "'", c.line);
	}
	for (const Port& p : area.ports) {
		addPoint(p.at, "port '" + p.id + "'", p.line);
	}
	for (const Helm& h : area.helms) {
		addPoint(h.at, "helm '" + h.id + "'", h.line);
	}
	auto solidAt = [&](Cell c) -> const Solid* {
		auto it = solids.find(c);
		return it == solids.end() ? nullptr : &it->second;
	};

	for (const auto& [name, cell] : area.markers) {
		if (const Solid* s = solidAt(cell)) {
			found.add(file, area.markerLines.count(name) ? area.markerLines.at(name) : 0, "check.marker_on_object", {name, s->what});
		}
	}
	std::map<Cell, const Exit*> exitCells;
	const Exit gangway{{}, "ashore", "", false, 0};
	for (Cell g : area.gangway) {
		exitCells.emplace(g, &gangway);
	}
	// A door: an interact hook on a gated exit's cell answers while the exit is closed, and the exit takes the cell once open.
	std::set<Cell> doors;
	for (const Exit& e : area.exits) {
		const Hook* hook = area.hookAt(e.at, HookTrigger::Interact);
		if (hook && hook->at == e.at && (!e.requireFlag.empty() || !e.blockFlag.empty())) {
			doors.insert(e.at);
		}
	}
	for (const Exit& e : area.exits) {
		if (const Solid* s = solidAt(e.at); s && !doors.count(e.at)) {
			found.add(file, e.line, "check.exit_on_object", {e.toArea, s->what});
		}
		if (!exitCells.emplace(e.at, &e).second) {
			found.add(file, e.line, "check.cell_shared", {"exit to '" + e.toArea + "'", "another exit", cellText(e.at)});
		}
	}
	for (const Hook& h : area.hooks) {
		if (h.trigger == HookTrigger::Step && !h.tile) {
			if (const Solid* s = solidAt(h.at)) {
				found.add(file, h.line, "check.step_hook_on_object", {h.id, s->what});
			}
		}
	}

	std::set<std::string> zoneIds;
	for (std::size_t i = 0; i < area.zones.size(); ++i) {
		const Zone& z = area.zones[i];
		if (!zoneIds.insert(z.id).second) {
			found.add(file, z.line, "check.zone_id_taken", {z.id});
		}
		auto backwards = [](const Zone& zone) { return zone.from.x > zone.to.x || zone.from.y > zone.to.y; };
		if (backwards(z)) {
			found.add(file, z.line, "check.zone_backwards", {z.id});
			continue;
		}
		if (!area.inside(z.from) || !area.inside(z.to)) {
			found.add(file, z.line, "check.zone_outside_map", {z.id, std::to_string(area.width()), std::to_string(area.height())});
		}
		for (std::size_t j = 0; j < i; ++j) {
			const Zone& o = area.zones[j];
			if (!backwards(o) && z.from.x <= o.to.x && o.from.x <= z.to.x && z.from.y <= o.to.y && o.from.y <= z.to.y) {
				found.add(file, z.line, "check.zones_overlap", {z.id, o.id});
			}
		}
		for (Cell s : z.visible.spawns) {
			if (const Solid* solid = solidAt(s)) {
				found.add(file, z.line, "check.spawn_on_object", {z.id, cellText(s), solid->what});
			}
		}
	}

	if (entrances.empty()) {
		found.add(file, 0, "check.area_no_entrance", {area.id});
		return;
	}
	// A hook solid only while active whose flags or actor can turn it off may stand aside, so reach is worked out with
	// such hooks aside: a thing reached while one stands is reached with it aside too. Such a hook is used while it
	// stands, and the first cell beside it a walk reaches never needs a way through the hook itself.
	std::set<Cell> switchable;
	for (const Hook& h : area.hooks) {
		const bool changes = !h.requireFlag.empty() || !h.blockFlag.empty() || std::find(actors.begin(), actors.end(), h.id) != actors.end();
		if (h.trigger == HookTrigger::Interact && h.solidWhileActive && changes) {
			switchable.insert(h.at);
		}
	}
	// The cells the party can walk to from a way in. Exit cells are reached but never walked through, since
	// stepping on one leaves the area. In a field area a vehicle may carry the party over terrain it crosses, and a
	// cell that a flag's change turns to floor counts as floor.
	Area view = area;
	view.passable = switchable;
	// Exit and gangway cells a step from a cell the party stands on takes.
	std::set<Cell> used;
	const std::set<Cell> reached = [&] {
		auto solidHere = [&](Cell c) { return solids.count(c) && !switchable.count(c); };
		// A platform's docks are stood on while it is there, and it carries the party from one to the other (only from `from`
		// to `to` for a one-way platform, which never goes back while its flags hold).
		std::map<Cell, std::vector<Cell>> carried;
		for (const Platform& p : area.platforms) {
			carried[p.from].push_back(p.to);
			if (p.mode != PlatformMode::OneWay) {
				carried[p.to].push_back(p.from);
			} else {
				carried[p.to];
			}
		}
		auto crossable = [&](Cell c) {
			if (carried.count(c) || doors.count(c)) {
				return true;
			}
			const auto solid = solids.find(c);
			if (solid != solids.end() && !solid->second.point) {
				return false;
			}
			if (view.walkable(c)) {
				return true;
			}
			const bool opened = std::any_of(area.changes.begin(), area.changes.end(), [&](const CellChange& change) {
				return change.tile == '.' && std::find(change.cells.begin(), change.cells.end(), c) != change.cells.end();
			});
			if (opened && !solidHere(c)) {
				return true;
			}
			if (area.kind != AreaKind::Field || !area.inside(c) || solidHere(c)) {
				return false;
			}
			return std::any_of(vehicles.begin(), vehicles.end(), [&](const FieldVehicle& v) { return v.canCross(area.rows[c.y][c.x]); });
		};
		// A cell is stood on when the party can be there without leaving: floor, a way in on an exit cell, or an exit cell
		// stepped onto from another exit cell that does not take it (`fires: from_floor`). An exit cell a step takes is
		// reached (used) but never walked through.
		std::set<Cell> standing;
		std::set<Cell> cells;
		std::deque<Cell> queue;
		for (const std::string& marker : entrances) {
			auto it = area.markers.find(marker);
			if (it != area.markers.end() && standing.insert(it->second).second) {
				cells.insert(it->second);
				queue.push_back(it->second);
			}
		}
		while (!queue.empty()) {
			const Cell c = queue.front();
			queue.pop_front();
			std::vector<Cell> next = {Cell{c.x, c.y - 1}, Cell{c.x + 1, c.y}, Cell{c.x, c.y + 1}, Cell{c.x - 1, c.y}};
			if (const auto ride = carried.find(c); ride != carried.end()) {
				next.insert(next.end(), ride->second.begin(), ride->second.end());
			}
			for (Cell step : next) {
				if (!crossable(step)) {
					continue;
				}
				const auto exit = exitCells.find(step);
				if (exit != exitCells.end() && (exit->second == &gangway || exitFiresFrom(area, *exit->second, c, config.exitFires))) {
					cells.insert(step);
					used.insert(step);
					continue;
				}
				if (standing.insert(step).second) {
					cells.insert(step);
					queue.push_back(step);
				}
			}
		}
		return cells;
	}();
	// The cells of a multi-cell exit (touching exits to the same marker) are used together: a cell that only other exit
	// cells touch is part of an exit taken from the floor beside another of its cells.
	const auto usedTogether = [&](const Exit& start) {
		std::set<Cell> seen{start.at};
		std::deque<Cell> queue{start.at};
		while (!queue.empty()) {
			const Cell c = queue.front();
			queue.pop_front();
			if (used.count(c)) {
				return true;
			}
			for (Cell n : {Cell{c.x, c.y - 1}, Cell{c.x + 1, c.y}, Cell{c.x, c.y + 1}, Cell{c.x - 1, c.y}}) {
				const Exit* e = area.exitAt(n);
				if (e && e->toArea == start.toArea && e->toMarker == start.toMarker && seen.insert(n).second) {
					queue.push_back(n);
				}
			}
		}
		return false;
	};
	for (const Exit& e : area.exits) {
		if (!usedTogether(e)) {
			found.add(file, e.line, "check.exit_not_reached", {e.toArea, cellText(e.at)});
		}
	}
	for (Cell g : area.gangway) {
		if (!used.count(g)) {
			found.add(file, 0, "check.exit_not_reached", {"ashore", cellText(g)});
		}
	}
	for (const auto& [name, cell] : area.markers) {
		if (!reached.count(cell) && !solidAt(cell)) {
			found.add(file, area.markerLines.count(name) ? area.markerLines.at(name) : 0, "check.marker_not_reached", {name});
		}
	}
	for (const Hook& h : area.hooks) {
		if (h.trigger == HookTrigger::Step && !h.tile && !reached.count(h.at) && !solidAt(h.at)) {
			found.add(file, h.line, "check.hook_not_reached", {h.id});
		}
	}
	// Standing on an exit or a gangway leaves the area, so the party cannot face an object from there.
	auto standable = [&](Cell at) {
		return reached.count(at) && !exitCells.count(at) && std::find(area.gangway.begin(), area.gangway.end(), at) == area.gangway.end();
	};
	// A counter is a cell that is not floor and holds nothing, which the session lets a point be used across.
	auto counter = [&](Cell c) { return area.inside(c) && area.rows[c.y][c.x] != '.' && !solids.count(c) && !exitCells.count(c); };
	for (const Use& use : uses) {
		const bool used = std::any_of(use.cells.begin(), use.cells.end(), [&](Cell c) {
			for (Cell d : {Cell{0, -1}, Cell{1, 0}, Cell{0, 1}, Cell{-1, 0}}) {
				const Cell near{c.x - d.x, c.y - d.y};
				// A marked counter (R109) passes any point behind it to the floor two cells away.
				if (standable(near) || (((use.across && counter(near)) || (area.counters.count(near) && !solids.count(near))) && standable({near.x - d.x, near.y - d.y}))) {
					return true;
				}
			}
			return false;
		});
		if (!used) {
			found.add(file, use.line, "check.object_not_reached", {use.what});
		}
	}
}

}

std::vector<rules::Problem> crossCheck(const GameData& data, bool loaderChecked) {
	std::vector<rules::Problem> out;
	Found found(out);
	const GameConfig& c = data.config;

	// Ways into each area: the start position, every exit that leads there, every script warp.
	std::map<std::string, std::set<std::string>> entrances;
	std::set<std::string> warpTargets;
	entrances[c.startArea].insert(c.startMarker);
	for (const auto& [id, area] : data.areas) {
		for (const Exit& e : area.exits) {
			entrances[e.toArea].insert(e.toMarker);
		}
	}
	for (const events::Instruction& in : data.program.code) {
		if (in.op == events::Op::action && in.action.type == events::ActionType::warp) {
			entrances[in.action.subject].insert(in.action.target);
			warpTargets.insert(in.action.subject);
		}
	}
	// Ferry ports lead to each other, a ship's ports to its vessel and the vessel to them, and a return lands at a town's
	// arrival marker.
	std::set<std::string> portAreas;
	std::map<std::string, std::set<std::string>> shipPorts;
	for (const auto& [id, area] : data.areas) {
		for (const Port& p : area.ports) {
			entrances[id].insert(p.marker);
			if (p.ship.empty()) {
				portAreas.insert(id);
			} else {
				shipPorts[p.ship].insert(id);
			}
		}
		if (!area.arrival.empty()) {
			entrances[id].insert(area.arrival);
		}
	}

	// In game.json's order, so the first area listed keeps a chest id and a later one is reported.
	std::vector<const Area*> ordered;
	for (const std::string& file : c.areaFiles) {
		for (const auto& [id, area] : data.areas) {
			if (area.file == file) {
				ordered.push_back(&area);
			}
		}
	}
	std::map<std::string, std::string> chestAreas;
	for (const Area* a : ordered) {
		const Area& area = *a;
		const std::string& id = area.id;
		if (!loaderChecked) {
			for (const Exit& e : area.exits) {
				const Area* to = data.area(e.toArea);
				if (!to) {
					found.add(area.file, e.line, "data.exit_leads_unknown_area", {e.toArea});
				} else if (!to->markers.count(e.toMarker)) {
					found.add(area.file, e.line, "data.exit_leads_missing_marker", {e.toArea, e.toMarker});
				}
			}
		}
		for (const Chest& ch : area.chests) {
			auto [it, added] = chestAreas.emplace(ch.id, id);
			if (!added) {
				found.add(area.file, ch.line, "check.chest_id_taken", {ch.id, it->second});
			}
		}
		auto in = entrances.find(id);
		checkArea(area, in == entrances.end() ? std::set<std::string>() : in->second, c, found);
	}

	// Areas reachable from the start through exits. A script warp can run anywhere, so its target counts as reached.
	std::set<std::string> reached = warpTargets;
	std::deque<std::string> queue(warpTargets.begin(), warpTargets.end());
	if (data.area(c.startArea) && reached.insert(c.startArea).second) {
		queue.push_back(c.startArea);
	}
	while (!queue.empty()) {
		const Area* a = data.area(queue.front());
		queue.pop_front();
		if (!a) {
			continue;
		}
		for (const Exit& e : a->exits) {
			if (data.area(e.toArea) && reached.insert(e.toArea).second) {
				queue.push_back(e.toArea);
			}
		}
		if (portAreas.count(a->id)) {
			for (const std::string& other : portAreas) {
				if (reached.insert(other).second) {
					queue.push_back(other);
				}
			}
		}
		for (const auto& [ship, areas] : shipPorts) {
			if (areas.count(a->id) && reached.insert(ship).second) {
				queue.push_back(ship);
			}
			if (ship == a->id) {
				for (const std::string& other : areas) {
					if (reached.insert(other).second) {
						queue.push_back(other);
					}
				}
			}
		}
	}
	for (const auto& [id, area] : data.areas) {
		if (!reached.count(id) && entrances.count(id)) {
			found.add(area.file, 0, "check.area_not_reached", {id, c.startArea});
		}
	}

	if (!loaderChecked) {
		for (std::size_t i = 0; i < data.languages.size() && i < c.languages.size(); ++i) {
			for (const auto& [id, area] : data.areas) {
				if (!data.languages[i].text.count("area." + id)) {
					found.add(c.languages[i].text, 0, "check.area_name_missing", {"area." + id, c.languages[i].code});
				}
			}
		}
	}
	return out;
}

CheckReport checkData(const DataDirectories& directories) {
	CheckReport report;
	GameData data;
	report.problems = loadGameDataPartly(directories, data, &report.complete);
	std::vector<rules::Problem> more = crossCheck(data, report.complete);
	report.problems.insert(report.problems.end(), more.begin(), more.end());

	const std::string coreDirectory = directories.core.empty() && !data.config.coreData.empty() ? join(directories.game, data.config.coreData) : directories.core;
	namespace fs = std::filesystem;
	std::error_code ignored;
	for (rules::Problem& p : report.problems) {
		if (p.file.empty() || fs::is_regular_file(fs::u8path(p.file), ignored)) {
			continue;
		}
		if (fs::is_regular_file(fs::u8path(join(directories.game, p.file)), ignored)) {
			p.file = join(directories.game, p.file);
		} else if (!coreDirectory.empty() && fs::is_regular_file(fs::u8path(join(coreDirectory, p.file)), ignored)) {
			p.file = join(coreDirectory, p.file);
		}
	}

	const rules::Database& db = data.db;
	std::size_t coreKeys = 0;
	if (!db.abilities.empty()) {
		rules::Result<rules::TextTable> coreText = rules::loadTextTable(join(coreDirectory, "text_en.json"));
		coreKeys = coreText.ok() ? coreText.value->size() : 0;
		report.loaded.push_back({"core data " + coreDirectory,
			{{"statuses", db.statuses.size()}, {"abilities", db.abilities.size()}, {"items", db.items.size()}, {"equipment pieces", db.equipment.size()},
				{"characters", db.characters.size()}, {"vehicles", db.vehicles.size()}, {"enemies", db.enemies.size()},
				{"encounters", db.encounters.size()}, {"combos", db.combos.size()}, {"difficulties", db.difficulties.size()},
				{"macros", db.macros.size()}, {"text keys (en)", coreKeys}}});
	}
	if (!data.config.title.empty()) {
		std::size_t markers = 0, exits = 0, hooks = 0, shops = 0, rests = 0, chests = 0, zones = 0;
		for (const auto& [id, a] : data.areas) {
			markers += a.markers.size();
			exits += a.exits.size();
			hooks += a.hooks.size();
			shops += a.shops.size();
			rests += a.rests.size();
			chests += a.chests.size();
			zones += a.zones.size();
		}
		std::size_t flags = 0, vars = 0;
		for (const events::Declaration& d : data.registry.declarations) {
			(d.is_flag ? flags : vars) += 1;
		}
		const Journal& j = data.journal;
		report.loaded.push_back({"game data " + directories.game,
			{{"areas", data.areas.size()}, {"markers", markers}, {"exits", exits}, {"hooks", hooks}, {"shops", shops}, {"rest points", rests},
				{"chests", chests}, {"encounter zones", zones}, {"script files", data.program.files.size()}, {"events", data.program.events.size()},
				{"flags", flags}, {"variables", vars}, {"actors", data.config.actors.size()}, {"prices", data.config.prices.size()},
				{"objectives", j.objectives.size()}, {"jobs", j.jobs.size()}, {"encyclopedia entries", j.encyclopedia.size()},
				{"party talk sets", j.partyTalk.size()}}});
		for (const LanguageTables& l : data.languages) {
			const std::size_t descriptions = static_cast<std::size_t>(std::count_if(l.text.begin(), l.text.end(),
				[](const auto& entry) { return entry.first.compare(0, std::char_traits<char>::length(kDescriptionPrefix), kDescriptionPrefix) == 0; }));
			const std::size_t own = (l.text.size() >= coreKeys ? l.text.size() - coreKeys : l.text.size()) - descriptions;
			report.loaded.push_back({"text " + l.code, {{"game text keys", own}, {"descriptions", descriptions}, {"dialogue strings", l.strings.entries.size()}}});
		}
	}
	return report;
}

std::string formatCheckReport(const CheckReport& report) {
	std::string out;
	if (!report.problems.empty()) {
		out += "Problems (" + std::to_string(report.problems.size()) + "):\n";
		for (const rules::Problem& p : report.problems) {
			out += "  " + rules::formatProblem(p, problemText()) + "\n";
		}
	}
	out += "Loaded:\n";
	if (report.loaded.empty()) {
		out += "  nothing\n";
	}
	for (const auto& [heading, counts] : report.loaded) {
		std::string line;
		for (const CheckCount& c : counts) {
			line += (line.empty() ? "" : ", ") + c.what + " " + std::to_string(c.count);
		}
		out += "  " + heading + ": " + line + "\n";
	}
	if (!report.complete) {
		out += "Not checked: the scripts and the references between files, because the problems above stopped the load. Fix them and run again.\n";
	}
	out += report.ok() ? "OK: no problems.\n" : "FAILED: " + std::to_string(report.problems.size()) + " problem" + (report.problems.size() == 1 ? "" : "s") + ".\n";
	return out;
}

}
