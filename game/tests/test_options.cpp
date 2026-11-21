#include "test.h"

#include "game/encounters.h"
#include "game/runner.h"
#include "game/save.h"

#include <sstream>

using namespace game;

namespace {

// A new game in the Cogway, standing on `cell` facing `facing`, with battles off unless asked for.
Session& cogway(Session& s, Cell cell, Facing facing, bool encounters = false) {
	s.newGame();
	test::confirmLines(s);
	CHECK(s.devWarpCell("cogway", cell).empty());
	test::confirmLines(s);
	s.devEncounters(encounters);
	s.face(facing);
	return s;
}

void walk(Session& s, Facing direction) {
	s.update(s.area()->stepMs + s.data().config.tickMs, {true, direction});
	s.update(s.data().config.tickMs);
}

}

// R19, R24: an exit with require_flag is a wall until the flag holds; the Cogway's power lever opens it and runs a once
// flag hook the first time, which the save keeps.
TEST(gatedExitsOpenWithTheirFlagAndOnceHooksFireOnce) {
	Session s(test::data(), SessionOptions());
	cogway(s, {1, 4}, Facing::West);
	CHECK_EQ(s.area()->rows[6][6], '#');
	CHECK(!exitOpen(s.area()->exits[0], s.world()));
	CHECK(s.interact().empty());
	std::vector<std::string> lines;
	while (s.linePending()) {
		lines.push_back(s.line()->textId);
		s.confirmLine();
	}
	CHECK((lines == std::vector<std::string>{"cogway.power.on", "cogway.welcome"}));
	CHECK(s.world().onceHooks.count("cogway/cogway_welcome") == 1);
	CHECK_EQ(s.area()->rows[6][6], '.');
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(s.interact().empty());
	lines.clear();
	while (s.linePending()) {
		lines.push_back(s.line()->textId);
		s.confirmLine();
	}
	CHECK((lines == std::vector<std::string>{"cogway.power.on"}));

	SaveFile file;
	file.world = s.world();
	const rules::Result<SaveFile> back = parseSave(serializeSave(file));
	CHECK(back.ok() && back.value->world.onceHooks == s.world().onceHooks);
}

// R20, R26: an interact hook marked solid while_active stops blocking once its flags turn it off, and its actor is no
// longer shown; a nameless actor's lines carry no name plate.
TEST(aSweeperStepsAsideOncePaidAndHasNoName) {
	Session s(test::data(), SessionOptions());
	cogway(s, {8, 3}, Facing::North);
	CHECK(!s.area()->walkable({8, 2}));
	s.world().money = 50;
	const int wornBefore = s.world().equipmentBag.count("copper_blade") ? s.world().equipmentBag.at("copper_blade") : 0;
	CHECK(s.interact().empty());
	CHECK(s.linePending() && s.line()->speakerId == "cog_sweeper" && s.line()->speakerName.empty());
	CHECK(s.lineText().find(':') == std::string::npos);
	test::confirmLines(s);
	CHECK(s.choose(0).empty());
	test::confirmLines(s);
	CHECK_EQ(s.world().money, 30);
	CHECK_EQ(s.world().member("fighter")->equipment.at(rules::EquipSlot::Weapon), std::string("gill_knife"));
	CHECK_EQ(s.world().equipmentBag.at("copper_blade"), wornBefore + 1);
	CHECK(s.world().flag("cogway.swept"));
	CHECK(s.area()->walkable({8, 2}));
	for (const ActorView& actor : s.actorsHere()) {
		CHECK(actor.id != "cog_sweeper" || !actor.visible);
	}
	walk(s, Facing::North);
	CHECK(s.world().position == (Cell{8, 2}));

	Session poor(test::data(), SessionOptions());
	cogway(poor, {8, 3}, Facing::North);
	poor.world().money = 3;
	CHECK(poor.interact().empty());
	test::confirmLines(poor);
	CHECK_EQ(poor.world().money, 8);
}

// R21: the rack's unequip takes the knife off and back to the bag.
TEST(scriptsTakeEquipmentOff) {
	Session s(test::data(), SessionOptions());
	cogway(s, {3, 1}, Facing::North);
	s.world().equipmentBag.erase("gill_knife");
	s.world().member("fighter")->equipment[rules::EquipSlot::Weapon] = "gill_knife";
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(!s.world().member("fighter")->equipment.count(rules::EquipSlot::Weapon));
	CHECK_EQ(s.world().equipmentBag.at("gill_knife"), 1);
}

// R23: a script's area music lasts in the save until the party leaves the area or a script sets it back.
TEST(areaMusicFromAScriptIsSavedUntilTheAreaChanges) {
	Session s(test::data(), SessionOptions());
	cogway(s, {10, 1}, Facing::North);
	CHECK_EQ(s.areaMusic(), std::string("dungeon_kiln_halls"));
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK_EQ(s.areaMusic(), std::string("dungeon_firing_floor"));
	std::string saved;
	CHECK(s.saveText(saved).empty());
	Session loaded(test::data(), SessionOptions());
	CHECK(loaded.loadSaveText(saved).empty());
	CHECK_EQ(loaded.areaMusic(), std::string("dungeon_firing_floor"));
	CHECK(loaded.devWarp("fell_moor", "town_gate").empty());
	CHECK(loaded.world().areaMusic.empty());
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK_EQ(s.areaMusic(), std::string("dungeon_kiln_halls"));
}

