#include "test.h"

#include "game/check.h"
#include "game/encounters.h"
#include "game/profile.h"
#include "game/session.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace game;

namespace {

// Wren Cellar with game.json's interaction rules as `reach` says and the run speed and vehicle opening off, as without
// the keys.
GameData cellarWith(InteractReach reach) {
	GameData d = test::interactData();
	d.config.interactReach = reach;
	d.config.runSpeedPercent = 0;
	d.config.vehicleOpening = VehicleOpening::Roll;
	return d;
}

Hook& hook(GameData& d, const std::string& id) {
	std::vector<Hook>& hooks = d.areas.at("wren_cellar").hooks;
	return *std::find_if(hooks.begin(), hooks.end(), [&](const Hook& h) { return h.id == id; });
}

bool reported(const std::vector<rules::Problem>& problems, const std::string& key, const std::string& what) {
	return std::any_of(problems.begin(), problems.end(),
		[&](const rules::Problem& p) { return p.message.key == key && !p.message.params.empty() && p.message.params[0] == what; });
}

bool sameTarget(const std::optional<InteractTarget>& t, InteractKind kind, const std::string& id, Cell cell, Facing facing) {
	return t && t->kind == kind && t->id == id && t->cell == cell && t->facing == facing;
}

}

TEST(withTheInteractionRulesOffOnlyTheFacedCellAnswers) {
	const GameData d = cellarWith(InteractReach::Faced);
	Session s(d, SessionOptions());
	test::placeParty(s, "wren_cellar", "notice_side", Facing::East);
	CHECK(!s.interactTarget());
	CHECK_EQ(s.interact().key, std::string("game.refuse.nothing_there"));
	CHECK(s.world().facing == Facing::East);
	// The cook's own cell behind its counter does not answer; its talk point does.
	test::placeParty(s, "wren_cellar", "cook_side", Facing::East);
	CHECK(!s.interactTarget());
	test::placeParty(s, "wren_cellar", "cook_front", Facing::South);
	CHECK(sameTarget(s.interactTarget(), InteractKind::Actor, "cellar_cook", {10, 4}, Facing::South));
	// Running needs run_speed_percent, so the run input changes nothing.
	CHECK_EQ(s.leaderStepMs(true), 250);
	CHECK_EQ(s.leaderStepMs(false), 250);
	// No prompt outside the field.
	s.openMenu();
	CHECK(!s.interactTarget());
}

TEST(nearbyInteractionTurnsTheLeaderToTheFirstNeighborThatAnswers) {
	GameData d = test::interactData();
	{
		Session s(d, SessionOptions());
		test::placeParty(s, "wren_cellar", "notice_side", Facing::East);
		CHECK(sameTarget(s.interactTarget(), InteractKind::Talk, "cellar_notice", {5, 2}, Facing::North));
		CHECK(s.interact().empty());
		CHECK(s.world().facing == Facing::North);
		CHECK(s.linePending());
		test::confirmLines(s);
		CHECK(s.world().flag("cellar.door_open"));
	}
	// A neighbor that would refuse is passed by; faced, it refuses as before.
	hook(d, "cellar_notice").requireFlag = "cellar.door_open";
	Session s(d, SessionOptions());
	test::placeParty(s, "wren_cellar", "notice_side", Facing::East);
	CHECK(!s.interactTarget());
	s.face(Facing::North);
	const std::optional<InteractTarget> faced = s.interactTarget();
	CHECK(faced && !faced->ready);
	CHECK_EQ(s.interact().key, std::string("game.refuse.no_answer"));
}

TEST(nearbyInteractionTalksToAnActorWhereItStands) {
	const GameData& d = test::interactData();
	Session s(d, SessionOptions());
	test::placeParty(s, "wren_cellar", "cook_side", Facing::East);
	CHECK(sameTarget(s.interactTarget(), InteractKind::Actor, "cellar_cook", {10, 5}, Facing::East));
	// Facing away, the cook still answers from the side, the facing first.
	s.face(Facing::North);
	CHECK(sameTarget(s.interactTarget(), InteractKind::Actor, "cellar_cook", {10, 5}, Facing::East));
	CHECK(s.interact().empty());
	CHECK(s.world().facing == Facing::East);
	CHECK(s.linePending());
}

