#include "test.h"

#include "game/check.h"
#include "game/encounters.h"
#include "game/runner.h"
#include "game/save.h"
#include "rules/formulas.h"
#include "rules/progression.h"

#include <algorithm>
#include <filesystem>
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

// R100: game.json's music.areas gives the area's track by the first listed flag that is set, read again as flags change in
// the area; a script's `music area` still wins until the party leaves, and `music area default` hands back to the list.
TEST(areaMusicFollowsTheFirstListedFlagAndAScriptStillOverridesIt) {
	GameData d = test::data();
	d.config.presentation.music.areas["cogway"] = {{"cogway.loud", "field_marsh_reeds"}, {"cogway.swept", "battle_marsh_ambush"}};
	d.config.presentation.music.areas["fell_moor"] = {{"", "title_harbor_dawn"}};
	Session s(d, SessionOptions());
	cogway(s, {10, 1}, Facing::North);
	CHECK_EQ(s.areaMusic(), std::string("dungeon_kiln_halls"));
	s.world().flags["cogway.swept"] = true;
	CHECK_EQ(s.areaMusic(), std::string("battle_marsh_ambush"));
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(s.world().flag("cogway.loud"));
	CHECK_EQ(s.areaMusic(), std::string("dungeon_firing_floor"));
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(!s.world().flag("cogway.loud") && s.world().areaMusic.empty());
	CHECK_EQ(s.areaMusic(), std::string("battle_marsh_ambush"));
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(s.devWarp("fell_moor", "town_gate").empty());
	test::confirmLines(s);
	CHECK_EQ(s.areaMusic(), std::string("title_harbor_dawn"));
	CHECK(s.devWarpCell("cogway", {10, 1}).empty());
	test::confirmLines(s);
	CHECK_EQ(s.areaMusic(), std::string("field_marsh_reeds"));
}

// R99: a random battle aboard plays the vehicle's track from game.json's music.vehicles, an encounter's own track first.
TEST(randomBattlesAboardPlayTheVehiclesTrackUnlessTheEncounterHasOne) {
	GameData d = test::data();
	d.config.presentation.music.vehicles["reed_skimmer"] = "dungeon_firing_floor";
	Session s(d, SessionOptions());
	cogway(s, {6, 4}, Facing::South);
	CHECK(s.battleMusic().empty());
	CHECK(s.devBattle("training").empty());
	CHECK(s.battleMusic().empty());
	CHECK(s.devEndBattle(true).empty());
	test::confirmLines(s);
	CHECK(s.devVehicle("reed_skimmer", "cogway", "entry").empty());
	CHECK(s.board().empty());
	CHECK(s.devBattle("training").empty());
	CHECK(s.battleSource() == BattleSource::Random && s.battle()->setup().flags.vehicleBattle);
	CHECK_EQ(s.battleMusic(), std::string("dungeon_firing_floor"));
	CHECK(s.devEndBattle(true).empty());
	CHECK(!s.battle() && s.endedBattle());
	CHECK_EQ(s.battleMusic(), std::string("dungeon_firing_floor"));
	test::confirmLines(s);
	CHECK(s.devBattle("marsh").empty());
	CHECK_EQ(s.battleMusic(), std::string("battle_marsh_ambush"));
}

