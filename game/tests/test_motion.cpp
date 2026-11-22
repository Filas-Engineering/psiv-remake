#include "test.h"

#include "game/check.h"
#include "game/save.h"
#include "game/session.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace game;

namespace {

void inMill(Session& s) {
	CHECK(s.newGame().empty());
	test::confirmLines(s);
	CHECK(s.devWarp("wren_mill", "mill_door").empty());
	s.drainOutput();
}

ActorView actorOf(const Session& s, const std::string& id) {
	for (const ActorView& a : s.actorsHere()) {
		if (a.id == id) {
			return a;
		}
	}
	return {};
}

PlatformView platformOf(const Session& s, const std::string& id) {
	for (const PlatformView& p : s.platformsHere()) {
		if (p.id == id) {
			return p;
		}
	}
	return {};
}

// A copy of the motion data with one file's text replaced in place of a fragment; the check report on it.
CheckReport checkWith(const std::string& name, const std::string& file, const std::vector<std::pair<std::string, std::string>>& edits) {
	namespace fs = std::filesystem;
	const std::string root = test::freshDirectory(name);
	std::error_code error;
	fs::copy(fs::path(test::motionDataDirectory()).parent_path().parent_path(), root, fs::copy_options::recursive, error);
	std::string text;
	CHECK(!error && readFile(root + "/game/data/" + file, text));
	for (const auto& [from, to] : edits) {
		const std::size_t at = text.find(from);
		CHECK(at != std::string::npos);
		text.replace(at, from.size(), to);
	}
	std::string problem;
	CHECK(writeFileAtomic(root + "/game/data/" + file, text, problem));
	return checkData(DataDirectories{root + "/game/data", root + "/core/data"});
}

CheckReport checkWith(const std::string& name, const std::string& file, const std::string& from, const std::string& to) {
	return checkWith(name, file, std::vector<std::pair<std::string, std::string>>{{from, to}});
}

bool reports(const CheckReport& report, const std::string& key) {
	return std::any_of(report.problems.begin(), report.problems.end(), [&](const rules::Problem& p) { return p.message.key == key; });
}

}

// A wanderer with `axis` x keeps its row, and with stand_percent spends some turns standing.
TEST(aWandererAlongXKeepsItsRowAndSometimesStands) {
	Session s(test::motionData(), SessionOptions());
	inMill(s);
	int moves = 0;
	Cell last = actorOf(s, "mill_cat").cell;
	for (int i = 0; i < 40; ++i) {
		s.update(200);
		const Cell now = actorOf(s, "mill_cat").cell;
		CHECK_EQ(now.y, 1);
		moves += now != last;
		last = now;
	}
	CHECK(moves > 0 && moves < 40);
}

// A patrol walks its loop, waits while its next cell is taken, and a save keeps where it is on the route.
TEST(aPatrolWalksItsLoopWaitsForABlockedCellAndIsSaved) {
	Session s(test::motionData(), SessionOptions());
	inMill(s);
	CHECK(actorOf(s, "mill_hand").cell == (Cell{4, 4}) && actorOf(s, "mill_hand").patrols);
	s.world().position = {4, 3};
	s.update(1000);
	CHECK(actorOf(s, "mill_hand").cell == (Cell{4, 4}));
	s.world().position = {2, 4};
	s.update(250);
	CHECK(actorOf(s, "mill_hand").cell == (Cell{4, 3}));
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("\npatrol mill_hand 0 1\n") != std::string::npos);
	s.update(250);
	CHECK(actorOf(s, "mill_hand").cell == (Cell{4, 2}) && actorOf(s, "mill_hand").facing == Facing::East);
	s.update(400 + 500);
	CHECK(actorOf(s, "mill_hand").cell == (Cell{4, 4}) && actorOf(s, "mill_hand").facing == Facing::North);
	Session loaded(test::motionData(), SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK(actorOf(loaded, "mill_hand").cell == (Cell{4, 3}));
	loaded.update(250);
	CHECK(actorOf(loaded, "mill_hand").cell == (Cell{4, 2}));
}

