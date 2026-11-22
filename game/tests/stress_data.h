#pragma once

#include "actor_data.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The placeholder data with a large synthetic world added, for timing game_cli on data the size of a full game (`make
// perf`, tools/make_stress_data.cpp) and for the work bounds of tests/test_perf.cpp. It writes OUT/game/data and
// OUT/core/data as actor_data does, and OUT/stress.txt, a script that walks the busy area and a tour of the others, talks
// to signs, rests, fights, saves and loads. Every name is invented. Settings and their defaults:
//   areas=300 width=48 height=32 busy_width=96 busy_height=64  areas, each WIDTHxHEIGHT; stress_000 is the busy one
//   actors=700 busy_wanderers=220 busy_solid=50                actors in all, and the busy area's wanderers and standing ones
//   hooks=1900 busy_hooks=300 events=3500 background=12        hooks in all and in the busy area, events, background events
//   timing=ticks|exact rounds=3 tour=40                        step_timing, busy-area rounds and areas the tour crosses
namespace stress_data {

struct Spot {
	int x = 0;
	int y = 0;
	bool operator<(const Spot& o) const { return x != o.x ? x < o.x : y < o.y; }
};

struct Options {
	int areas = 300;
	int width = 48;
	int height = 32;
	int busyWidth = 96;
	int busyHeight = 64;
	int actors = 700;
	int busyWanderers = -1;
	int busySolid = -1;
	int hooks = 1900;
	int busyHooks = 300;
	int events = 3500;
	int background = 12;
	int rounds = 3;
	int tour = 40;
	std::string timing = "ticks";
};

inline std::string id3(const std::string& prefix, int n) {
	std::string digits = std::to_string(n);
	while (digits.size() < 3) {
		digits = "0" + digits;
	}
	return prefix + digits;
}

inline std::string id4(const std::string& prefix, int n) {
	std::string digits = std::to_string(n);
	while (digits.size() < 4) {
		digits = "0" + digits;
	}
	return prefix + digits;
}

inline std::string cell(Spot s) {
	return "[" + std::to_string(s.x) + ", " + std::to_string(s.y) + "]";
}

inline const char* const kLinesEn[] = {"The stone is cold to the touch.", "Someone carved a small fish here.", "The paint is peeling.", "A draft comes through a crack.",
	"Moss grows thick in the corner.", "Faint scratches count the days.", "The plaster smells of salt.", "A nail sticks out, bent.",
	"A chalk arrow points nowhere.", "Old rope is tied to a ring."};
inline const char* const kLinesJa[] = {"いしは つめたい。", "ちいさな さかなの ほりもの。", "ペンキが はがれている。", "すきまかぜが ふく。", "すみに こけが しげる。",
	"ひにちを かぞえる きず。", "しおの においが する。", "まがった くぎが ある。", "どこも ささない やじるし。", "わに ふるい なわ。"};
inline constexpr int kLines = 200;

struct AreaPlan {
	std::string id;
	int width = 0;
	int height = 0;
	std::vector<std::string> rows;
	std::set<Spot> used;
	std::vector<Spot> homes;
	std::vector<Spot> floorSlots;
	std::vector<Spot> wallSlots;
	std::vector<std::string> hooks;
	std::string extra;
	int midX = 0;
	int midY = 0;
	bool busy = false;
};

inline AreaPlan planArea(int index, int width, int height) {
	AreaPlan a;
	a.id = id3("stress_", index);
	a.width = width;
	a.height = height;
	a.midX = width / 2;
	a.midY = height / 2;
	a.busy = index == 0;
	a.rows.assign(static_cast<std::size_t>(height), std::string(static_cast<std::size_t>(width), '.'));
	for (int x = 0; x < width; ++x) {
		a.rows[0][x] = '#';
		a.rows[height - 1][x] = '#';
	}
	for (int y = 0; y < height; ++y) {
		a.rows[y][0] = '#';
		a.rows[y][width - 1] = '#';
	}
	const auto reserved = [&](int x, int y) { return y <= 2 || y >= height - 3 || x <= 2 || x >= width - 3 || std::abs(x - a.midX) < 3 || std::abs(y - a.midY) < 3; };
	// Homes on a lattice three cells apart, far enough from the corridors that a wanderer's radius of 2 never reaches them.
	for (int y = 4; y <= height - 5; y += 3) {
		for (int x = 4; x <= width - 5; x += 3) {
			if (std::abs(x - a.midX) >= 3 && std::abs(y - a.midY) >= 3) {
				a.homes.push_back({x, y});
			}
		}
	}
	// Pillars off the lattice, on no corridor.
	for (int y = 5; y < height - 3; y += 8) {
		for (int x = 5; x < width - 3; x += 8) {
			if (!reserved(x, y) && (x - 4) % 3 != 0) {
				a.rows[y][x] = '#';
				a.wallSlots.push_back({x, y});
			}
		}
	}
	for (const Spot& h : a.homes) {
		const Spot s{h.x + 1, h.y + 1};
		if (!reserved(s.x, s.y) && a.rows[s.y][s.x] == '.') {
			a.floorSlots.push_back(s);
		}
	}
	return a;
}

// Copies GAME and CORE to OUT/game/data and OUT/core/data, adds the synthetic world and writes OUT/stress.txt, with
// `settings` as KEY=VALUE. Returns the problem, or empty; `summary` says what was made.
inline std::string make(const std::string& game, const std::string& core, const std::string& out, const std::vector<std::string>& settings, std::string& summary) {
	Options o;
	for (const std::string& arg : settings) {
		const std::size_t eq = arg.find('=');
		const std::string key = arg.substr(0, eq);
		const std::string value = eq == std::string::npos ? "" : arg.substr(eq + 1);
		const int n = std::atoi(value.c_str());
		std::map<std::string, int*> ints{{"areas", &o.areas}, {"width", &o.width}, {"height", &o.height}, {"busy_width", &o.busyWidth}, {"busy_height", &o.busyHeight},
			{"actors", &o.actors}, {"busy_wanderers", &o.busyWanderers}, {"busy_solid", &o.busySolid}, {"hooks", &o.hooks}, {"busy_hooks", &o.busyHooks},
			{"events", &o.events}, {"background", &o.background}, {"rounds", &o.rounds}, {"tour", &o.tour}};
		if (key == "timing" && (value == "ticks" || value == "exact")) {
			o.timing = value;
		} else if (ints.count(key) && n >= 0 && !value.empty()) {
			*ints[key] = n;
		} else {
			return "bad argument '" + arg + "'";
		}
	}
	o.areas = std::max(2, o.areas);
	o.width = std::max(24, o.width);
	o.height = std::max(20, o.height);
	o.busyWidth = std::max(24, o.busyWidth);
	o.busyHeight = std::max(20, o.busyHeight);
	if (o.busyWanderers < 0) {
		o.busyWanderers = o.actors * 220 / 700;
	}
	if (o.busySolid < 0) {
		o.busySolid = o.actors * 50 / 700;
	}
	o.tour = std::min(o.tour, o.areas - 1);
	o.background = std::min(o.background, 32);

	namespace fs = std::filesystem;
	std::error_code error;
	fs::remove_all(out, error);
	const std::string data = out + "/game/data";
	fs::create_directories(out + "/game", error);
	fs::create_directories(out + "/core", error);
	fs::copy(game, data, fs::copy_options::recursive, error);
	if (!error) {
		fs::copy(core, out + "/core/data", fs::copy_options::recursive, error);
	}
	if (error) {
		return "cannot copy the data: " + error.message();
	}

	std::vector<AreaPlan> areas;
	for (int i = 0; i < o.areas; ++i) {
		areas.push_back(i == 0 ? planArea(i, o.busyWidth, o.busyHeight) : planArea(i, o.width, o.height));
	}
	AreaPlan& busy = areas[0];

	// Actors: the busy area's patrols, path actors, wanderers and standing ones, then the rest spread over the others.
	std::ostringstream actors;
	std::vector<std::string> actorIds;
	std::vector<std::string> busyStanding;
	std::vector<std::string> wandererTouch;
	std::vector<std::size_t> nextHome(areas.size(), 0);
	const auto takeHome = [&](AreaPlan& a, Spot& at) {
		while (nextHome[&a - &areas[0]] < a.homes.size()) {
			const Spot s = a.homes[nextHome[&a - &areas[0]]++];
			if (!a.used.count(s) && !a.used.count({s.x + 1, s.y}) && a.rows[s.y][s.x + 1] == '.') {
				at = s;
				a.used.insert(s);
				return true;
			}
		}
		return false;
	};
	const auto addActor = [&](AreaPlan& a, const std::string& kind) {
		Spot at;
		if (!takeHome(a, at)) {
			return false;
		}
		const std::string id = id4("stress_a", static_cast<int>(actorIds.size()));
		actorIds.push_back(id);
		actors << "{\"id\": \"" << id << "\", \"area\": \"" << a.id << "\", \"at\": " << cell(at);
		if (kind == "wander") {
			const int n = static_cast<int>(actorIds.size());
			actors << ", \"wander\": {\"radius\": 2, \"speed_percent\": " << (50 + n % 4 * 25) << ", \"pause_ms\": [" << (200 + n % 5 * 100) << ", " << (900 + n % 7 * 150)
				   << "]" << (n % 5 == 0 ? ", \"stand_percent\": 30" : "") << (n % 11 == 0 ? ", \"axis\": \"x\"" : "") << "}";
			if (n % 9 == 0) {
				actors << ", \"solid\": false";
			} else if (n % 13 == 0) {
				wandererTouch.push_back(a.id + " " + id);
			}
		} else if (kind == "patrol") {
			a.used.insert({at.x + 1, at.y});
			actors << ", \"patrol\": {\"speed_percent\": 75, \"steps\": [{\"walk\": \"east\", \"count\": 1, \"pause_ms\": 300}, {\"walk\": \"west\", \"count\": 1, \"face\": \"south\"}]}";
		} else if (kind == "path") {
			a.used.insert({at.x + 1, at.y});
			actors << ", \"path\": {\"offsets\": [[0, 0], [4, 0], [8, 0], [12, 0], [8, 0], [4, 0]], \"ticks\": 2, \"footprint\": [1, 1]}";
		} else if (a.busy) {
			busyStanding.push_back(id);
		}
		actors << "}, ";
		return true;
	};
	for (int i = 0; i < 10; ++i) {
		addActor(busy, "patrol");
	}
	for (int i = 0; i < 3; ++i) {
		addActor(busy, "path");
	}
	for (int i = 0; i < o.busyWanderers; ++i) {
		addActor(busy, "wander");
	}
	for (int i = 0; i < o.busySolid; ++i) {
		addActor(busy, "solid");
	}
	for (int i = 0; static_cast<int>(actorIds.size()) < o.actors && i < o.actors * 4; ++i) {
		AreaPlan& a = areas[1 + static_cast<std::size_t>(i) % (areas.size() - 1)];
		const char* kind = i % 50 == 49 ? "path" : i % 10 == 9 ? "patrol" : i % 3 == 1 ? "solid" : "wander";
		addActor(a, kind);
	}

	// Flags: f for the signs, h for the hooks that fire once (their own block flag).
	constexpr int kFlags = 600;
	std::vector<std::string> flags;
	for (int i = 0; i < kFlags; ++i) {
		flags.push_back(id3("stress.f", i));
	}
	int onceFlags = 0;
	std::ostringstream events;
	int eventCount = 0;
	int signCount = 0;
	std::vector<std::string> battleEvents;
	auto onceFlag = [&]() {
		const std::string f = id4("stress.h", onceFlags++);
		flags.push_back(f);
		return f;
	};
	const auto newEvent = [&](const std::string& body, const char* mark = "skippable") {
		const std::string id = id4("stress_e", eventCount++);
		events << "event " << id << " " << mark << "\n" << body << "end\n\n";
		return id;
	};

	// Hooks: per area, signs on the top and bottom walls and on pillars, step hooks off the corridors, flag hooks, load
	// hooks, a sign the tour talks to, and in the busy area battle signs, solid-while-active hooks and touch hooks.
	std::vector<int> hookTargets(areas.size(), 0);
	hookTargets[0] = std::min(o.busyHooks, o.hooks);
	for (int i = 0, left = o.hooks - hookTargets[0]; left > 0; ++i, --left) {
		++hookTargets[1 + static_cast<std::size_t>(i) % (areas.size() - 1)];
	}
	std::vector<std::vector<Spot>> signSpots(areas.size());
	std::vector<std::string> scriptSigns(areas.size());
	int bgSpawned = 0;
	const int helperCount = std::max(1, o.events - o.hooks);
	for (std::size_t ai = 0; ai < areas.size(); ++ai) {
		AreaPlan& a = areas[ai];
		std::vector<Spot> walls;
		for (int x = 3; x < a.width - 3; x += 2) {
			if (std::abs(x - a.midX) > 1) {
				walls.push_back({x, 0});
				walls.push_back({x, a.height - 1});
			}
		}
		std::size_t wall = 0;
		std::size_t floor = 0;
		std::size_t pillar = 0;
		int made = 0;
		const auto hook = [&](const std::string& json) {
			a.hooks.push_back(json);
			++made;
		};
		const auto sign = [&](Spot at, const std::string& extraKeys) {
			const std::string f = flags[static_cast<std::size_t>(signCount) % kFlags];
			const std::string body = "\tnarrate @" + id3("stress.line.", signCount % kLines) + "\n\tset " + f + "\n\tcall " + id4("stress_x", signCount % helperCount) + "\n";
			++signCount;
			const std::string event = newEvent(body);
			hook("{\"id\": \"" + event + "\", \"at\": " + cell(at) + ", \"event\": \"" + event + "\"" + extraKeys + "}");
			return event;
		};
		// The sign the script talks to: on the top wall just east of the corridor; in the busy area also one that warps
		// back to the gate, so the data check reaches the area from the start.
		const Spot main{a.midX + 2, 0};
		scriptSigns[ai] = sign(main, "");
		if (a.busy) {
			const std::string event = newEvent("\tnarrate @stress.line.000\n\twarp " + a.id + " west_gate\n");
			hook("{\"id\": \"" + event + "\", \"at\": " + cell({a.midX - 2, 0}) + ", \"event\": \"" + event + "\"}");
			for (int b = 0; b < 3; ++b) {
				const Spot at{a.midX + 2, a.height - 1};
				const Spot battleAt{at.x + 2 * b, at.y};
				const std::string body = "\tbattle training\n\twin\n\t\tset " + onceFlag() + "\n\tlose\n\t\twarp " + a.id + " west_gate\n\tflee\n\tend\n";
				const std::string event = newEvent(body, "unskippable");
				battleEvents.push_back(event + " " + std::to_string(battleAt.x) + " " + std::to_string(battleAt.y));
				hook("{\"id\": \"" + event + "\", \"at\": " + cell(battleAt) + ", \"event\": \"" + event + "\"}");
			}
		}
		std::set<Spot> taken{main, {a.midX - 2, 0}};
		for (int b = 0; a.busy && b < 3; ++b) {
			taken.insert({a.midX + 2 + 2 * b, a.height - 1});
		}
		int kind = static_cast<int>(ai);
		while (made < hookTargets[ai]) {
			const int k = kind++ % 8;
			if (k == 0 || k == 1 || k == 4) {
				while (wall < walls.size() && taken.count(walls[wall])) {
					++wall;
				}
				if (wall < walls.size()) {
					taken.insert(walls[wall]);
					signSpots[ai].push_back(walls[wall]);
					sign(walls[wall++], "");
					continue;
				}
			}
			if (k == 2) {
				while (floor < a.floorSlots.size() && a.used.count(a.floorSlots[floor])) {
					++floor;
				}
				if (floor < a.floorSlots.size()) {
					const Spot at = a.floorSlots[floor++];
					a.used.insert(at);
					const std::string once = onceFlag();
					const std::string event = newEvent("\tset " + once + "\n\tvar stress.steps += 1\n");
					hook("{\"id\": \"" + event + "\", \"at\": " + cell(at) + ", \"trigger\": \"step\", \"event\": \"" + event + "\", \"require_flag\": \"" +
						flags[static_cast<std::size_t>(made * 7 + static_cast<int>(ai)) % kFlags] + "\", \"block_flag\": \"" + once + "\"}");
					continue;
				}
			}
			if (k == 3) {
				const std::string once = onceFlag();
				const std::string event = newEvent("\tset " + once + "\n\tvar stress.reactions += 1\n");
				hook("{\"id\": \"" + event + "\", \"trigger\": \"flag\", \"event\": \"" + event + "\", \"require_flag\": \"" +
					flags[static_cast<std::size_t>(made * 3 + static_cast<int>(ai) * 5) % kFlags] + "\", \"block_flag\": \"" + once + "\"}");
				continue;
			}
			if (k == 5) {
				const std::string event = newEvent("\tvar stress.loads += 1\n");
				hook("{\"id\": \"" + event + "\", \"trigger\": \"load\", \"event\": \"" + event + "\", \"require_flag\": \"" +
					flags[static_cast<std::size_t>(made + static_cast<int>(ai)) % kFlags] + "\"}");
				continue;
			}
			if (k == 6 && pillar < a.wallSlots.size()) {
				const Spot at = a.wallSlots[pillar++];
				if (!taken.count(at)) {
					taken.insert(at);
					sign(at, ", \"solid\": \"while_active\", \"require_flag\": \"" + flags[static_cast<std::size_t>(made * 11) % kFlags] + "\"");
					continue;
				}
			}
			if (k == 7 && bgSpawned < o.background && (a.busy ? bgSpawned < (o.background + 1) / 2 : true)) {
				const std::string event = id3("stress_bg", bgSpawned);
				hook("{\"id\": \"" + event + "_start\", \"trigger\": \"load\", \"event\": \"" + event + "\"}");
				++bgSpawned;
				continue;
			}
			// Nothing of that kind fits any more: a flag hook needs no cell.
			const std::string once = onceFlag();
			const std::string event = newEvent("\tset " + once + "\n");
			hook("{\"id\": \"" + event + "\", \"trigger\": \"flag\", \"event\": \"" + event + "\", \"require_flag\": \"" +
				flags[static_cast<std::size_t>(made * 13 + static_cast<int>(ai)) % kFlags] + "\", \"block_flag\": \"" + once + "\"}");
		}
	}
	for (const std::string& touch : wandererTouch) {
		const std::string areaId = touch.substr(0, touch.find(' '));
		const std::string actor = touch.substr(touch.find(' ') + 1);
		const std::string event = newEvent("\tnarrate @stress.line.001\n\tvar stress.touches += 1\n", "unskippable");
		const std::size_t ai = static_cast<std::size_t>(std::atoi(areaId.substr(7).c_str()));
		areas[ai].hooks.push_back("{\"id\": \"" + event + "\", \"trigger\": \"touch\", \"event\": \"" + event + "\", \"actor\": \"" + actor + "\"}");
	}

	// Background events: the first two move one of the busy area's standing actors between two posts, the rest count.
	std::ostringstream background;
	for (int b = 0; b < bgSpawned; ++b) {
		background << "event " << id3("stress_bg", b) << " background\n";
		if (b < 2 && b < static_cast<int>(busyStanding.size()) && b < (o.background + 1) / 2) {
			background << "\twhile not stress.bg_stop\n\t\tmove " << busyStanding[static_cast<std::size_t>(b)] << " to post_a pathfind\n\t\twait 1\n\t\tmove "
					   << busyStanding[static_cast<std::size_t>(b)] << " to post_b pathfind\n\t\twait 1\n\tend\n";
		} else {
			background << "\twhile not stress.bg_stop\n\t\twait 0.5\n\t\tvar stress.bg += 1\n\tend\n";
		}
		background << "end\n\n";
	}
	// Helper events the signs call, up to the event count.
	std::ostringstream helpers;
	for (int i = 0; i < helperCount; ++i) {
		helpers << "event " << id4("stress_x", i) << " skippable\n";
		if (i % 4 == 0) {
			helpers << "\tif " << flags[static_cast<std::size_t>(i) % kFlags] << "\n\t\tvar stress.calls += 2\n\telse\n\t\tvar stress.calls += 1\n\tend\n";
		} else {
			helpers << "\tvar stress.calls += 1\n";
		}
		helpers << "end\n\n";
	}

	// Areas.
	for (std::size_t ai = 0; ai < areas.size(); ++ai) {
		AreaPlan& a = areas[ai];
		std::vector<Spot> posts;
		for (const Spot& f : a.floorSlots) {
			if (posts.size() < 2 && f.x > a.midX && f.y < a.midY && !a.used.count(f)) {
				posts.push_back(f);
				a.used.insert(f);
			}
		}
		while (posts.size() < 2) {
			posts.push_back({a.midX + 1 + static_cast<int>(posts.size()), a.midY - 2});
		}
		const Spot postA = posts[0];
		const Spot postB = posts[1];
		if (ai > 0) {
			a.rows[a.midY][0] = '.';
		}
		if (ai + 1 < areas.size()) {
			a.rows[a.midY][a.width - 1] = '.';
		}
		std::ostringstream j;
		j << "{\n\t\"id\": \"" << a.id << "\",\n\t\"kind\": \"field\",\n\t\"encounter_mode\": \"visible\",\n\t\"step_ms\": " << (a.busy ? "133.5" : ai % 7 == 0 ? "212.5" : "250")
		  << ",\n\t\"music\": \"field_bell_shore\",\n\t\"region\": \"stress\",\n";
		if (ai % 25 == 0) {
			j << "\t\"name\": [{\"flag\": \"" << flags[ai % kFlags] << "\", \"key\": \"area.stress_lit\"}, {\"key\": \"area." << a.id << "\"}],\n";
		}
		// A pond with a platform in the busy area and every thirtieth.
		const bool pond = ai % 30 == 0;
		if (pond) {
			for (int x = 3; x <= 9; ++x) {
				a.rows[a.height - 3][x] = '~';
			}
		}
		// A cell a flag opens on each pillar of the first row, and a conveyor under the north wall.
		std::vector<std::string> changes;
		for (std::size_t p = 0; p < a.wallSlots.size() && p < 4; ++p) {
			changes.push_back("{\"flag\": \"" + flags[(ai * 3 + p) % kFlags] + "\", \"tile\": \".\", \"cells\": [" + cell(a.wallSlots[p]) + "]}");
		}
		j << "\t\"map\": [\n";
		for (std::size_t y = 0; y < a.rows.size(); ++y) {
			j << "\t\t\"" << a.rows[y] << "\"" << (y + 1 < a.rows.size() ? ",\n" : "\n");
		}
		j << "\t],\n\t\"markers\": {\"west_gate\": " << cell({1, a.midY}) << ", \"east_gate\": " << cell({a.width - 2, a.midY}) << ", \"center\": " << cell({a.midX, a.midY})
		  << ", \"north_end\": " << cell({a.midX, 1}) << ", \"south_end\": " << cell({a.midX, a.height - 2}) << ", \"nw\": " << cell({1, 1}) << ", \"se\": "
		  << cell({a.width - 2, a.height - 2}) << ", \"post_a\": " << cell(postA) << ", \"post_b\": " << cell(postB) << "},\n";
		j << "\t\"exits\": [";
		std::vector<std::string> exits;
		if (ai > 0) {
			exits.push_back("{\"at\": " + cell({0, a.midY}) + ", \"to\": \"" + areas[ai - 1].id + "\", \"marker\": \"east_gate\"}");
		}
		if (ai + 1 < areas.size()) {
			exits.push_back("{\"at\": " + cell({a.width - 1, a.midY}) + ", \"to\": \"" + areas[ai + 1].id + "\", \"marker\": \"west_gate\"" +
				(ai % 4 == 0 ? ", \"block_flag\": \"stress.never\"" : "") + "}");
		}
		for (std::size_t e = 0; e < exits.size(); ++e) {
			j << (e ? ", " : "") << exits[e];
		}
		j << "],\n\t\"hooks\": [\n";
		for (std::size_t h = 0; h < a.hooks.size(); ++h) {
			j << "\t\t" << a.hooks[h] << (h + 1 < a.hooks.size() ? ",\n" : "\n");
		}
		j << "\t],\n";
		if (a.busy) {
			j << "\t\"rests\": [{\"id\": \"stress_inn\", \"at\": " << cell({a.midX + 1, a.midY - 1}) << ", \"cost\": 5}],\n";
			j << "\t\"shops\": [{\"id\": \"stress_shop\", \"at\": " << cell({a.midX - 1, a.midY - 1}) << ", \"stock\": [\"tonic\"]}],\n";
			std::vector<Spot> spawns;
			for (const Spot& h : a.homes) {
				const Spot s{h.x + 1, h.y - 1};
				if (h.x >= a.midX + 3 && h.y >= 7 && h.y <= a.midY - 3 && s.x + 2 <= a.width - 3 && !a.used.count(s) && a.rows[s.y][s.x] == '.' && spawns.size() < 6) {
					spawns.push_back(s);
				}
			}
			if (!spawns.empty()) {
				j << "\t\"zones\": [{\"id\": \"stress_pests\", \"from\": " << cell({a.midX + 3, 3}) << ", \"to\": " << cell({a.width - 3, a.midY - 3})
				  << ", \"table\": [{\"encounter\": \"training\", \"weight\": 1}], \"visible\": {\"spawns\": [";
				for (std::size_t s = 0; s < spawns.size(); ++s) {
					j << (s ? ", " : "") << cell(spawns[s]);
				}
				j << "], \"cap\": " << spawns.size() << ", \"detection_radius\": 0, \"leash_radius\": 2, \"wander_percent\": 50}}],\n";
			}
		}
		if (pond) {
			j << "\t\"platforms\": [{\"id\": \"" << a.id << "_raft\", \"from\": " << cell({3, a.height - 3}) << ", \"to\": " << cell({9, a.height - 3})
			  << ", \"step_ms\": 200, \"pause_ms\": 500}],\n";
		}
		if (!changes.empty()) {
			j << "\t\"changes\": [";
			for (std::size_t c = 0; c < changes.size(); ++c) {
				j << (c ? ", " : "") << changes[c];
			}
			j << "],\n";
		}
		j << "\t\"conveyors\": [{\"cells\": [" << cell({a.width - 9, 2}) << ", " << cell({a.width - 8, 2}) << ", " << cell({a.width - 7, 2}) << "], \"direction\": \"east\", \"step_ms\": "
		  << (ai % 2 ? "180" : "212.5") << (ai % 3 == 0 ? ", \"require_flag\": \"" + flags[(ai * 5) % kFlags] + "\"" : "") << "}]\n}\n";
		std::string problem;
		if (!game::writeFileAtomic(data + "/areas/" + a.id + ".json", j.str(), problem)) {
			return problem;
		}
	}

	// Events and strings.
	std::string problem;
	if (!game::writeFileAtomic(data + "/events/stress.ev", "# Synthetic events for timing (make_stress_data).\n\n" + events.str() + background.str() + helpers.str(), problem)) {
		return problem;
	}
	std::string config;
	if (!game::readFile(data + "/game.json", config)) {
		return "cannot read " + data + "/game.json";
	}
	std::string areaFiles;
	std::string flagList;
	std::string nameless;
	for (const AreaPlan& a : areas) {
		areaFiles += (areaFiles.empty() ? "" : ", ") + std::string("\"areas/") + a.id + ".json\"";
	}
	for (const std::string& f : flags) {
		flagList += "\"" + f + "\", ";
	}
	for (const std::string& id : actorIds) {
		nameless += "\"" + id + "\", ";
	}
	std::string music;
	for (std::size_t ai = 0; ai < areas.size(); ai += 10) {
		music += "\"" + areas[ai].id + "\": [{\"flag\": \"" + flags[(ai + 1) % kFlags] + "\", \"track\": \"town_tide_bells\"}, \"field_bell_shore\"], ";
	}
	const std::string actorsText = actors.str();
	const bool patched = actor_data::insertAfter(config, "{", "\n\t\"step_timing\": \"" + o.timing + "\",\n\t\"background_limit\": 8,\n\t\"cell_pixels\": 16,") &&
		actor_data::appendToList(config, "areas", areaFiles) && actor_data::appendToList(config, "events", "\"events/stress.ev\"") &&
		actor_data::insertAfter(config, "\"nameless_actors\": [", nameless) && actor_data::insertAfter(config, "\"actors\": [", actorsText) &&
		actor_data::insertAfter(config, "\"flags\": [", flagList + "\"stress.bg_stop\", \"stress.never\", ") &&
		actor_data::insertAfter(config, "\"vars\": {", "\"stress.bg\": 0, \"stress.calls\": 0, \"stress.loads\": 0, \"stress.reactions\": 0, \"stress.steps\": 0, \"stress.touches\": 0, ") &&
		actor_data::insertAfter(config, "\"music\": {", "\n\t\t\t\"areas\": {" + music.substr(0, music.size() - 2) + "},");
	if (!patched || !game::writeFileAtomic(data + "/game.json", config, problem)) {
		return "cannot patch game.json " + problem;
	}
	for (const std::string code : {"en", "ja"}) {
		std::string text;
		std::string strings;
		const std::string table = data + "/text/" + code;
		std::string names = code == "en" ? "\n\t\t\"area.stress_lit\": \"Lit Hall\"," : "\n\t\t\"area.stress_lit\": \"あかるい ひろま\",";
		for (const AreaPlan& a : areas) {
			names += "\n\t\t\"area." + a.id + "\": \"" + (code == "en" ? "Hall " : "ひろま ") + a.id.substr(7) + "\",";
		}
		std::string lines = "\n";
		for (int i = 0; i < std::min(kLines, std::max(2, signCount)); ++i) {
			lines += id3("stress.line.", i) + " = " + (code == "en" ? kLinesEn[i % 10] : kLinesJa[i % 10]) + "\n";
		}
		if (!game::readFile(table + ".json", text) || !game::readFile(table + ".strings", strings) || !actor_data::insertAfter(text, "\"text\": {", names) ||
			!game::writeFileAtomic(table + ".json", text, problem) || !game::writeFileAtomic(table + ".strings", strings + lines, problem)) {
			return "cannot add the " + code + " text " + problem;
		}
	}

	// The script: weighted toward long walks among many actors, area changes with autosaves, signs (events that set
	// flags, so an autosave at each end), a rest, battles played with a named macro, saves and loads.
	std::ostringstream s;
	s << "# Written by make_stress_data. Run with --seed 6 on " << out << "/game/data.\n";
	s << "new\ndev money 5000\ndev level fighter 40\nmenu\nmacro create stress\nmacro step stress 0 fighter attack\nclose\n";
	s << "autobattle on stress\ndev warp " << busy.id << " west_gate\n";
	const auto talkTo = [&](const AreaPlan& a, Spot at) {
		const bool top = at.y == 0;
		s << "walk " << at.x << " " << (top ? 1 : a.height - 2) << "\nface " << (top ? "north" : "south") << "\ntalk\n";
	};
	for (int round = 0; round < o.rounds; ++round) {
		s << "walk east_gate\nwalk north_end\nwalk south_end\nwalk west_gate\nwalk center\nwalk nw\nwalk se\nwalk center\n";
		for (std::size_t k = static_cast<std::size_t>(round); k < signSpots[0].size(); k += signSpots[0].size() / 8 + 1) {
			talkTo(busy, signSpots[0][k]);
		}
		talkTo(busy, {busy.midX + 2, 0});
		s << "walk " << busy.midX + 1 << " " << busy.midY << "\nface north\ntalk\nrest\n";
		s << "walk " << busy.midX - 1 << " " << busy.midY << "\nface north\ntalk\nbuy tonic 1\nleave\n";
		if (!battleEvents.empty()) {
			std::istringstream b(battleEvents[static_cast<std::size_t>(round) % battleEvents.size()]);
			std::string id;
			Spot at;
			b >> id >> at.x >> at.y;
			if (round % 2 == 1) {
				s << "autobattle off\n";
				talkTo(busy, at);
				s << "fight stress\nautobattle on stress\n";
			} else {
				talkTo(busy, at);
			}
		}
		s << "wait 3000\nmenu\nsave manual_1\nclose\n";
		if (round == 0) {
			// The tour: east through the chain, crossing each area along its corridor and talking to its sign.
			for (int t = 0; t < o.tour; ++t) {
				const AreaPlan& a = areas[static_cast<std::size_t>(t)];
				const AreaPlan& next = areas[static_cast<std::size_t>(t) + 1];
				s << "walk east_gate\ne\n";
				talkTo(next, {next.midX + 2, 0});
				if (t % 10 == 9) {
					s << "dev warp " << areas[areas.size() - 1 - static_cast<std::size_t>(t)].id << " center\nwalk se\nwalk nw\ndev warp " << next.id << " west_gate\n";
					s << "menu\nsave manual_2\nclose\ntitle\nload manual_2\n";
				}
				(void)a;
			}
			s << "dev warp " << busy.id << " west_gate\n";
		}
	}
	s << "title\nload manual_1\nwalk east_gate\nstatus\n";
	if (!game::writeFileAtomic(out + "/stress.txt", s.str(), problem)) {
		return problem;
	}
	summary = std::to_string(areas.size()) + " areas, " + std::to_string(actorIds.size()) + " actors, " + std::to_string(o.hooks) + " hooks wanted, " +
		std::to_string(eventCount + helperCount + bgSpawned) + " events, " + std::to_string(bgSpawned) + " background events, " + std::to_string(flags.size()) + " flags";
	return "";
}

}