// presentation.sounds: a chest's own sound over the default, a vehicle's board, move (every step, or every move_ms of
// moving) and leave sounds, each a `sound` action outside any event; unknown ids are problems.
TEST(fieldSoundsPlayAsSoundActions) {
	GameData d = test::data();
	FieldSounds& sounds = d.config.presentation.sounds;
	sounds.chest = "brass_alarm";
	sounds.chests["reed_cache"] = "dungeon_firing_floor";
	sounds.vehicles["reed_skimmer"] = {"battle_marsh_ambush", 0, "brass_alarm", "defeat_low_tide"};
	Session s(d, SessionOptions());
	const auto played = [&] {
		std::vector<std::string> out;
		for (const PresentedAction& a : s.pollActions()) {
			if (a.action.type == events::ActionType::sound) {
				out.push_back(a.action.subject);
			}
		}
		return out;
	};
	test::placeParty(s, "reed_path", "cache_front", Facing::North);
	played();
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(played() == std::vector<std::string>{"dungeon_firing_floor"});
	cogway(s, {6, 4}, Facing::South);
	CHECK(s.devVehicle("reed_skimmer", "cogway", "entry").empty());
	played();
	CHECK(s.board().empty());
	CHECK(played() == std::vector<std::string>{"brass_alarm"});
	const Cell start = s.world().position;
	for (Facing f : {Facing::South, Facing::East, Facing::North, Facing::West}) {
		for (int i = 0; i < 4 && s.world().position == start; ++i) {
			walk(s, f);
		}
	}
	CHECK(!(s.world().position == start));
	const std::vector<std::string> moved = played();
	CHECK(!moved.empty() && std::all_of(moved.begin(), moved.end(), [](const std::string& m) { return m == "battle_marsh_ambush"; }));
	CHECK(s.leaveVehicle().empty());
	CHECK(played() == std::vector<std::string>{"defeat_low_tide"});

	GameData every = test::data();
	every.config.presentation.sounds.vehicles["reed_skimmer"] = {"battle_marsh_ambush", 100000, "", ""};
	Session t(every, SessionOptions());
	cogway(t, {6, 4}, Facing::South);
	CHECK(t.devVehicle("reed_skimmer", "cogway", "entry").empty());
	CHECK(t.board().empty());
	t.pollActions();
	const Cell from = t.world().position;
	for (Facing f : {Facing::South, Facing::East, Facing::North, Facing::West}) {
		for (int i = 0; i < 4 && t.world().position == from; ++i) {
			walk(t, f);
		}
	}
	CHECK(!(t.world().position == from));
	const std::vector<PresentedAction> quiet = t.pollActions();
	CHECK(std::none_of(quiet.begin(), quiet.end(), [](const PresentedAction& a) { return a.action.type == events::ActionType::sound; }));

	const std::string root = test::freshDirectory("field_sounds");
	std::filesystem::create_directories(root + "/game");
	std::filesystem::create_directories(root + "/core");
	std::filesystem::copy("data", root + "/game/data", std::filesystem::copy_options::recursive);
	std::filesystem::copy("../core/data", root + "/core/data", std::filesystem::copy_options::recursive);
	std::string text;
	CHECK(readFile(root + "/game/data/game.json", text));
	text.replace(text.find("\"presentation\": {"), 17, "\"presentation\": {\"sounds\": {\"chest\": \"no_such_cue\", \"chests\": {\"no_such_chest\": \"brass_alarm\"}, "
		"\"travel_things\": {\"tonic\": \"brass_alarm\"}, \"vehicles\": {\"hovercart\": {\"move\": \"brass_alarm\", \"move_ms\": 300}}}, ");
	std::string problem;
	CHECK(writeFileAtomic(root + "/game/data/game.json", text, problem));
	const CheckReport report = checkData(DataDirectories{root + "/game/data", root + "/core/data"});
	for (const char* key : {"data.sound_not_audio", "data.sound_unknown_chest", "data.sound_not_travel", "data.sound_unknown_vehicle"}) {
		CHECK(std::any_of(report.problems.begin(), report.problems.end(), [&](const rules::Problem& p) { return p.message.key == key; }));
	}
}