// A path actor moves by pixel offsets from its home, one entry per `ticks` field ticks; its footprint blocks the cells it
// overlaps, and the save keeps the tick.
TEST(aPathMovesByPixelsItsFootprintBlocksAndItIsSaved) {
	Session s(test::motionData(), SessionOptions());
	inMill(s);
	ActorView wheel = actorOf(s, "mill_wheel");
	CHECK(wheel.onPath && wheel.pathX == 0 && wheel.pathY == 0 && wheel.home == (Cell{11, 4}) && wheel.footprintWidth == 1);
	s.update(100);
	wheel = actorOf(s, "mill_wheel");
	CHECK(wheel.pathX == 4 && wheel.pathY == -2 && wheel.cell == (Cell{11, 3}));
	s.world().position = {10, 4};
	CHECK(!s.canStep(Facing::East));
	s.world().position = {10, 3};
	CHECK(!s.canStep(Facing::East));
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("\npath mill_wheel 2\n") != std::string::npos);
	Session loaded(test::motionData(), SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK(actorOf(loaded, "mill_wheel").pathX == 4);
}

// The triggered raft waits docked, carries the party when it steps on (held input waiting), docks and lets it off; no
// battle rolls while it rides, even where every step would start one.
TEST(aPlatformCarriesThePartyAndRollsNoBattleOnTheWay) {
	GameData data = test::motionData();
	Area& mill = data.areas.at("wren_mill");
	mill.kind = AreaKind::Field;
	Zone zone;
	zone.id = "mill";
	zone.from = {1, 1};
	zone.to = {12, 4};
	zone.table = {{"training", 1}};
	zone.random.minSteps = 1;
	zone.random.maxSteps = 1;
	mill.zones = {zone};
	Session s(data, SessionOptions());
	inMill(s);
	s.world().position = {5, 2};
	s.world().facing = Facing::East;
	CHECK(platformOf(s, "raft").end == 0);
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.mode() == Mode::Field && s.ridingPlatform() == "raft");
	CHECK(!s.canStep(Facing::East));
	s.update(1000);
	CHECK(s.mode() == Mode::Field);
	CHECK(s.world().position == (Cell{9, 2}) && platformOf(s, "raft").end == 1 && platformOf(s, "raft").carrying);
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("\nriding_platform raft\n") != std::string::npos);
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.ridingPlatform().empty());
	CHECK(s.mode() == Mode::Battle);
	Session loaded(data, SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK(loaded.ridingPlatform() == "raft" && platformOf(loaded, "raft").end == 1);
}

// The looping skiff shuttles by itself; a one-way platform goes to `to` once its flags hold and stays.
TEST(loopingAndOneWayPlatformsMoveByThemselves) {
	GameData data = test::motionData();
	Platform& raft = data.areas.at("wren_mill").platforms.front();
	raft.mode = PlatformMode::OneWay;
	raft.requireFlag = "hollow.met";
	Session s(data, SessionOptions());
	inMill(s);
	s.update(1000);
	CHECK(platformOf(s, "skiff").end == -1);
	CHECK(platformOf(s, "raft").end == 0);
	s.world().flags["hollow.met"] = true;
	s.update(1000);
	CHECK(platformOf(s, "raft").end == 1);
	s.update(3000);
	CHECK(platformOf(s, "raft").end == 1 && platformOf(s, "raft").x == 9 * 16);
	CHECK(s.devWarp("wren_mill", "mill_door").empty());
	CHECK(platformOf(s, "raft").end == 1);
}

// The data check counts a platform's two docks as joined, and finds the far bank unreachable without it.
TEST(theDataCheckReachesAcrossPlatforms) {
	CHECK(checkData(DataDirectories{test::motionDataDirectory(), test::motionDataDirectory() + "/../../core/data"}).problems.empty());
	const CheckReport without = checkWith("motion_no_crossing", "areas/wren_mill.json",
		{{"\"to\": [9, 2]", "\"to\": [7, 2]"}, {"\"to\": [9, 4]", "\"to\": [7, 4]"}});
	CHECK(reports(without, "check.marker_not_reached"));
}

