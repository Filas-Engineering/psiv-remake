#include "test.h"

#include "game/save.h"
#include "game/session.h"

#include "rules/progression.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace game;

namespace {

// Walks the leader to `goal` along the game's path walk, fighting any battle on the way with core's policy.
void walkTo(Session& s, Cell goal) {
	PathWalk walk = s.beginWalk(goal);
	for (int i = 0; i < 400; ++i) {
		const WalkStatus status = s.walkStep(walk);
		test::confirmLines(s);
		test::fightToTheEnd(s);
		if (status != WalkStatus::Walking && s.mode() == Mode::Field) {
			return;
		}
	}
}

// One step east at a time until the leader stands on `x`, fighting what comes; the vehicle battles seen are counted.
int driveEastTo(Session& s, int x) {
	int vehicleBattles = 0;
	for (int i = 0; i < 100 && s.world().position.x < x; ++i) {
		s.walk(Facing::East);
		if (s.mode() == Mode::Battle) {
			vehicleBattles += s.battle()->setup().flags.vehicleBattle ? 1 : 0;
			CHECK_EQ(s.battle()->setup().vehicle, std::string("reed_skimmer"));
			test::fightToTheEnd(s);
		}
	}
	return vehicleBattles;
}

// A party at the brine flats' mooring post that has just been given the skimmer.
Session& moored(Session& s) {
	test::placeParty(s, "brine_flats", "post_front", Facing::East);
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(s.world().vehicles.count("reed_skimmer"));
	return s;
}

const VehicleView* findVehicle(const std::vector<VehicleView>& views, const std::string& id) {
	auto it = std::find_if(views.begin(), views.end(), [&](const VehicleView& v) { return v.id == id; });
	return it == views.end() ? nullptr : &*it;
}

}

TEST(theSkimmerIsGivenBoardedAndCrossesWater) {
	Session s(test::data(), SessionOptions());
	moored(s);
	CHECK(test::outputHas(s.drainOutput(), "The Reed Skimmer is yours to ride."));
	const std::vector<VehicleView> here = s.vehiclesHere();
	const VehicleView* skimmer = findVehicle(here, "reed_skimmer");
	CHECK(skimmer && skimmer->cell == (Cell{7, 4}) && !skimmer->aboard);

	// On foot the water stops the party and the moored skimmer is in the way.
	test::placeParty(s, "brine_flats", "mooring", Facing::East);
	s.world().position = {7, 3};
	CHECK_EQ(s.walk(Facing::East).key, std::string("game.refuse.blocked"));
	CHECK_EQ(s.walk(Facing::South).key, std::string("game.refuse.blocked"));

	s.world().position = {6, 4};
	CHECK(s.face(Facing::East).empty());
	CHECK(s.interact().empty());
	CHECK_EQ(s.world().aboard, std::string("reed_skimmer"));
	CHECK(s.world().position == (Cell{7, 4}));
	CHECK(s.riding() && s.riding()->speedPercent == 200);
	CHECK_EQ(s.interact().key, std::string("game.refuse.aboard"));

	// Aboard, the skimmer crosses the water twice as fast as walking, and battles on the way are vehicle battles.
	const std::int64_t before = s.world().playTimeMs;
	const int battles = driveEastTo(s, 19);
	CHECK(s.world().position == (Cell{19, 4}));
	CHECK(battles >= 1);
	if (battles == 0) {
		CHECK(s.world().playTimeMs - before == 12 * 125);
	}
	CHECK(s.world().vehicles.at("reed_skimmer").cell == (Cell{19, 4}));
	CHECK(s.world().vehicles.at("reed_skimmer").homeArea == "brine_flats" && s.world().vehicles.at("reed_skimmer").home == (Cell{7, 4}));
}