// A vehicle's `field` track plays on the field while the party rides it, over the area's; a script's `music area` wins.
TEST(aVehiclesFieldTrackPlaysWhileThePartyRidesIt) {
	GameData d = test::data();
	d.config.presentation.music.vehiclesField["reed_skimmer"] = "battle_brass_warden";
	Session s(d, SessionOptions());
	cogway(s, {6, 4}, Facing::South);
	const std::string own = s.areaMusic();
	CHECK(own != "battle_brass_warden");
	CHECK(s.devVehicle("reed_skimmer", "cogway", "entry").empty());
	CHECK(s.board().empty());
	CHECK_EQ(s.areaMusic(), std::string("battle_brass_warden"));
	s.world().areaMusic = "title_harbor_dawn";
	CHECK_EQ(s.areaMusic(), std::string("title_harbor_dawn"));
	s.world().areaMusic.clear();
	CHECK(s.leaveVehicle().empty());
	CHECK_EQ(s.areaMusic(), own);
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

namespace {

// The Cogway's floor with no safe cells, so every step after the grace rolls.
GameData cogwayRolls(int graceSteps, int stepChance, int vehicleStepChance, int graceMs) {
	GameData d = test::data();
	d.config.graceMs = graceMs;
	RandomRules& r = d.areas.at("cogway").zones.at(0).random;
	r.graceSteps = graceSteps;
	r.stepChance = stepChance;
	r.vehicleStepChance = vehicleStepChance;
	r.safeNearExits = false;
	return d;
}

// Holds a direction until the leader stands on the next cell; whether that step started a battle.
bool stepFor(Session& s, Facing direction) {
	const Cell from = s.world().position;
	for (int i = 0; i < 40 && s.world().position == from && s.mode() == Mode::Field; ++i) {
		s.update(s.data().config.tickMs, {true, direction});
	}
	CHECK(s.world().position != from);
	return s.mode() == Mode::Battle;
}

// Paces the Cogway's row `y` between columns 2 and 9; whether the step started a battle, which it then wins.
bool pace(Session& s, int y) {
	const Cell at = s.world().position;
	CHECK_EQ(at.y, y);
	const Facing direction = at.x >= 9 ? Facing::West : at.x <= 2 ? Facing::East : s.world().facing;
	const bool battle = stepFor(s, direction);
	if (battle) {
		CHECK(s.devEndBattle(true).empty());
		test::confirmLines(s);
		CHECK(s.mode() == Mode::Field);
	}
	return battle;
}

}

// R18: with grace_steps N and a chance of 1, the first battle comes on step N + 1, on foot and aboard, and battles keep
// coming after it once the post-battle grace runs out.
TEST(theFirstRandomBattleComesTheStepAfterTheGrace) {
	const GameData d = cogwayRolls(3, 1, 1, 3000);
	for (bool aboard : {false, true}) {
		Session s(d, SessionOptions());
		cogway(s, {6, 4}, Facing::South, true);
		if (aboard) {
			CHECK(s.devVehicle("reed_skimmer", "cogway", "entry").empty());
			CHECK(s.board().empty());
		}
		s.face(Facing::East);
		const int y = s.world().position.y;
		for (int step = 1; step <= 3; ++step) {
			CHECK(!pace(s, y));
		}
		CHECK(pace(s, y));
		CHECK_EQ(s.world().areaSteps, 4);
		int later = 0;
		for (int step = 0; step < 60; ++step) {
			later += pace(s, y) ? 1 : 0;
		}
		CHECK(later >= 3);
	}
}

namespace {

// One step along the Cogway's row; a battle it starts is won or escaped, never lost.
bool paceAndEnd(Session& s, int y, bool escape) {
	const Cell at = s.world().position;
	const Facing direction = at.x >= 9 ? Facing::West : at.x <= 2 ? Facing::East : s.world().facing;
	if (!stepFor(s, direction)) {
		return false;
	}
	if (escape) {
		for (int round = 0; round < 10 && s.mode() == Mode::Battle; ++round) {
			CHECK(s.setCommand(s.battle()->partyCount() - 1, rules::Command::escape()).empty());
			s.resolveRound();
			test::confirmLines(s);
		}
		CHECK(s.endedBattle() && s.endedBattle()->outcome == rules::Outcome::Escaped);
	} else {
		CHECK(s.devEndBattle(true).empty());
		test::confirmLines(s);
	}
	CHECK(s.mode() == Mode::Field);
	CHECK_EQ(s.world().position.y, y);
	return true;
}

}

// R45: with grace_after_battle (on the zone, or for every zone in game.json) the grace steps start again after every
// battle, won or escaped, on foot and aboard; without it the next step rolls at once.
TEST(theGraceStepsCanStartAgainAfterEveryBattle) {
	for (int mode = 0; mode < 3; ++mode) {
		GameData d = cogwayRolls(3, 1, 1, 0);
		for (auto& [id, character] : d.db.characters) {
			character.base.agility = 250;
		}
		for (auto& [id, vehicle] : d.db.vehicles) {
			vehicle.stats.agility = 250;
		}
		d.areas.at("cogway").zones.at(0).random.graceAfterBattle = mode == 1;
		d.config.graceAfterBattle = mode == 2;
		for (bool aboard : {false, true}) {
			for (bool escape : {false, true}) {
				Session s(d, SessionOptions());
				cogway(s, {6, 4}, Facing::South, true);
				if (aboard) {
					CHECK(s.devVehicle("reed_skimmer", "cogway", "entry").empty());
					CHECK(s.board().empty());
				}
				s.face(Facing::East);
				const int y = s.world().position.y;
				for (int step = 1; step <= 3; ++step) {
					CHECK(!paceAndEnd(s, y, escape));
				}
				CHECK(paceAndEnd(s, y, escape));
				if (mode == 0) {
					CHECK(paceAndEnd(s, y, escape));
					continue;
				}
				CHECK_EQ(s.world().areaSteps, 0);
				for (int step = 1; step <= 3; ++step) {
					CHECK(!paceAndEnd(s, y, escape));
				}
				CHECK(paceAndEnd(s, y, escape));
				CHECK_EQ(s.world().areaSteps, 0);
			}
		}
	}
	const std::string zone = "{\"id\": \"a\", \"kind\": \"field\", \"map\": [\"...\"], \"markers\": {\"m\": [0, 0]}, \"zones\": [{\"id\": \"z\", \"from\": [0, 0], \"to\": [2, 0], "
		"\"table\": [{\"encounter\": \"training\", \"weight\": 1}], \"random\": {";
	const rules::Result<Area> graced = parseArea("a.json", zone + "\"grace_steps\": 2, \"grace_after_battle\": true}}]}");
	CHECK(graced.ok() && graced.value->zones[0].random.graceAfterBattle);
	const rules::Result<Area> refused = parseArea("a.json", zone + "\"grace_after_battle\": true}}]}");
	CHECK(!refused.ok() && refused.problems.size() == 1 && refused.problems[0].message.key == "data.grace_after_battle_needs_grace");
}

// R18: past the grace, battles come at 1 in step_chance per step on foot and 1 in vehicle_step_chance aboard, over seeds.
TEST(randomBattlesComeAtTheirChancePerStepOverSeeds) {
	const GameData d = cogwayRolls(3, 6, 12, 0);
	for (bool aboard : {false, true}) {
		int battles = 0;
		int steps = 0;
		for (std::uint64_t seed = 1; seed <= 8; ++seed) {
			SessionOptions options;
			options.seed = seed;
			Session s(d, options);
			cogway(s, {6, 4}, Facing::South, true);
			if (aboard) {
				CHECK(s.devVehicle("reed_skimmer", "cogway", "entry").empty());
				CHECK(s.board().empty());
			}
			s.face(Facing::East);
			const int y = s.world().position.y;
			for (int step = 1; step <= 3; ++step) {
				CHECK(!pace(s, y));
			}
			for (int step = 0; step < 240; ++step, ++steps) {
				battles += pace(s, y) ? 1 : 0;
			}
		}
		// 1920 steps: 320 expected at 1 in 6 (standard deviation 16), 160 at 1 in 12 (12).
		if (aboard) {
			CHECK(battles > 112 && battles < 208);
		} else {
			CHECK(battles > 256 && battles < 384);
		}
		CHECK_EQ(steps, 1920);
	}
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

// R33: a character's experience table drives the member detail's next level and the levels battle rewards bring.
TEST(experienceTablesReachTheMemberDetailAndRewards) {
	GameData d = test::data();
	rules::CharacterDef& fighter = d.db.characters.at("fighter");
	fighter.levelExperience = {1, 1, 1};
	fighter.levelStats = {fighter.base, rules::statsAtLevel(fighter, 2), rules::statsAtLevel(fighter, 3)};
	fighter.levelStats[1].maxHp += 40;
	Session s(d, SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	const std::optional<MemberDetail> before = s.memberDetail("fighter");
	CHECK(before && before->experienceToNext == 1);
	CHECK(s.devBattle("training").empty());
	CHECK(s.devEndBattle(true).empty());
	test::confirmLines(s);
	const rules::PartyMember& member = *s.world().member("fighter");
	CHECK(member.level >= 4);
	const std::optional<MemberDetail> after = s.memberDetail("fighter");
	CHECK(after && after->experienceToNext == rules::formulas::experienceToNextLevel(member.level) - member.experience);
	CHECK(s.devSetLevel("fighter", 2).empty());
	CHECK_EQ(rules::memberStats(d.db, *s.world().member("fighter")).maxHp - rules::memberStats(test::data().db, *s.world().member("fighter")).maxHp, 40);
}

// A chest's flag hooks fire as part of opening it, each once: a hook with a block flag the event sets, then a once hook
// after that event ends; closing a menu later fires neither again.
TEST(openingAChestRunsItsFlagHooksAtOnceAndOnlyOnce) {
	GameData d = test::data();
	Area& tidewater = d.areas.at("tidewater");
	Hook note;
	note.id = "crate_note";
	note.trigger = HookTrigger::Flag;
	note.event = "cove_quiet";
	note.requireFlag = "chest.tidewater_crate";
	note.blockFlag = "cove.quiet_seen";
	tidewater.hooks.push_back(note);
	Hook again = note;
	again.id = "crate_again";
	again.blockFlag.clear();
	again.once = true;
	tidewater.hooks.push_back(again);
	Session s(d, SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	CHECK(s.devWarpCell("tidewater", {10, 7}).empty());
	test::confirmLines(s);
	s.face(Facing::East);
	CHECK(s.interact().empty());
	CHECK(s.world().flag("chest.tidewater_crate"));
	int told = 0;
	while (s.linePending()) {
		told += s.line()->textId == "cove.quiet.tide" ? 1 : 0;
		s.confirmLine();
	}
	CHECK_EQ(told, 2);
	CHECK(s.mode() == Mode::Field);
	CHECK(s.openMenu().empty());
	CHECK(s.closeMenu().empty());
	CHECK(!s.linePending());
	CHECK(s.interact().empty());
	CHECK(!s.linePending());
}

// R48: a macro step that names a piece is kept by the editor, marked until the party has one to cast, saved and loaded
// back, and cast in battle.
TEST(macroStepsNamingPiecesAreKeptSavedAndCast) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	CHECK(s.openMenu().empty());
	CHECK(s.createMacro("fan").empty());
	rules::MacroStep step;
	step.member = "fighter";
	step.kind = rules::CommandKind::Item;
	step.id = "rivet_plate";
	step.piece = true;
	CHECK_EQ(s.setMacroStep("fan", 0, step).key, std::string("game.refuse.macro_piece_no_use"));
	step.id = "squall_fan";
	step.target.kind = rules::MacroTargetKind::Enemy;
	step.target.enemySlot = 0;
	CHECK(s.setMacroStep("fan", 0, step).empty());
	auto fanStep = [&s] { return s.macroViews().back().steps.at(0); };
	CHECK(fanStep().step.piece && fanStep().issue == rules::MacroIssue::NoItem && fanStep().commandName == "Squall Fan");
	CHECK(s.devGive("squall_fan", 1).empty());
	CHECK(fanStep().issue == rules::MacroIssue::None);

	const std::string text = serializeSave(s.makeSave(false));
	CHECK(text.find("step fighter piece squall_fan enemy - 0\n") != std::string::npos);
	const rules::Result<SaveFile> loaded = parseSave(text);
	CHECK(loaded.ok() && loaded.value->world.macros.back().steps.at(0).piece);

	CHECK(s.closeMenu().empty());
	CHECK(s.devBattle("training").empty());
	std::vector<rules::MacroStepResult> results;
	CHECK(s.applyMacro("fan", &results).empty());
	CHECK(results.size() == 1 && results[0].status == rules::MacroStepStatus::Applied && results[0].command.id == "squall_fan");
	bool cast = false;
	for (const rules::Event& e : s.resolveRound()) {
		cast = cast || (e.type == rules::EventType::ActionStarted && e.ability == "squall_fan" && e.actor == 0);
	}
	CHECK(cast);
}

// R49, R51: `autobattle random` plays the field's battles alone and leaves an event's battle to the script, which
// `fight` plays with a macro, a member that cannot run its step taking core's choice.
TEST(theTerminalLeavesEventBattlesToTheScriptUnderAutobattleRandom) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "hold_front", Facing::North);
	std::ostringstream out;
	RunnerOptions options;
	options.echo = false;
	Runner runner(s, out, options);
	runner.execute("autobattle random");
	CHECK(s.devBattle("training").empty());
	runner.settle();
	CHECK(s.mode() != Mode::Battle && s.endedBattle() && s.endedBattle()->source == BattleSource::Random);
	test::placeParty(s, "tidewater", "hold_front", Facing::North);
	runner.execute("talk");
	CHECK(s.mode() == Mode::Battle && s.battleSource() == BattleSource::Event);
	runner.execute("fight nothing");
	CHECK(runner.errors() == 1 && s.mode() == Mode::Battle);
	runner.execute("fight opener");
	CHECK(s.mode() != Mode::Battle && s.endedBattle() && s.endedBattle()->source == BattleSource::Event);
	CHECK_EQ(runner.errors(), 1);
}

// R52: the terminal log names an enemy that takes its next form.
TEST(theTerminalLogShowsAnEnemyTransforming) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	std::ostringstream out;
	Runner runner(s, out);
	CHECK(s.devBattle("pupa").empty());
	runner.settle();
	for (int round = 0; round < 4 && s.mode() == Mode::Battle; ++round) {
		runner.execute("defend fighter");
		runner.execute("round");
	}
	CHECK(out.str().find("  Moss Pupa becomes Moss Moth.\n") != std::string::npos);
}