// Load problems: a patrol that drifts or walks off its terrain, two ways of moving, a crooked platform and a floor dock.
TEST(motionDataIsCheckedAtLoad) {
	CHECK(reports(checkWith("motion_drift", "game.json", "{\"to\": [4, 4], \"face\": \"north\"}", "{\"face\": \"north\"}"), "data.patrol_not_a_loop"));
	CHECK(reports(checkWith("motion_wall", "game.json", "\"count\": 2,", "\"count\": 5,"), "data.patrol_blocked"));
	CHECK(reports(checkWith("motion_both", "game.json", "\"path\": {", "\"wander\": {\"radius\": 1}, \"path\": {"), "data.moves_more_than_one_way"));
	CHECK(reports(checkWith("motion_crooked", "areas/wren_mill.json", "\"to\": [9, 4]", "\"to\": [9, 3]"), "data.platform_not_straight"));
	CHECK(reports(checkWith("motion_floor_dock", "areas/wren_mill.json", "\"from\": [6, 4]", "\"from\": [5, 4]"), "data.platform_dock_not_open"));
	CHECK(reports(checkWith("motion_axis", "game.json", "\"axis\": \"x\"", "\"axis\": \"z\""), "data.bad_wander_axis"));
	CHECK(reports(checkWith("motion_footprint", "game.json", "\"footprint\": [1, 1]", "\"footprint\": [0, 1]"), "data.bad_footprint"));
}

// Field poison and paralysis in an area with field_status: a poisoned member loses 1 hit point every Nth step and falls at
// 0; paralysis wears off by its roll or surely at `by`; the count starts again on entering an area.
TEST(fieldStatusRulesActOnStepsInMarkedAreas) {
	GameData data = test::motionData();
	data.areas.at("wren_mill").fieldStatus = true;
	data.db.battleRules.fieldPoisonEvery = 2;
	data.db.battleRules.fieldParalysisFrom = 1;
	data.db.battleRules.fieldParalysisChance = 1000;
	data.db.battleRules.fieldParalysisBy = 3;
	Session s(data, SessionOptions());
	inMill(s);
	rules::PartyMember& fighter = *s.world().member("fighter");
	fighter.hp = 1;
	fighter.statuses = {{"poison", 0}, {"paralysis", 0}};
	s.world().position = {1, 1};
	CHECK(s.walk(Facing::East).empty());
	CHECK_EQ(s.world().fieldSteps, 1);
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.world().member("fighter")->hp == 0 || s.mode() == Mode::Title);
	CHECK(test::outputHas(s.drainOutput(), "succumbs to poison"));
}

TEST(theParalysisRuleShakesItOffBy) {
	GameData data = test::motionData();
	data.areas.at("wren_mill").fieldStatus = true;
	data.db.battleRules.fieldParalysisFrom = 1;
	data.db.battleRules.fieldParalysisChance = 1000;
	data.db.battleRules.fieldParalysisBy = 3;
	Session s(data, SessionOptions());
	inMill(s);
	s.world().member("fighter")->statuses = {{"paralysis", 0}};
	s.world().position = {1, 1};
	for (int i = 0; i < 3; ++i) {
		CHECK(s.walk(Facing::East).empty());
	}
	CHECK(s.world().member("fighter")->statuses.empty());
	CHECK(test::outputHas(s.drainOutput(), "can move again"));
	CHECK(s.devWarp("wren_mill", "mill_door").empty());
	CHECK_EQ(s.world().fieldSteps, 0);
}