TEST(leavingNeedsDryGroundAndTheVehicleStaysWhereItWasLeft) {
	Session s(test::data(), SessionOptions());
	moored(s);
	test::placeParty(s, "brine_flats", "mooring", Facing::East);
	s.world().position = {6, 4};
	CHECK(s.interact().empty());
	// Out on the water every neighbor is water.
	s.world().position = {12, 4};
	s.world().vehicles["reed_skimmer"].cell = {12, 4};
	CHECK_EQ(s.leaveVehicle().key, std::string("game.refuse.nowhere_leave"));
	CHECK_EQ(s.world().aboard, std::string("reed_skimmer"));
	// At the islet's shore the faced cell comes first, then north, east, south, west.
	s.world().position = {18, 4};
	s.world().vehicles["reed_skimmer"].cell = {18, 4};
	CHECK(s.face(Facing::East).empty());
	CHECK(s.leaveVehicle().empty());
	CHECK(s.world().aboard.empty());
	CHECK(s.world().position == (Cell{19, 4}));
	CHECK(s.world().vehicles.at("reed_skimmer").cell == (Cell{18, 4}));
	CHECK_EQ(s.leaveVehicle().key, std::string("game.refuse.not_aboard"));
	// What was behind the water: the crate and the beacon that opens the ferry.
	walkTo(s, {21, 2});
	CHECK(s.face(Facing::North).empty());
	CHECK(s.interact().empty());
	CHECK_EQ(s.world().items.at("homing_shell"), 2);
	walkTo(s, {22, 4});
	CHECK(s.face(Facing::East).empty());
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(s.world().flag("travel.isles_open"));
}

TEST(aVehicleWaitsOutsideTownsAndReturnsWhenLeftOutOfReach) {
	Session s(test::data(), SessionOptions());
	moored(s);
	test::placeParty(s, "brine_flats", "mooring", Facing::East);
	s.world().position = {6, 4};
	CHECK(s.interact().empty());
	// Driving through the exit into Saltgrass Market leaves the skimmer on the last cell before it.
	CHECK(s.face(Facing::West).empty());
	for (int i = 0; i < 10 && s.world().area == "brine_flats"; ++i) {
		s.walk(Facing::West);
		test::fightToTheEnd(s);
	}
	CHECK_EQ(s.world().area, std::string("saltgrass"));
	CHECK(s.world().aboard.empty());
	CHECK(test::outputHas(s.drainOutput(), "The Reed Skimmer waits outside."));
	CHECK(s.world().vehicles.at("reed_skimmer").area == "brine_flats" && s.world().vehicles.at("reed_skimmer").cell == (Cell{1, 4}));

	// A return to a town while aboard on open water leaves the skimmer out of reach, so it goes back to where it was
	// boarded (FLD-063).
	test::placeParty(s, "brine_flats", "mooring", Facing::East);
	s.world().vehicles["reed_skimmer"] = {"brine_flats", {13, 4}, Facing::East, "brine_flats", {7, 4}};
	s.world().aboard = "reed_skimmer";
	s.world().position = {13, 4};
	s.world().items["homing_shell"] = 1;
	CHECK(s.openMenu().empty());
	CHECK(s.travel(TravelKind::Town, "saltgrass", "", "homing_shell").empty());
	CHECK(s.world().aboard.empty());
	CHECK_EQ(s.world().area, std::string("saltgrass"));
	CHECK(test::outputHas(s.drainOutput(), "The Reed Skimmer drifts back to where you boarded it."));
	CHECK(s.world().vehicles.at("reed_skimmer").area == "brine_flats" && s.world().vehicles.at("reed_skimmer").cell == (Cell{7, 4}));
}

TEST(scriptsGivePlaceAndRemoveVehiclesAndTestForThem) {
	const GameData d = test::withScript("events/flats.ev", R"(event flats_mooring unskippable
	if vehicle reed_skimmer
		vehicle remove reed_skimmer
	else
		vehicle give reed_skimmer brine_flats mooring
	end
	if not vehicle reed_skimmer
		set travel.isles_open
	end
end
event flats_beacon unskippable
	if aboard reed_skimmer
		vehicle place reed_skimmer brine_flats islet_shore
	end
end
)");
	Session s(d, SessionOptions());
	test::placeParty(s, "brine_flats", "post_front", Facing::East);
	const int autosaves = s.autosaveCount();
	CHECK(s.interact().empty());
	CHECK(s.world().vehicles.count("reed_skimmer"));
	CHECK(!s.world().flag("travel.isles_open"));
	// Acquiring a vehicle is an autosave point (FLOW-050).
	CHECK_EQ(s.autosaveCount(), autosaves + 1);
	CHECK(s.interact().empty());
	CHECK(!s.world().vehicles.count("reed_skimmer"));
	CHECK(s.world().flag("travel.isles_open"));
}

