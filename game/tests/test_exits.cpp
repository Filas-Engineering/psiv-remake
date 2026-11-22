#include "test.h"

#include "game/check.h"
#include "game/session.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace game;

namespace {

// A 2x2 exit block (to Tidewater's east gate) with a way in on its north-west cell, a dead end of floor west of it and an
// `always` exit east of it (to Tidewater's start).
const char* kBlock = R"({"id": "exit_lab", "kind": "field", "map": ["######", "#....#", "#...##", "######"],
	"markers": {"pad": [2, 1]},
	"exits": [
		{"at": [2, 1], "to": "tidewater", "marker": "east_gate"}, {"at": [3, 1], "to": "tidewater", "marker": "east_gate"},
		{"at": [2, 2], "to": "tidewater", "marker": "east_gate"}, {"at": [3, 2], "to": "tidewater", "marker": "east_gate"},
		{"at": [4, 1], "to": "tidewater", "marker": "start", "fires": "always"}]})";

// A pit beside an exit, a gated exit beside another, a belt that runs onto an exit, and a platform that docks beside one.
const char* kMechanisms = R"({"id": "exit_works", "kind": "field", "map": ["#########", "#.......#", "#.......#", "#.......#", "#.......#", "#.~~....#", "#########"],
	"markers": {"on_e": [4, 1], "on_e2": [4, 2], "west": [1, 3], "dock_side": [1, 5]},
	"exits": [
		{"at": [3, 1], "to": "tidewater", "marker": "start", "fall": true}, {"at": [4, 1], "to": "tidewater", "marker": "east_gate"},
		{"at": [3, 2], "to": "tidewater", "marker": "start", "require_flag": "lab.open"}, {"at": [4, 2], "to": "tidewater", "marker": "east_gate"},
		{"at": [4, 4], "to": "tidewater", "marker": "east_gate"}, {"at": [4, 5], "to": "tidewater", "marker": "start"}],
	"conveyors": [{"cells": [[2, 4], [3, 4]], "direction": "east", "step_ms": 100}],
	"platforms": [{"id": "float", "from": [2, 5], "to": [3, 5], "step_ms": 100, "pause_ms": 0, "mode": "triggered"}]})";

GameData withAreas(std::vector<const char*> texts) {
	GameData data = test::data();
	for (const char* text : texts) {
		rules::Result<Area> parsed = parseArea("lab.json", text);
		CHECK(parsed.ok());
		if (parsed.ok()) {
			data.areas[parsed.value->id] = *parsed.value;
			data.areas[parsed.value->id].file = std::string(parsed.value->id) + ".json";
			data.config.areaFiles.push_back(parsed.value->id + ".json");
		}
	}
	return data;
}

void startIn(Session& s, const std::string& area, const std::string& marker) {
	CHECK(s.newGame().empty());
	test::confirmLines(s);
	CHECK(s.devWarp(area, marker).empty());
	s.drainOutput();
}

}

// Arriving on a cell of a multi-cell exit, the party walks across the block without leaving; it leaves only by stepping
// back onto the block from the dead end beside it.
TEST(anExitTakesTheLeaderOnlyFromACellThatIsNoExit) {
	const GameData data = withAreas({kBlock});
	Session s(data, SessionOptions());
	startIn(s, "exit_lab", "pad");
	for (Facing f : {Facing::East, Facing::South, Facing::West}) {
		CHECK(s.walk(f).empty());
		CHECK_EQ(s.world().area, std::string("exit_lab"));
	}
	CHECK(s.world().position == (Cell{2, 2}));
	CHECK(s.walk(Facing::West).empty());
	CHECK(s.world().area == "exit_lab" && s.world().position == (Cell{1, 2}));
	CHECK(s.walk(Facing::East).empty());
	CHECK_EQ(s.world().area, std::string("tidewater"));
	CHECK(s.world().position == s.data().area("tidewater")->markers.at("east_gate"));
}

TEST(anAlwaysExitTakesTheLeaderFromAnotherExitCell) {
	const GameData data = withAreas({kBlock});
	Session s(data, SessionOptions());
	startIn(s, "exit_lab", "pad");
	CHECK(s.walk(Facing::East).empty());
	CHECK_EQ(s.world().area, std::string("exit_lab"));
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.world().area == "tidewater" && s.world().position == s.data().area("tidewater")->markers.at("start"));
}