namespace {

// A copy of the shipped data with `insert` put at the start of game.json's presentation object, checked.
CheckReport checkWithPresentation(const std::string& name, const std::string& insert, std::string* gameDir = nullptr) {
	const std::string root = test::freshDirectory(name);
	std::filesystem::create_directories(root + "/game");
	std::filesystem::create_directories(root + "/core");
	std::filesystem::copy("data", root + "/game/data", std::filesystem::copy_options::recursive);
	std::filesystem::copy("../core/data", root + "/core/data", std::filesystem::copy_options::recursive);
	std::string text;
	CHECK(readFile(root + "/game/data/game.json", text));
	text.replace(text.find("\"presentation\": {"), 17, "\"presentation\": {" + insert);
	std::string problem;
	CHECK(writeFileAtomic(root + "/game/data/game.json", text, problem));
	if (gameDir) {
		*gameDir = root;
	}
	return checkData(DataDirectories{root + "/game/data", root + "/core/data"});
}

bool hasProblem(const CheckReport& report, const char* key) {
	return std::any_of(report.problems.begin(), report.problems.end(), [&](const rules::Problem& p) { return p.message.key == key; });
}

std::vector<std::string> noticeKeys(Session& s) {
	std::vector<std::string> keys;
	for (const Output& o : s.drainOutput()) {
		if (o.kind == OutputKind::Notice) {
			keys.push_back(o.message.key);
		}
	}
	return keys;
}

}