TEST(aCounterIsUsedAcrossByTheHooksAcrossOrTheGameRule) {
	GameData d = cellarWith(InteractReach::Faced);
	{
		Session s(d, SessionOptions());
		test::placeParty(s, "wren_cellar", "clerk_front", Facing::North);
		const std::optional<InteractTarget> t = s.interactTarget();
		CHECK(sameTarget(t, InteractKind::Talk, "cellar_clerk", {2, 1}, Facing::North));
		CHECK(t && t->across && t->ready);
		CHECK(s.interact().empty());
		CHECK(s.linePending());
	}
	hook(d, "cellar_clerk").across = false;
	{
		Session s(d, SessionOptions());
		test::placeParty(s, "wren_cellar", "clerk_front", Facing::North);
		CHECK(!s.interactTarget());
		CHECK_EQ(s.interact().key, std::string("game.refuse.nothing_there"));
	}
	d.config.interactAcross = true;
	Session s(d, SessionOptions());
	test::placeParty(s, "wren_cellar", "clerk_front", Facing::North);
	CHECK(sameTarget(s.interactTarget(), InteractKind::Talk, "cellar_clerk", {2, 1}, Facing::North));
	// Only across a counter: floor in between is walked, not reached over.
	test::placeParty(s, "wren_cellar", "notice_side", Facing::West);
	CHECK(!s.interactTarget());
}

TEST(aHookWithFurtherCellsAnswersFromBesideAnyOfThem) {
	const GameData d = cellarWith(InteractReach::Faced);
	Session s(d, SessionOptions());
	const Area& cellar = *d.area("wren_cellar");
	CHECK(!cellar.walkable({9, 2}));
	CHECK(cellar.hookAt({9, 2}, HookTrigger::Interact) == cellar.hookAt({8, 2}, HookTrigger::Interact));
	test::placeParty(s, "wren_cellar", "table_north", Facing::South);
	CHECK(sameTarget(s.interactTarget(), InteractKind::Talk, "cellar_table", {9, 2}, Facing::South));
	CHECK(s.interact().empty());
	test::confirmLines(s);
	test::placeParty(s, "wren_cellar", "table_south", Facing::North);
	CHECK(sameTarget(s.interactTarget(), InteractKind::Talk, "cellar_table", {8, 2}, Facing::North));
	CHECK(!s.canStep(Facing::North));
}

TEST(theDataCheckFollowsTheInteractionRules) {
	GameData d = cellarWith(InteractReach::Faced);
	CHECK(crossCheck(d, true).empty());
	// The clerk stands boxed in behind its counter: reached only across it.
	hook(d, "cellar_clerk").across = false;
	CHECK(reported(crossCheck(d, true), "check.object_not_reached", "hook 'cellar_clerk'"));
	d.config.interactAcross = true;
	CHECK(crossCheck(d, true).empty());
	// A further cell beside the floor makes the clerk reached without the rule.
	d.config.interactAcross = false;
	hook(d, "cellar_clerk").cells = {{4, 1}};
	CHECK(crossCheck(d, true).empty());
	hook(d, "cellar_clerk").cells = {{1, 1}};
	CHECK(reported(crossCheck(d, true), "check.object_not_reached", "hook 'cellar_clerk'"));
	hook(d, "cellar_clerk").cells = {{5, 2}};
	CHECK(reported(crossCheck(d, true), "check.cell_shared", "hook 'cellar_notice'"));
	hook(d, "cellar_clerk").cells.clear();
	hook(d, "cellar_clerk").across = true;
	// The cook's counter walled in on every side: under nearby, the cell it stands on is used.
	std::vector<std::string>& rows = d.areas.at("wren_cellar").rows;
	rows[3][10] = '#';
	rows[4][9] = '#';
	rows[4][11] = '#';
	CHECK(reported(crossCheck(d, true), "check.object_not_reached", "hook 'cellar_cook'"));
	d.config.interactReach = InteractReach::Nearby;
	CHECK(!reported(crossCheck(d, true), "check.object_not_reached", "hook 'cellar_cook'"));
}