// game.json's exit_fires `always` keeps every step onto an exit taking it, the arrival cell aside.
TEST(exitFiresAlwaysTakesEveryStepOntoAnExit) {
	GameData data = withAreas({kBlock});
	data.config.exitFires = ExitFires::Always;
	Session s(data, SessionOptions());
	startIn(s, "exit_lab", "pad");
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.world().area == "tidewater" && s.world().position == s.data().area("tidewater")->markers.at("east_gate"));
}

// The path walk crosses the block from the way in, but treats a cell a step would leave by as a goal only.
TEST(thePathWalkCrossesExitCellsThatDoNotFire) {
	const GameData data = withAreas({kBlock});
	Session s(data, SessionOptions());
	startIn(s, "exit_lab", "pad");
	PathWalk walk = s.beginWalk({3, 2});
	WalkStatus status = WalkStatus::Walking;
	while (status == WalkStatus::Walking) {
		status = s.walkStep(walk);
	}
	CHECK(status == WalkStatus::Arrived && s.world().area == "exit_lab");
	walk = s.beginWalk({1, 1});
	while ((status = s.walkStep(walk)) == WalkStatus::Walking) {
	}
	CHECK(status == WalkStatus::Arrived && s.world().position == (Cell{1, 1}));
	walk = s.beginWalk({3, 2});
	CHECK(s.walkStep(walk) == WalkStatus::NoPath);
	walk = s.beginWalk({2, 1});
	while ((status = s.walkStep(walk)) == WalkStatus::Walking) {
	}
	CHECK(status == WalkStatus::LeftArea && s.world().area == "tidewater");
}

// Aboard, the vehicle follows the same rule.
TEST(aVehicleCrossesAnExitBlockItArrivedOn) {
	const GameData data = withAreas({kBlock});
	Session s(data, SessionOptions());
	startIn(s, "exit_lab", "pad");
	VehiclePlace place;
	place.area = "exit_lab";
	place.cell = {2, 1};
	s.world().vehicles["reed_skimmer"] = place;
	s.world().aboard = "reed_skimmer";
	CHECK(s.walk(Facing::South).empty());
	CHECK(s.world().area == "exit_lab" && s.world().position == (Cell{2, 2}));
	CHECK(s.walk(Facing::West).empty());
	CHECK(s.walk(Facing::East).empty());
	CHECK_EQ(s.world().area, std::string("tidewater"));
}

// A pit drops the party from wherever it is stepped on, an exit cell too.
TEST(aPitFiresFromAnExitCell) {
	const GameData data = withAreas({kMechanisms});
	Session s(data, SessionOptions());
	startIn(s, "exit_works", "on_e");
	CHECK(s.walk(Facing::West).empty());
	CHECK(s.world().area == "tidewater" && s.world().position == s.data().area("tidewater")->markers.at("start"));
	CHECK(test::outputHas(s.drainOutput(), "floor gives way"));
}

// A gated exit is a wall while closed; open, it follows the rule: crossed from the exit beside it, taken from floor.
TEST(aGatedExitFollowsTheRuleOnceOpen) {
	const GameData data = withAreas({kMechanisms});
	Session s(data, SessionOptions());
	startIn(s, "exit_works", "on_e2");
	CHECK(!s.canStep(Facing::West));
	s.world().flags["lab.open"] = true;
	CHECK(s.walk(Facing::West).empty());
	CHECK(s.world().area == "exit_works" && s.world().position == (Cell{3, 2}));
	CHECK(s.walk(Facing::South).empty());
	CHECK(s.walk(Facing::North).empty());
	CHECK(s.world().area == "tidewater" && s.world().position == s.data().area("tidewater")->markers.at("start"));
}

// A belt carries the party as a step from the belt's cell: onto an exit from floor, it takes it.
TEST(aBeltRunningOntoAnExitTakesIt) {
	const GameData data = withAreas({kMechanisms});
	Session s(data, SessionOptions());
	startIn(s, "exit_works", "west");
	CHECK(s.walk(Facing::South).empty());
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.world().position == (Cell{2, 4}));
	FieldInput none;
	for (int i = 0; i < 20 && s.world().area == "exit_works"; ++i) {
		s.update(50, none);
	}
	CHECK(s.world().area == "tidewater" && s.world().position == s.data().area("tidewater")->markers.at("east_gate"));
}