// presentation.chest_opened_notice: off by default (the find alone); on, the opened line comes first, by the chest's
// kind when the language has that key, else the generic line; an empty chest says only that it is empty.
TEST(aChestCanSayItOpenedBeforeWhatItHeld) {
	auto open = [](GameData d) {
		Session s(d, SessionOptions());
		test::placeParty(s, "reed_path", "cache_front", Facing::North);
		s.drainOutput();
		CHECK(s.interact().empty());
		std::vector<std::string> keys = noticeKeys(s);
		CHECK(s.interact().empty());
		const std::vector<std::string> again = noticeKeys(s);
		CHECK(again == std::vector<std::string>{"game.chest_empty"});
		return keys;
	};
	const std::vector<std::string> found = {"game.found", "game.found_money"};
	CHECK(open(test::data()) == found);

	GameData on = test::data();
	on.config.presentation.chestOpenedNotice = true;
	CHECK(open(on) == (std::vector<std::string>{"game.chest_opened", "game.found", "game.found_money"}));

	GameData boxed = test::data();
	boxed.config.presentation.chestOpenedNotice = true;
	for (auto& [id, area] : boxed.areas) {
		for (Chest& chest : area.chests) {
			chest.kind = chest.id == "reed_cache" ? "crate" : chest.kind;
		}
	}
	CHECK(open(boxed) == (std::vector<std::string>{"game.chest_opened", "game.found", "game.found_money"}));
	for (LanguageTables& language : boxed.languages) {
		language.text["game.chest_opened.crate"] = "The crate has been pried open.";
	}
	CHECK(open(boxed) == (std::vector<std::string>{"game.chest_opened.crate", "game.found", "game.found_money"}));
	Session s(boxed, SessionOptions());
	test::placeParty(s, "reed_path", "cache_front", Facing::North);
	s.drainOutput();
	CHECK(s.interact().empty());
	const std::vector<Output> lines = s.drainOutput();
	CHECK(!lines.empty() && lines.front().text == "The crate has been pried open.");
}