TEST(vehicleStateRoundTripsThroughSavesAboardAndOnFoot) {
	Session s(test::data(), SessionOptions());
	moored(s);
	test::placeParty(s, "brine_flats", "mooring", Facing::East);
	s.world().position = {6, 4};
	CHECK(s.interact().empty());
	s.world().dungeonExitArea = "saltgrass";
	s.world().dungeonExitCell = {46, 12};
	for (bool aboard : {true, false}) {
		if (!aboard) {
			s.world().position = {18, 4};
			s.world().vehicles["reed_skimmer"].cell = {18, 4};
			CHECK(s.face(Facing::East).empty());
			CHECK(s.leaveVehicle().empty());
		}
		const SaveFile save = s.makeSave(false);
		const std::string text = serializeSave(save);
		CHECK(text.find("chunk travel 2 ") != std::string::npos);
		rules::Result<SaveFile> parsed = parseSave(text);
		std::string diff;
		CHECK(parsed.ok() && sameWorld(parsed.value->world, save.world, &diff));
		CHECK_EQ(diff, std::string(""));
		CHECK(parsed.ok() && parsed.value->world.aboard == (aboard ? "reed_skimmer" : ""));
		CHECK(parsed.ok() && serializeSave(*parsed.value) == text);
	}
	// A save that says the party rides a vehicle it does not have is refused.
	std::string bad = serializeSave(s.makeSave(false));
	bad.replace(bad.find("aboard -"), 8, "aboard XY");
	CHECK(!parseSave(bad).ok());
}

TEST(theFerryRunsBetweenOpenPorts) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "saltgrass", "pier_front", Facing::North);
	CHECK_EQ(s.interact().key, std::string("game.refuse.port_closed"));
	CHECK(s.travelDestinations(TravelKind::Port).empty());
	s.world().flags["travel.isles_open"] = true;
	CHECK(s.interact().empty());
	CHECK(s.menuKind() == MenuKind::Port);
	CHECK(s.port() && s.port()->id == "saltgrass_pier");
	const std::vector<TravelDestination> to = s.travelDestinations(TravelKind::Port);
	CHECK(to.size() == 1 && to[0].id == "quay_pier" && to[0].area == "cinder_quay" && to[0].region == "isles");
	CHECK_EQ(s.travel(TravelKind::Port, "saltgrass_pier").key, std::string("game.refuse.no_destination"));
	CHECK(s.travel(TravelKind::Port, "quay_pier").empty());
	CHECK(s.mode() == Mode::Field);
	CHECK_EQ(s.world().area, std::string("cinder_quay"));
	CHECK(s.world().position == (Cell{6, 5}));
	CHECK_EQ(s.travel(TravelKind::Port, "saltgrass_pier").key, std::string("game.refuse.no_port"));
}