// Stepping off a platform docked beside an exit takes the exit: the platform's cell is no exit.
TEST(steppingOffAPlatformOntoAnExitTakesIt) {
	const GameData data = withAreas({kMechanisms});
	Session s(data, SessionOptions());
	startIn(s, "exit_works", "dock_side");
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.ridingPlatform() == "float");
	s.update(1000);
	CHECK(s.world().position == (Cell{3, 5}));
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.world().area == "tidewater" && s.world().position == s.data().area("tidewater")->markers.at("start"));
}

// The data check follows the rule: a block arrived on counts as used from its dead end, and its far cells, which only
// other exit cells touch, are reached as part of it.
TEST(theDataCheckReachesExitsByTheRule) {
	GameData data = withAreas({kBlock});
	Exit in;
	in.at = data.areas.at("tidewater").markers.at("east_gate");
	in.toArea = "exit_lab";
	in.toMarker = "pad";
	data.areas.at("tidewater").exits.push_back(in);
	const auto unreached = [](const GameData& d) {
		const std::vector<rules::Problem> found = crossCheck(d, true);
		return std::count_if(found.begin(), found.end(), [](const rules::Problem& p) { return p.file == "exit_lab.json" && p.message.key == "check.exit_not_reached"; });
	};
	CHECK_EQ(unreached(data), 0);
	data.areas.at("exit_lab").rows[1][1] = '#';
	data.areas.at("exit_lab").rows[2][1] = '#';
	CHECK_EQ(unreached(data), 4);
}

// Followers walk the leader's trail, stand on the leader's cell after an entry (or behind it under `behind` for entries
// that are no walk through an exit, by followers_arrive_other), are saved and block nothing.
TEST(followersWalkTheLeadersTrail) {
	GameData data = withAreas({kMechanisms});
	Session s(data, SessionOptions());
	startIn(s, "exit_works", "west");
	const std::vector<std::string> lineup = s.world().lineup;
	s.world().lineup = {"fighter", "medic", "scout"};
	s.world().follower = "peddler";
	std::vector<FollowerView> f = s.followers();
	CHECK(f.size() == 3 && f[0].id == "medic" && f[2].id == "peddler");
	CHECK(std::all_of(f.begin(), f.end(), [&](const FollowerView& v) { return v.cell == s.world().position; }));
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.walk(Facing::East).empty());
	f = s.followers();
	CHECK(f[0].cell == (Cell{2, 3}) && f[1].cell == (Cell{1, 3}) && f[2].cell == (Cell{1, 3}));
	CHECK(s.walk(Facing::West).empty());
	CHECK(s.world().position == (Cell{2, 3}));
	s.world().lineup = lineup;
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("\ntrail 3 3 2 3 1 3\n") != std::string::npos);
	Session loaded(data, SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK(!loaded.followers().empty() && loaded.followers()[0].cell == (Cell{3, 3}));
	data.config.followersArrive = FollowersArrive::Behind;
	Session stacked(data, SessionOptions());
	CHECK(stacked.newGame().empty());
	test::confirmLines(stacked);
	CHECK(stacked.devWarpCell("exit_works", {3, 3}).empty());
	CHECK(stacked.world().trail.empty());
	data.config.followersArriveOther = FollowersArrive::Behind;
	Session behind(data, SessionOptions());
	CHECK(behind.newGame().empty());
	test::confirmLines(behind);
	behind.world().facing = Facing::West;
	CHECK(behind.devWarpCell("exit_works", {3, 3}).empty());
	behind.world().lineup = {"fighter", "medic", "scout"};
	f = behind.followers();
	CHECK(f.size() == 2 && f[0].cell == (Cell{4, 3}) && f[1].cell == (Cell{5, 3}));
}