// field_poison alone counts poison steps and leaves paralysis to areas that mark it, or to every area under rules.json's
// field_paralysis.everywhere.
TEST(fieldPoisonAndParalysisApartAndParalysisEverywhere) {
	GameData data = test::motionData();
	data.areas.at("wren_mill").fieldPoison = true;
	data.db.battleRules.fieldPoisonEvery = 1;
	data.db.battleRules.fieldParalysisFrom = 0;
	data.db.battleRules.fieldParalysisChance = 1000;
	data.db.battleRules.fieldParalysisBy = 1;
	Session s(data, SessionOptions());
	inMill(s);
	rules::PartyMember& fighter = *s.world().member("fighter");
	fighter.hp = 5;
	fighter.statuses = {{"poison", 0}, {"paralysis", 0}};
	s.world().position = {1, 1};
	CHECK(s.walk(Facing::East).empty());
	CHECK_EQ(s.world().member("fighter")->hp, 4);
	CHECK_EQ(s.world().member("fighter")->statuses.size(), std::size_t(2));
	data.areas.at("wren_mill").fieldPoison = false;
	data.db.battleRules.fieldParalysisEverywhere = true;
	Session t(data, SessionOptions());
	inMill(t);
	t.world().member("fighter")->statuses = {{"poison", 0}, {"paralysis", 0}};
	t.world().position = {1, 1};
	CHECK(t.walk(Facing::East).empty());
	CHECK(t.world().member("fighter")->statuses.size() == 1 && t.world().member("fighter")->statuses[0].id == "poison");
	CHECK(test::outputHas(t.drainOutput(), "can move again"));
}

// Hosts: whether Load can run now, the keys a battle can carry, and the member detail's added fields.
TEST(canLoadBattleKeysAndMemberDetailFields) {
	Session s(test::data(), SessionOptions());
	CHECK(s.canLoad());
	CHECK(s.newGame().empty());
	CHECK(!s.canLoad());
	const BattleKeys keys = s.battleKeys();
	CHECK(std::find(keys.messages.begin(), keys.messages.end(), "battle.instant_defeat") != keys.messages.end());
	CHECK(std::find(keys.messages.begin(), keys.messages.end(), "status.poison.applied") != keys.messages.end());
	CHECK(std::find(keys.notices.begin(), keys.notices.end(), "game.victory") != keys.notices.end());
	CHECK(keys.cancelReasons.size() == 6 && keys.cancelReasons.front() == "actor_unable");
	const std::optional<MemberDetail> detail = s.memberDetail("fighter");
	CHECK(detail && detail->attack >= detail->stats.strength && detail->baseStats.strength <= detail->stats.strength);
	CHECK(detail && detail->experienceForLevel == detail->experienceToNext + s.world().member("fighter")->experience);
	CHECK(detail && detail->totalExperience >= s.world().member("fighter")->experience && detail->kind == rules::CreatureKind::Living);
}

// A door: an interact hook on a gated exit's cell answers from beside it while the exit is closed; once open the cell is
// the exit's and the hook is passed by. The data check accepts the hook there only on a gated exit.
TEST(aHookOnAClosedExitIsADoorUntilTheExitOpens) {
	GameData data = test::motionData();
	Area& mill = data.areas.at("wren_mill");
	mill.exits.front().requireFlag = "hollow.met";
	Hook door;
	door.id = "mill_door_hook";
	door.at = mill.exits.front().at;
	door.trigger = HookTrigger::Interact;
	door.event = "mill_sign";
	mill.hooks.push_back(door);
	Session s(data, SessionOptions());
	inMill(s);
	s.world().position = {2, 4};
	s.world().facing = Facing::South;
	std::optional<InteractTarget> target = s.interactTarget();
	CHECK(target && target->id == "mill_door_hook" && target->kind == InteractKind::Talk);
	CHECK(!s.canStep(Facing::South));
	s.world().flags["hollow.met"] = true;
	target = s.interactTarget();
	CHECK(!target || target->id != "mill_door_hook");
	CHECK(s.canStep(Facing::South));
	CHECK(s.walk(Facing::South).empty());
	CHECK_EQ(s.world().area, std::string("wren_loft"));
	const std::string exitLine = "{\"at\": [2, 5], \"to\": \"wren_loft\", \"marker\": \"loft_door\"";
	const std::string hookLine = "{\"id\": \"mill_sign\", \"at\": [1, 0], \"event\": \"mill_sign\"}";
	const std::string doorHook = hookLine + ", {\"id\": \"mill_door_hook\", \"at\": [2, 5], \"event\": \"mill_sign\"}";
	CHECK(checkWith("motion_door", "areas/wren_mill.json", {{exitLine, exitLine + ", \"require_flag\": \"hollow.met\""}, {hookLine, doorHook}}).problems.empty());
	CHECK(reports(checkWith("motion_door_open", "areas/wren_mill.json", hookLine, doorHook), "check.exit_on_object"));
}