TEST(interactionFieldsAreReadAndRefused) {
	const auto area = [](const std::string& exit, const std::string& hook) {
		return parseArea("t.json", R"({"id": "t", "map": ["#####", "#...#", "##.##"], "markers": {"a": [1, 1]}, "exits": [)" + exit + R"(], "hooks": [)" +
			hook + "]}");
	};
	const rules::Result<Area> good = area(R"({"at": [2, 2], "to": "t", "marker": "a", "block_flag": "f", "closed_tile": "~"})",
		R"({"id": "h", "at": [2, 0], "event": "e", "across": true, "cells": [[1, 0], [3, 0]]})");
	CHECK(good.ok());
	CHECK(good.ok() && good.value->exits[0].closedTile == '~' && good.value->hooks[0].across && good.value->hooks[0].cells.size() == 2u);
	CHECK(area(R"({"at": [2, 2], "to": "t", "marker": "a", "require_flag": "f", "closed_tile": "normal"})", "").value->exits[0].closedTile == 0);
	CHECK(area(R"({"at": [2, 2], "to": "t", "marker": "a"})", "").value->exits[0].closedTile == '#');
	const auto refused = [&](const std::string& exit, const std::string& hook, const std::string& key) {
		const rules::Result<Area> r = area(exit, hook);
		return !r.ok() && std::any_of(r.problems.begin(), r.problems.end(), [&](const rules::Problem& p) { return p.message.key == key; });
	};
	CHECK(refused(R"({"at": [2, 2], "to": "t", "marker": "a", "block_flag": "f", "closed_tile": "door"})", "", "data.bad_closed_tile"));
	CHECK(refused(R"({"at": [2, 2], "to": "t", "marker": "a", "closed_tile": "normal"})", "", "data.closed_tile_never_closed"));
	CHECK(refused(R"({"at": [2, 2], "to": "t", "marker": "a"})", R"({"id": "h", "at": [2, 0], "event": "e", "cells": [[2, 0]]})", "data.hook_cell_twice"));
	CHECK(refused(R"({"at": [2, 2], "to": "t", "marker": "a"})", R"({"id": "h", "at": [1, 1], "trigger": "step", "event": "e", "across": true})",
		"data.across_cells_interact_hooks_only"));
}

TEST(aClosedExitIsDrawnAsItsClosedTileAndStillBlocks) {
	GameData d = cellarWith(InteractReach::Faced);
	{
		Session s(d, SessionOptions());
		test::placeParty(s, "wren_cellar", "cellar_door", Facing::South);
		const Area& view = *s.area();
		CHECK_EQ(view.rows[9][6], '.');
		CHECK(!view.walkable({6, 9}));
		CHECK(!s.canStep(Facing::South));
		CHECK(s.canStep(Facing::North));
		CHECK_EQ(s.walk(Facing::South).key, std::string("game.refuse.blocked"));
		CHECK(s.devSetFlag("cellar.door_open", true).empty());
		CHECK(s.area()->walkable({6, 9}));
		CHECK(s.canStep(Facing::South));
		CHECK(s.walk(Facing::South).empty());
		CHECK_EQ(s.world().area, std::string("wren_hollow"));
	}
	d.areas.at("wren_cellar").exits[0].closedTile = ':';
	Session s(d, SessionOptions());
	test::placeParty(s, "wren_cellar", "cellar_door", Facing::South);
	CHECK_EQ(s.area()->rows[9][6], ':');
	CHECK(!s.canStep(Facing::South));
	// An exit drawn as a wall while closed keeps today's view.
	d.areas.at("wren_cellar").exits[0].closedTile = '#';
	Session wall(d, SessionOptions());
	test::placeParty(wall, "wren_cellar", "cellar_door", Facing::South);
	CHECK_EQ(wall.area()->rows[9][6], '#');
	CHECK(wall.area()->closed.empty());
}