// The exit walked through places the followers: its own `followers`, else game.json's followers_arrive; a warp does not
// use it and a load restores the saved trail. A cell that cannot be walked ends the line, and the rest stand on its
// last cell.
TEST(theExitTakenPlacesTheFollowers) {
	std::string room = R"({"id": "follow_lab", "kind": "field", "map": ["#######", "#.....#", "#.....#", "#.....#", "#######"],
		"markers": {"middle": [3, 2], "west": [1, 2]},
		"exits": [{"at": [5, 1], "to": "follow_lab", "marker": "middle", "followers": "up"},
			{"at": [5, 3], "to": "follow_lab", "marker": "middle", "followers": "behind"},
			{"at": [1, 1], "to": "follow_lab", "marker": "west"},
			{"at": [1, 3], "to": "follow_lab", "marker": "middle", "followers": "left"}]})";
	GameData data = withAreas({room.c_str()});
	Session s(data, SessionOptions());
	startIn(s, "follow_lab", "middle");
	const std::vector<std::string> lineup = s.world().lineup;
	s.world().lineup = {"fighter", "medic", "scout"};
	const auto cells = [&] {
		std::vector<Cell> out;
		for (const FollowerView& f : s.followers()) {
			out.push_back(f.cell);
		}
		return out;
	};
	CHECK(s.world().trail.empty());
	// East, then north onto the `up` exit: the line goes north of the leader, one cell, then stacks there at the wall.
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.walk(Facing::North).empty());
	CHECK(s.world().position == (Cell{3, 2}));
	CHECK(cells() == (std::vector<Cell>{{3, 1}, {3, 1}}));
	// South onto the `behind` exit: the leader arrives facing south, so the line goes north.
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.walk(Facing::South).empty());
	CHECK(s.world().position == (Cell{3, 2}) && s.world().facing == Facing::South);
	CHECK(cells() == (std::vector<Cell>{{3, 1}, {3, 1}}));
	// The `left` exit: two cells west.
	CHECK(s.walk(Facing::West).empty());
	CHECK(s.walk(Facing::West).empty());
	CHECK(s.walk(Facing::South).empty());
	CHECK(cells() == (std::vector<Cell>{{2, 2}, {1, 2}}));
	// An exit without its own: game.json's followers_arrive, stacked by default.
	CHECK(s.walk(Facing::West).empty());
	CHECK(s.walk(Facing::North).empty());
	CHECK(s.walk(Facing::West).empty());
	CHECK(s.world().position == (Cell{1, 2}) && s.world().trail.empty());
	// A warp uses followers_arrive_other, stacked by default, whatever the exits say.
	CHECK(s.walk(Facing::South).empty());
	CHECK(!s.world().trail.empty());
	CHECK(s.devWarp("follow_lab", "middle").empty());
	CHECK(s.world().trail.empty());
	// A load restores the trail the save holds, placed by the exit.
	CHECK(s.walk(Facing::West).empty());
	CHECK(s.walk(Facing::West).empty());
	CHECK(s.walk(Facing::South).empty());
	const std::vector<Cell> placed = cells();
	CHECK(placed == (std::vector<Cell>{{2, 2}, {1, 2}}));
	s.world().lineup = lineup;
	std::string text;
	CHECK(s.saveText(text).empty());
	Session loaded(data, SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK(loaded.world().trail == s.world().trail);
	data.config.followersArrive = FollowersArrive::Right;
	Session right(data, SessionOptions());
	startIn(right, "follow_lab", "middle");
	right.world().lineup = {"fighter", "medic"};
	CHECK(right.walk(Facing::West).empty());
	CHECK(right.walk(Facing::North).empty());
	CHECK(right.walk(Facing::West).empty());
	CHECK(right.world().position == (Cell{1, 2}) && right.followers()[0].cell == (Cell{2, 2}));
}