TEST(aReturnReachesVisitedTownsOfTheRegionOnly) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
	s.world().items["homing_shell"] = 2;
	s.world().visitedAreas = {"tidewater", "reed_path", "saltgrass", "cinder_quay"};
	CHECK_EQ(s.travel(TravelKind::Town, "tidewater", "", "homing_shell").key, std::string("game.refuse.travel_menu_only"));
	CHECK(s.openMenu().empty());
	CHECK_EQ(s.useItem("homing_shell", "").key, std::string("game.refuse.travel_item"));
	std::vector<TravelDestination> towns = s.travelDestinations(TravelKind::Town);
	CHECK(towns.size() == 2 && towns[0].id == "saltgrass" && towns[1].id == "tidewater");
	CHECK_EQ(s.travel(TravelKind::Town, "cinder_quay", "", "homing_shell").key, std::string("game.refuse.no_destination"));
	CHECK_EQ(s.travel(TravelKind::Town, "tidewater", "", "tonic").key, std::string("game.refuse.not_travel"));
	CHECK_EQ(s.travel(TravelKind::Town, "tidewater", "", "waymark_chalk").key, std::string("game.refuse.not_travel"));
	CHECK(s.travel(TravelKind::Town, "tidewater", "", "homing_shell").empty());
	CHECK(s.mode() == Mode::Field);
	CHECK_EQ(s.world().area, std::string("tidewater"));
	CHECK(s.world().position == (Cell{3, 4}));
	CHECK_EQ(s.world().items.at("homing_shell"), 1);

	// In the isles the coast towns are out of reach, and the town the party stands in is no destination.
	test::placeParty(s, "cinder_quay", "pier_front", Facing::North);
	CHECK(s.openMenu().empty());
	CHECK(s.travelDestinations(TravelKind::Town).empty());
	CHECK_EQ(s.travel(TravelKind::Town, "saltgrass", "", "homing_shell").key, std::string("game.refuse.no_town"));
	CHECK(s.closeMenu().empty());
	test::placeParty(s, "ember_steps", "west_gate", Facing::East);
	CHECK(s.openMenu().empty());
	towns = s.travelDestinations(TravelKind::Town);
	CHECK(towns.size() == 1 && towns[0].id == "cinder_quay");

	// The technique costs its points; one the user has not learned is core's refusal.
	CHECK_EQ(s.travel(TravelKind::Town, "cinder_quay", "fighter", "homeward").key, std::string("field.not_known"));
}

TEST(leavingADungeonReturnsToWhereThePartyWentIn) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "saltgrass", "east_gate", Facing::East);
	s.world().items["waymark_chalk"] = 1;
	s.world().items["homing_shell"] = 1;
	CHECK(s.walk(Facing::East).empty());
	test::fightToTheEnd(s);
	test::confirmLines(s);
	CHECK_EQ(s.world().area, std::string("kiln_upper"));
	CHECK(s.world().dungeonExitArea == "saltgrass" && s.world().dungeonExitCell == (Cell{46, 12}));
	if (s.mode() == Mode::Event) {
		s.skip();
		test::confirmLines(s);
	}
	CHECK(s.openMenu().empty());
	CHECK_EQ(s.travel(TravelKind::Town, "tidewater", "", "homing_shell").key, std::string("game.refuse.in_dungeon"));
	const std::vector<TravelDestination> out = s.travelDestinations(TravelKind::DungeonExit);
	CHECK(out.size() == 1 && out[0].area == "saltgrass" && out[0].cell == (Cell{46, 12}));
	CHECK(s.travel(TravelKind::DungeonExit, "", "", "waymark_chalk").empty());
	CHECK_EQ(s.world().area, std::string("saltgrass"));
	CHECK(s.world().position == (Cell{46, 12}));
	CHECK(!s.world().items.count("waymark_chalk"));
	CHECK(s.world().dungeonExitArea.empty());
	CHECK(s.openMenu().empty());
	CHECK_EQ(s.travel(TravelKind::DungeonExit, "", "", "homing_shell").key, std::string("game.refuse.not_travel"));
	s.world().items["waymark_chalk"] = 1;
	CHECK_EQ(s.travel(TravelKind::DungeonExit, "", "", "waymark_chalk").key, std::string("game.refuse.not_dungeon"));

	// The mystic learns Homeward at level 9 and pays its points.
	s.world().lineup = {"fighter", "mystic"};
	s.world().roster.push_back(rules::makePartyMember(test::data().db, *test::data().db.character("mystic"), 9));
	const int tp = s.world().member("mystic")->tp;
	CHECK(s.travel(TravelKind::Town, "tidewater", "mystic", "homeward").empty());
	CHECK_EQ(s.world().member("mystic")->tp, tp - 8);
	CHECK_EQ(s.world().area, std::string("tidewater"));
}