// A chest's kind is an id; with the notice on, a language with neither the kind's key nor the generic one is a problem.
TEST(theDataCheckReadsChestKindsAndTheOpenedNoticesText) {
	CHECK(checkWithPresentation("chest_kind_ok", "\"chest_opened_notice\": true, ").problems.empty());
	std::string root;
	checkWithPresentation("chest_kind_bad", "\"chest_opened_notice\": true, ", &root);
	for (const char* lang : {"en", "ja"}) {
		std::string text;
		CHECK(readFile(root + "/game/data/text/" + lang + ".json", text));
		text.erase(text.find("\t\t\"game.chest_opened\""), text.find('\n', text.find("\t\t\"game.chest_opened\"")) - text.find("\t\t\"game.chest_opened\"") + 1);
		std::string problem;
		CHECK(writeFileAtomic(root + "/game/data/text/" + lang + ".json", text, problem));
	}
	auto kind = [&](const std::string& word) {
		std::string area;
		CHECK(readFile(root + "/game/data/areas/reed_path.json", area));
		const std::size_t at = area.find("\"id\": \"reed_cache\"");
		area.replace(at, area.find("\"at\"", at) - at, "\"id\": \"reed_cache\", \"kind\": \"" + word + "\", ");
		std::string problem;
		CHECK(writeFileAtomic(root + "/game/data/areas/reed_path.json", area, problem));
		return checkData(DataDirectories{root + "/game/data", root + "/core/data"});
	};
	const CheckReport missing = kind("crate");
	CHECK(hasProblem(missing, "data.chest_opened_text_missing"));
	CHECK(std::any_of(missing.problems.begin(), missing.problems.end(), [](const rules::Problem& p) {
		return p.message.params.size() == 2 && p.message.params[1] == "game.chest_opened.crate"; }));
	CHECK(hasProblem(kind("Old Box"), "data.bad_chest_kind"));
}