// A flag declared with reset "area" clears whenever the party enters an area, and a save in the area keeps it.
TEST(anAreaFlagClearsOnEnteringAnArea) {
	GameData data = test::motionData();
	data.config.areaFlags.insert("hollow.met");
	Session s(data, SessionOptions());
	inMill(s);
	s.world().flags["hollow.met"] = true;
	std::string text;
	CHECK(s.saveText(text).empty());
	Session loaded(data, SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK(loaded.world().flag("hollow.met"));
	CHECK(s.devWarp("wren_mill", "mill_door").empty());
	CHECK(!s.world().flag("hollow.met"));
	CHECK(reports(checkWith("motion_flag_reset", "game.json", "\"flags\": [", "\"flags\": [{\"id\": \"mill.door_open\", \"reset\": \"week\"}, "), "data.bad_flag_reset"));
}

// An area's name may depend on where the party came from or on a flag; the arrival notice and the display name use it.
TEST(anAreasNameFollowsWhereThePartyCameFrom) {
	GameData data = test::motionData();
	data.areas.at("wren_mill").names = {{"wren_loft", "", "area.wren_loft", 0}, {"", "hollow.met", "area.wren_hollow", 0}, {"", "", "area.wren_mill", 0}};
	Session s(data, SessionOptions());
	inMill(s);
	CHECK_EQ(s.displayName("wren_mill"), std::string("Wren Mill"));
	s.world().flags["hollow.met"] = true;
	CHECK_EQ(s.areaNameKey("wren_mill"), std::string("area.wren_hollow"));
	s.world().flags.erase("hollow.met");
	CHECK(s.devWarp("wren_loft", "loft_door").empty());
	CHECK(s.devWarp("wren_mill", "mill_door").empty());
	CHECK(test::outputHas(s.drainOutput(), "You arrive at Wren Loft."));
	CHECK_EQ(s.world().cameFrom, std::string("wren_loft"));
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("\ncame_from wren_loft\n") != std::string::npos);
}

// Two belts on the same cells with opposite flags: only the one whose flags hold carries the party; a fractional belt
// step is kept to the microsecond under exact timing and rounded in milliseconds.
TEST(oppositeBeltsOnOneCellAndFractionalBeltSteps) {
	GameData data = test::motionData();
	data.config.exactSteps = true;
	Area& mill = data.areas.at("wren_mill");
	Conveyor east;
	east.cells = {{2, 3}};
	east.direction = Facing::East;
	east.stepMs = 134;
	east.stepUs = 133500;
	east.requireFlag = "hollow.met";
	Conveyor west = east;
	west.direction = Facing::West;
	west.requireFlag.clear();
	west.blockFlag = "hollow.met";
	mill.conveyors = {east, west};
	Session s(data, SessionOptions());
	inMill(s);
	s.world().position = {2, 3};
	const FieldMotion motion = s.fieldMotion();
	CHECK(motion.conveyorStepUs == 133500 && motion.conveyorStepMs == 134);
	s.update(150);
	CHECK(s.world().position == (Cell{1, 3}));
	s.world().flags["hollow.met"] = true;
	s.world().position = {2, 3};
	s.update(150);
	CHECK(s.world().position == (Cell{3, 3}));
}

// The leader, music, area and tile tests read the game.
TEST(leaderMusicAreaAndTileConditionsReadTheGame) {
	const GameData data = test::withScript("events/bell_cove.ev",
		"event cove_bell unskippable\n\tif leader fighter and area tidewater and tile floor\n\t\tmoney give 7\n\tend\n\tif music no_such_track or tile water\n\t\tmoney give 100\n\tend\nend\n");
	Session s(data, SessionOptions());
	CHECK(s.newGame().empty());
	test::confirmLines(s);
	const int before = s.world().money;
	CHECK(s.devRunEvent("cove_bell").empty());
	test::confirmLines(s);
	CHECK_EQ(s.world().money, before + 7);
}