// Under followers_on_exits the line places followers on exit cells (a block, a pit, an `always` exit) and goes on beyond
// them, and nothing fires; without it the line ends before the first. The trail crosses the block the leader walks over
// either way, and a follower blocks nothing.
TEST(followersOnExitsPlacesFollowersOnExitCells) {
	const char* room = R"({"id": "follow_room", "kind": "field", "map": ["#######", "#.....#", "#.....#", "#.....#", "#.....#", "#######"],
		"markers": {"top": [3, 1], "block": [2, 2], "corner": [1, 3]},
		"exits": [{"at": [2, 2], "to": "tidewater", "marker": "east_gate"}, {"at": [3, 2], "to": "tidewater", "marker": "east_gate"},
			{"at": [3, 3], "to": "tidewater", "marker": "start", "fall": true},
			{"at": [4, 2], "to": "tidewater", "marker": "start", "fires": "always"},
			{"at": [1, 4], "to": "follow_room", "marker": "top", "followers": "down"},
			{"at": [4, 4], "to": "follow_room", "marker": "block", "followers": "behind"}]})";
	GameData data = withAreas({room});
	Session today(data, SessionOptions());
	startIn(today, "follow_room", "corner");
	today.world().lineup = {"fighter", "medic"};
	CHECK(today.walk(Facing::South).empty());
	CHECK(today.world().position == (Cell{3, 1}) && today.world().trail.empty());
	data.config.followersOnExits = true;
	Session s(data, SessionOptions());
	startIn(s, "follow_room", "corner");
	const std::vector<std::string> lineup = s.world().lineup;
	s.world().lineup = {"fighter", "medic"};
	const auto cells = [&] {
		std::vector<Cell> out;
		for (const FollowerView& f : s.followers()) {
			out.push_back(f.cell);
		}
		return out;
	};
	CHECK(s.walk(Facing::South).empty());
	CHECK(s.world().area == "follow_room" && s.world().position == (Cell{3, 1}));
	CHECK(cells() == (std::vector<Cell>{{3, 2}}));
	s.world().lineup = {"fighter", "medic", "scout"};
	s.world().follower = "peddler";
	CHECK(cells() == (std::vector<Cell>{{3, 2}, {3, 3}, {3, 4}}));
	for (Facing f : {Facing::East, Facing::East, Facing::South, Facing::South, Facing::South, Facing::West}) {
		CHECK(s.walk(f).empty());
	}
	CHECK(s.world().position == (Cell{2, 2}) && s.world().facing == Facing::West);
	CHECK(cells() == (std::vector<Cell>{{3, 2}, {4, 2}, {5, 2}}));
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.walk(Facing::North).empty());
	CHECK(s.world().area == "follow_room" && s.world().position == (Cell{3, 1}));
	CHECK(cells() == (std::vector<Cell>{{3, 2}, {2, 2}, {3, 2}}));
	s.world().follower.clear();
	s.world().lineup = lineup;
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("\ntrail 3 2 2 2 3 2 4 2 5 2\n") != std::string::npos);
	Session loaded(data, SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK(loaded.world().trail == s.world().trail);
	CHECK(s.walk(Facing::South).empty());
	CHECK(s.world().area == "tidewater" && s.world().position == s.data().area("tidewater")->markers.at("east_gate"));
}

// An area's arrive_notice by the area the party came from: the first entry listing it, else the entry without `from`.
TEST(theArrivalNoticeDependsOnWhereThePartyCameFrom) {
	std::string text = kBlock;
	text.insert(text.find("\"map\""), R"("arrive_notice": [{"from": ["tidewater"], "notice": false}, {"notice": true}], )");
	const GameData data = withAreas({text.c_str()});
	Session s(data, SessionOptions());
	startIn(s, "exit_lab", "pad");
	CHECK(!s.arrivalAnnounced());
	CHECK(s.devWarp("reed_path", "west_gate").empty());
	CHECK(s.devWarp("exit_lab", "pad").empty());
	CHECK(s.arrivalAnnounced());
	CHECK(s.devWarp("exit_lab", "pad").empty());
	CHECK(s.arrivalAnnounced());
}

// A town's registration as a place to return to by the area the party came from (arrival_from, arrival_not_from).
TEST(aTownRegistersOnlyWhenEnteredFromAListedArea) {
	std::string from = kBlock;
	from.insert(from.find("\"map\""), R"("arrival": "pad", "arrival_from": ["reed_path"], )");
	from.replace(from.find("\"field\""), 7, "\"town\"");
	GameData data = withAreas({from.c_str()});
	Session s(data, SessionOptions());
	startIn(s, "exit_lab", "pad");
	CHECK(!s.world().visitedAreas.count("exit_lab"));
	CHECK(s.devWarp("reed_path", "west_gate").empty());
	CHECK(s.devWarp("exit_lab", "pad").empty());
	CHECK(s.world().visitedAreas.count("exit_lab"));
	data.areas.at("exit_lab").arrivalFrom.clear();
	data.areas.at("exit_lab").arrivalNotFrom = {"tidewater"};
	Session t(data, SessionOptions());
	startIn(t, "exit_lab", "pad");
	CHECK(!t.world().visitedAreas.count("exit_lab"));
	CHECK(t.devWarp("reed_path", "west_gate").empty());
	CHECK(t.devWarp("exit_lab", "pad").empty());
	CHECK(t.world().visitedAreas.count("exit_lab"));
	std::string missing = from;
	missing.replace(missing.find(R"("arrival": "pad", )"), 18, "");
	CHECK(!parseArea("bad.json", missing).ok());
}