// Without presentation.options the built-in four levels, the index the profile's enum; with five declared levels the
// profile keeps the chosen id (written as text_speed_level), a new profile starts at the default, an old one's word
// finds its level, an enum change through setProfile moves to the level of that name, and an unknown id is refused.
TEST(speedLevelsComeFromTheDataWithTheBuiltInFourAsTheDefault) {
	Session plain(test::data(), SessionOptions());
	const OptionLevels& builtin = plain.optionLevels(OptionSetting::TextSpeed);
	CHECK(!builtin.declared && builtin.levels.size() == 4 && builtin.levels[3].id == "instant" && builtin.levels[1].value == 50);
	CHECK_EQ(plain.optionLevel(OptionSetting::TextSpeed), 1);
	CHECK_EQ(plain.optionLevels(OptionSetting::BattleSpeed).levels[2].value, 200);
	CHECK(plain.setOptionLevel(OptionSetting::TextSpeed, 2).empty());
	CHECK(plain.profile().textSpeed == TextSpeed::Fast && plain.profile().textSpeedLevel.empty());
	CHECK(plain.setOptionLevel(OptionSetting::BattleSpeed, 3).empty());
	CHECK(plain.profile().battleSpeed == BattleSpeed::X3);
	CHECK_EQ(plain.setOptionLevel(OptionSetting::TextSpeed, 4).key, std::string("game.refuse.unknown_option_level"));
	Profile wrong = plain.profile();
	wrong.textSpeedLevel = "brisk";
	CHECK_EQ(plain.setProfile(wrong).key, std::string("game.refuse.unknown_option_level"));

	const std::string five = "\"options\": {\"text_speed\": {\"levels\": [{\"id\": \"slowest\", \"value\": 12}, {\"id\": \"slow\", \"value\": 25}, "
		"{\"id\": \"normal\", \"value\": 50}, {\"id\": \"fast\", \"value\": 100}, {\"id\": \"instant\", \"value\": 0}], \"default\": \"slow\"}, "
		"\"battle_speed\": {\"levels\": [{\"id\": \"half\", \"label\": \"ui.multiplier.x1\", \"value\": 50}, {\"id\": \"1x\", \"value\": 100}, {\"id\": \"2x\", \"value\": 200}]}}, ";
	std::string root;
	CHECK(checkWithPresentation("speed_levels", five, &root).problems.empty());
	rules::Result<GameData> loaded = loadGameData(DataDirectories{root + "/game/data", root + "/core/data"});
	CHECK(loaded.ok());
	SessionOptions options;
	options.saveDirectory = root + "/saves";
	{
		Session s(*loaded.value, options);
		const OptionLevels& text = s.optionLevels(OptionSetting::TextSpeed);
		CHECK(text.declared && text.levels.size() == 5 && text.levels[0].label == "ui.text_speed.slowest");
		CHECK_EQ(s.optionLevel(OptionSetting::TextSpeed), 1);
		CHECK_EQ(s.optionLevel(OptionSetting::BattleSpeed), 1);
		CHECK(s.setOptionLevel(OptionSetting::TextSpeed, 0).empty());
		CHECK_EQ(s.profile().textSpeedLevel, std::string("slowest"));
		CHECK(s.setOptionLevel(OptionSetting::BattleSpeed, 0).empty());
	}
	{
		Session s(*loaded.value, options);
		CHECK_EQ(s.optionLevel(OptionSetting::TextSpeed), 0);
		CHECK_EQ(s.optionLevel(OptionSetting::BattleSpeed), 0);
		Profile p = s.profile();
		p.textSpeed = TextSpeed::Fast;
		CHECK(s.setProfile(p).empty());
		CHECK_EQ(s.optionLevel(OptionSetting::TextSpeed), 3);
		CHECK_EQ(s.profile().textSpeedLevel, std::string("fast"));
	}
	std::string written;
	CHECK(readFile(root + "/saves/profile.cfg", written));
	CHECK(written.find("text_speed_level fast\n") != std::string::npos && written.find("battle_speed_level half\n") != std::string::npos);
	Profile old;
	old.textSpeed = TextSpeed::Instant;
	std::string problem;
	std::filesystem::create_directories(root + "/old");
	CHECK(writeFileAtomic(root + "/old/profile.cfg", serializeProfile(old), problem));
	SessionOptions oldOptions;
	oldOptions.saveDirectory = root + "/old";
	Session fromOld(*loaded.value, oldOptions);
	CHECK_EQ(fromOld.optionLevel(OptionSetting::TextSpeed), 4);
	// A level id this game does not list, from another game's profile, is left alone and does not block other settings.
	Profile foreign;
	foreign.textSpeedLevel = "glacial";
	std::filesystem::create_directories(root + "/foreign");
	CHECK(writeFileAtomic(root + "/foreign/profile.cfg", serializeProfile(foreign), problem));
	SessionOptions foreignOptions;
	foreignOptions.saveDirectory = root + "/foreign";
	Session fromForeign(*loaded.value, foreignOptions);
	CHECK_EQ(fromForeign.optionLevel(OptionSetting::TextSpeed), 2);
	Profile louder = fromForeign.profile();
	louder.musicVolume = 40;
	CHECK(fromForeign.setProfile(louder).empty());
	CHECK(serializeProfile(old).find("text_speed_level") == std::string::npos);

	CHECK(hasProblem(checkWithPresentation("speed_levels_twice", "\"options\": {\"text_speed\": {\"levels\": [{\"id\": \"a\", \"value\": 1}, {\"id\": \"a\", \"value\": 2}]}}, "),
		"data.option_level_twice"));
	CHECK(hasProblem(checkWithPresentation("speed_levels_default", "\"options\": {\"battle_speed\": {\"levels\": [{\"id\": \"a\", \"value\": 100}], \"default\": \"b\"}}, "),
		"data.option_default_unknown"));
	CHECK(hasProblem(checkWithPresentation("speed_levels_empty", "\"options\": {\"text_speed\": {\"levels\": []}}, "), "data.option_levels_empty"));
	CHECK(hasProblem(checkWithPresentation("speed_levels_id", "\"options\": {\"text_speed\": {\"levels\": [{\"id\": \"Fast!\", \"value\": 9}]}}, "), "data.bad_option_level"));
}