TEST(runningScalesTheLeadersStepTimeOnFoot) {
	const GameData& d = test::interactData();
	Session s(d, SessionOptions());
	test::placeParty(s, "wren_cellar", "cook_side", Facing::West);
	CHECK_EQ(s.leaderStepMs(), 250);
	CHECK_EQ(s.leaderStepMs(true), 125);
	// A step at 125 ms takes three 50 ms ticks; walking it takes five.
	const auto ticksForAStep = [&](bool running) {
		const Cell from = s.world().position;
		int ticks = 0;
		while (s.world().position == from && ticks < 20) {
			s.update(d.config.tickMs, {true, Facing::West, running});
			++ticks;
		}
		return ticks;
	};
	CHECK_EQ(ticksForAStep(true), 3);
	CHECK_EQ(ticksForAStep(false), 5);
	Profile p = s.profile();
	p.alwaysRun = true;
	CHECK(s.setProfile(p).empty());
	CHECK_EQ(s.leaderStepMs(), 125);
	CHECK_EQ(s.leaderStepMs(true), 250);
	CHECK_EQ(ticksForAStep(false), 3);
	CHECK(serializeProfile(p).find("always_run on\n") != std::string::npos);
	const rules::Result<Profile> back = parseProfile(serializeProfile(p));
	CHECK(back.ok() && back.value->alwaysRun);
	CHECK(serializeProfile(Profile()).find("always_run") == std::string::npos);
	// Aboard, the vehicle's speed holds whatever the run input says.
	CHECK(s.devVehicle("reed_skimmer", "wren_cellar", "skiff_post").empty());
	test::placeParty(s, "wren_cellar", "skiff_front", Facing::East);
	CHECK(s.board().empty());
	CHECK_EQ(s.leaderStepMs(), 125);
	CHECK_EQ(s.leaderStepMs(true), 125);
}

TEST(battlesAboardOpenWithTheRollOrNormallyAsTheDataSays) {
	const GameData& d = test::interactData();
	const Area& cellar = *d.area("wren_cellar");
	const FieldVehicle& skimmer = *d.vehicle("reed_skimmer");
	int rolled = 0;
	for (std::uint64_t seed = 1; seed <= 100; ++seed) {
		World rolling;
		rolling.encounterStream = rules::Rng(seed);
		World normal = rolling;
		const std::optional<EncounterStart> r = vehicleStep(rolling, cellar, {11, 7}, skimmer.encounters, d.db, 200, true);
		const std::optional<EncounterStart> n = vehicleStep(normal, cellar, {11, 7}, skimmer.encounters, d.db, 200, false);
		CHECK(r && n && r->encounter == n->encounter);
		CHECK(n && n->opening == rules::Opening::Normal);
		rolled += r && r->opening != rules::Opening::Normal;
		// The normal opening draws nothing for the roll.
		normal.encounterStream.next();
		CHECK_EQ(normal.encounterStream.state(), rolling.encounterStream.state());
	}
	CHECK_EQ(rolled, 100);
}

TEST(aVehiclesOwnOpeningWinsOverTheGames) {
	// The encounter stream once the skimmer's first battle in the pool has started: a roll draws one number more.
	const auto streamAtBattle = [](const GameData& d) {
		Session s(d, SessionOptions());
		test::placeParty(s, "wren_cellar", "skiff_front", Facing::East);
		CHECK(s.devVehicle("reed_skimmer", "wren_cellar", "skiff_post").empty());
		CHECK(s.board().empty());
		s.world().graceMs = 0;
		CHECK(s.walk(Facing::East).empty());
		CHECK(s.walk(Facing::East).empty());
		CHECK(s.mode() == Mode::Battle);
		return s.world().encounterStream.state();
	};
	GameData normal = test::interactData();
	CHECK(normal.config.vehicleOpening == VehicleOpening::Normal);
	GameData rolling = normal;
	rolling.config.vehicleOpening = VehicleOpening::Roll;
	GameData ownNormal = rolling;
	ownNormal.config.vehicles[0].opening = VehicleOpening::Normal;
	GameData ownRoll = normal;
	ownRoll.config.vehicles[0].opening = VehicleOpening::Roll;
	CHECK(normal.config.vehicles[0].id == "reed_skimmer");
	CHECK(streamAtBattle(normal) != streamAtBattle(rolling));
	CHECK(streamAtBattle(normal) == streamAtBattle(ownNormal));
	CHECK(streamAtBattle(rolling) == streamAtBattle(ownRoll));
}