// R25: a conveyor carries the party a cell per step, taking no input; a step hook is a lift.
TEST(beltsCarryThePartyAndALiftStepHookWarps) {
	Session s(test::data(), SessionOptions());
	cogway(s, {2, 3}, Facing::South);
	for (int i = 0; i < 40 && s.world().position != Cell{6, 3}; ++i) {
		s.update(s.data().config.tickMs, {true, Facing::West});
	}
	CHECK(s.world().position == (Cell{6, 3}));
	walk(s, Facing::West);
	CHECK(s.world().position == (Cell{5, 3}));

	Session lift(test::data(), SessionOptions());
	cogway(lift, {1, 2}, Facing::North);
	walk(lift, Facing::North);
	test::confirmLines(lift);
	CHECK(lift.world().position == (Cell{10, 1}));
}

// R18: the Cogway's zone rolls 1 in 6 per step after 3 grace steps, and never beside its exit.
TEST(encounterRollsHaveGraceAChancePerStepAndSafeCellsByExits) {
	const Area& area = *test::data().area("cogway");
	const Zone& zone = *area.zone("cogway_floor");
	World w;
	w.encounterStream = rules::Rng(9);
	std::vector<rules::PartyMember> party;
	for (int step = 1; step <= 3; ++step) {
		w.areaSteps = step;
		const std::uint64_t before = w.encounterStream.state();
		CHECK(!randomStep(w, area, {3, 2}, test::data().db, party));
		CHECK_EQ(w.encounterStream.state(), before);
	}
	w.areaSteps = zone.random.graceSteps + 1;
	const std::uint64_t before = w.encounterStream.state();
	randomStep(w, area, {6, 5}, test::data().db, party);
	CHECK_EQ(w.encounterStream.state(), before);
	int battles = 0;
	for (int step = 0; step < 600; ++step) {
		battles += randomStep(w, area, {3, 2}, test::data().db, party) ? 1 : 0;
	}
	CHECK(battles > 60 && battles < 150);
	CHECK_EQ(w.randomCounter, 0);
}

// R22: sand is the soft ground ':' already: only a vehicle that crosses soft ground goes over it.
TEST(sandIsTheSoftGroundTerrain) {
	const Area& moor = *test::data().area("fell_moor");
	CHECK_EQ(moor.rows[1][7], ':');
	CHECK(!moor.walkable({7, 1}));
	const FieldVehicle* crawler = test::data().vehicle("dune_crawler");
	const FieldVehicle* skimmer = test::data().vehicle("reed_skimmer");
	CHECK(crawler && crawler->canCross(':'));
	CHECK(skimmer && !skimmer->canCross(':'));
}

// R19, R25: step hooks may name a map character in place of a cell; ports close while their block_flag is set.
TEST(tileStepHooksAndPortBlockFlags) {
	const rules::Result<Area> area = parseArea("test.json", R"({"id": "t", "map": ["....", ".~~."], "markers": {"a": [0, 0]},
		"hooks": [{"id": "wet", "trigger": "step", "tile": "~", "event": "e"}, {"id": "spot", "trigger": "step", "at": [0, 0], "event": "e"}],
		"ports": [{"id": "p", "at": [3, 0], "marker": "a", "block_flag": "b"}]})");
	CHECK(area.ok());
	if (!area.ok()) {
		return;
	}
	CHECK(area.value->stepHookAt({1, 1}) && area.value->stepHookAt({1, 1})->id == "wet");
	CHECK(area.value->stepHookAt({0, 0}) && area.value->stepHookAt({0, 0})->id == "spot");
	CHECK(!area.value->stepHookAt({1, 0}));
	World w;
	CHECK(portOpen(area.value->ports[0], w));
	w.flags["b"] = true;
	CHECK(!portOpen(area.value->ports[0], w));
	CHECK(!parseArea("bad.json", R"({"id": "t", "map": ["."], "markers": {}, "hooks": [{"id": "x", "trigger": "interact", "at": [0, 0], "tile": ".", "event": "e"}]})").ok());
	CHECK(!parseArea("bad.json", R"({"id": "t", "map": ["."], "markers": {}, "hooks": [{"id": "x", "trigger": "step", "at": [0, 0], "event": "e", "once": true}]})").ok());
}

// Bag pieces with use_from_bag reach the battle; the status screen lists every element with a non-normal affinity.
TEST(bagPiecesReachBattlesAndDetailListsDeclaredElements) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	s.world().equipmentBag["squall_fan"] = 1;
	s.world().equipmentBag["ember_mail"] = 1;
	CHECK(s.openMenu().empty());
	CHECK(s.equip("fighter", "ember_mail").empty());
	const std::optional<MemberDetail> detail = s.memberDetail("fighter");
	CHECK(detail && detail->affinities.size() == 2 && detail->affinities[0].first == rules::Element::Heat && detail->affinities[0].second == 50 &&
		detail->affinities[1].first == rules::Element::Chill && detail->affinities[1].second == 150);
	CHECK_EQ(s.elementName(rules::Element("tide")), std::string("Tide"));
	s.closeMenu();
	CHECK(s.devBattle("training").empty());
	CHECK(s.battle() && s.battle()->setup().pieces.count("squall_fan") == 1);
}

// The terminal names an enemy that joins mid-battle, as it names the others.
TEST(theTerminalLogNamesEnemiesThatJoin) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	std::ostringstream out;
	RunnerOptions options;
	options.autoBattle = true;
	Runner runner(s, out, options);
	CHECK(s.devBattle("hive").empty());
	runner.settle();
	const std::string log = out.str();
	CHECK(log.find("Gel Mite") != std::string::npos);
	CHECK(log.find(" ?") == std::string::npos && log.find("? ") == std::string::npos);
}
