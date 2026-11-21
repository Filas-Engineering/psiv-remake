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

// What stands on a solid cell: interact hooks, shops, rest points, chests with a cell and ports.
struct Solid {
	std::string what;
	int line = 0;
};

void checkArea(const Area& area, const std::set<std::string>& entrances, const std::vector<FieldVehicle>& vehicles, Found& found) {
	const std::string& file = area.file;
	std::map<Cell, Solid> solids;
	auto addSolid = [&](Cell c, const std::string& what, int line) {
		auto [it, added] = solids.emplace(c, Solid{what, line});
		if (!added) {
			found.add(file, line, "check.cell_shared", {what, it->second.what, cellText(c)});
		}
	};
	std::set<std::string> hookIds;
	for (const Hook& h : area.hooks) {
		if (!hookIds.insert(h.id).second) {
			found.add(file, h.line, "check.hook_id_taken", {h.id});
		}
		if (h.trigger == HookTrigger::Interact) {
			addSolid(h.at, "hook '" + h.id + "'", h.line);
		}
	}
	for (const Shop& s : area.shops) {
		if (s.at) {
			addSolid(*s.at, "shop '" + s.id + "'", s.line);
		}
		if (s.stock.empty()) {
			found.add(file, s.line, "check.shop_empty", {s.id});
		}
	}
	for (const RestPoint& r : area.rests) {
		if (r.at) {
			addSolid(*r.at, "rest point '" + r.id + "'", r.line);
		}
	}
	for (const Chest& c : area.chests) {
		addSolid(c.at, "chest '" + c.id + "'", c.line);
	}
	for (const Port& p : area.ports) {
		addSolid(p.at, "port '" + p.id + "'", p.line);
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
	for (const Exit& e : area.exits) {
		if (const Solid* s = solidAt(e.at)) {
			found.add(file, e.line, "check.exit_on_object", {e.toArea, s->what});
		}
		if (!exitCells.emplace(e.at, &e).second) {
			found.add(file, e.line, "check.cell_shared", {"exit to '" + e.toArea + "'", "another exit", cellText(e.at)});
		}
	}
	for (const Hook& h : area.hooks) {
		if (h.trigger == HookTrigger::Step) {
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
	// The cells the party can walk to from a way in. Exit cells are reached but never walked through, since
	// stepping on one leaves the area. In a field area a vehicle may carry the party over terrain it crosses.
	auto crossable = [&](Cell c) {
		if (area.walkable(c)) {
			return true;
		}
		if (area.kind != AreaKind::Field || !area.inside(c) || solids.count(c)) {
			return false;
		}
		return std::any_of(vehicles.begin(), vehicles.end(), [&](const FieldVehicle& v) { return v.canCross(area.rows[c.y][c.x]); });
	};
	std::set<Cell> reached;
	std::deque<Cell> queue;
	for (const std::string& marker : entrances) {
		auto it = area.markers.find(marker);
		if (it != area.markers.end() && reached.insert(it->second).second) {
			queue.push_back(it->second);
		}
	}
	while (!queue.empty()) {
		const Cell c = queue.front();
		queue.pop_front();
		if (exitCells.count(c)) {
			continue;
		}
		for (Cell step : {Cell{c.x, c.y - 1}, Cell{c.x + 1, c.y}, Cell{c.x, c.y + 1}, Cell{c.x - 1, c.y}}) {
			if (crossable(step) && reached.insert(step).second) {
				queue.push_back(step);
			}
		}
	}
	for (const Exit& e : area.exits) {
		if (!reached.count(e.at)) {
			found.add(file, e.line, "check.exit_not_reached", {e.toArea, cellText(e.at)});
		}
	}
	for (const auto& [name, cell] : area.markers) {
		if (!reached.count(cell) && !solidAt(cell)) {
			found.add(file, area.markerLines.count(name) ? area.markerLines.at(name) : 0, "check.marker_not_reached", {name});
		}
	}
	for (const Hook& h : area.hooks) {
		if (h.trigger == HookTrigger::Step && !reached.count(h.at) && !solidAt(h.at)) {
			found.add(file, h.line, "check.hook_not_reached", {h.id});
		}
	}
	for (const auto& [cell, solid] : solids) {
		const Cell c = cell;
		const bool besideReached = reached.count({c.x, c.y - 1}) || reached.count({c.x + 1, c.y}) || reached.count({c.x, c.y + 1}) ||
			reached.count({c.x - 1, c.y});
		if (!besideReached) {
			found.add(file, solid.line, "check.object_not_reached", {solid.what});
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
	// Ports lead to each other, and a return lands at a town's arrival marker.
	std::set<std::string> portAreas;
	for (const auto& [id, area] : data.areas) {
		for (const Port& p : area.ports) {
			entrances[id].insert(p.marker);
			portAreas.insert(id);
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
		checkArea(area, in == entrances.end() ? std::set<std::string>() : in->second, c.vehicles, found);
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
