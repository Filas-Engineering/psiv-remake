#include "test.h"

#include "game/save.h"
#include "game/session.h"

#include "rules/progression.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace game;

namespace {

bool inLineup(const Session& s, const std::string& member) {
	return std::find(s.world().lineup.begin(), s.world().lineup.end(), member) != s.world().lineup.end();
}

char tile(const Session& s, Cell c) {
	return s.area()->rows[c.y][c.x];
}

}

// P4 with P2: the lodge gives the last place to one of two candidates and swaps them later; the one who leaves keeps
// wearing its pieces, and comes back having gained the share of the battles it missed.
TEST(theLodgeGivesTheLastPlaceToOneCandidateAndSwapsThemLater) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "ashfall_market", "lodge_front", Facing::West);
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(s.choice());
	CHECK(s.choose(1).empty());
	test::confirmLines(s);
	CHECK(inLineup(s, "harpooner"));
	CHECK(!inLineup(s, "lancer"));

	CHECK(s.interact().empty());
	CHECK(s.choose(0).empty());
	test::confirmLines(s);
	CHECK(inLineup(s, "lancer"));
	CHECK(!inLineup(s, "harpooner"));
	const rules::PartyMember* away = s.world().member("harpooner");
	CHECK(away && away->equipment.count(rules::EquipSlot::LeftHand) && away->equipment.at(rules::EquipSlot::Weapon) == "arc_disc");
}

// P2: game.json `leave_equipment` decides whether a member who leaves keeps its pieces or gives them back to the bag.
TEST(aMemberWhoLeavesKeepsOrGivesBackItsEquipment) {
	for (LeaveEquipment rule : {LeaveEquipment::Keep, LeaveEquipment::Bag}) {
		GameData d = test::withScript("events/steps.ev", "event steps_wayfarer unskippable\n\tparty add lancer\n\tparty remove lancer\nend\n");
		d.config.leaveEquipment = rule;
		Session s(d, SessionOptions());
		test::placeParty(s, "ember_steps", "west_gate", Facing::North);
		s.world().position = {3, 2};
		CHECK(s.interact().empty());
		test::confirmLines(s);
		const rules::PartyMember* lancer = s.world().member("lancer");
		CHECK(lancer && !inLineup(s, "lancer"));
		CHECK_EQ(lancer->equipment.empty(), rule == LeaveEquipment::Bag);
		CHECK_EQ(s.world().equipmentBag.count("kelp_cleaver"), std::size_t(rule == LeaveEquipment::Bag ? 1 : 0));
	}
}

// P10 through the session: a piece goes in the hand named, and the comparison says what it sends back.
TEST(handPiecesGoInTheHandNamedAndThePreviewSaysWhatComesOff) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "ashfall_market", "lodge_front", Facing::West);
	s.world().equipmentBag = {{"gill_knife", 1}, {"kelp_cleaver", 1}, {"driftwood_buckler", 1}};
	CHECK(s.openMenu().empty());
	CHECK(s.equip("fighter", "gill_knife", rules::EquipSlot::LeftHand).empty());
	const std::optional<EquipPreview> cleaver = s.equipPreview("fighter", "kelp_cleaver");
	CHECK(cleaver && cleaver->position == rules::EquipSlot::Weapon && cleaver->replaces == "copper_blade" && cleaver->alsoReplaces == "gill_knife");
	CHECK(cleaver && cleaver->after.stats.strength == cleaver->before.stats.strength - 4 - 3 + 12);
	const std::optional<EquipPreview> buckler = s.equipPreview("fighter", "driftwood_buckler");
	CHECK(buckler && buckler->position == rules::EquipSlot::LeftHand && buckler->replaces == "gill_knife" && buckler->alsoReplaces.empty());
	CHECK_EQ(s.equip("fighter", "kelp_cleaver", rules::EquipSlot::LeftHand).key, std::string("equip.wrong_position"));
	CHECK(s.equip("fighter", "kelp_cleaver").empty());
	CHECK_EQ(s.world().equipmentBag.at("gill_knife"), 1);
	CHECK(!s.world().member("fighter")->equipment.count(rules::EquipSlot::LeftHand));
}

// F1: an area plays random-only by its data, and one game.json switch turns every area random.
TEST(randomOnlyIsPerAreaAndOneSwitchTurnsEveryAreaRandom) {
	Session visible(test::data(), SessionOptions());
	test::placeParty(visible, "reed_path", "west_gate", Facing::East);
	CHECK(visible.encounterMode() == EncounterMode::Visible);
	test::placeParty(visible, "fell_moor", "town_gate", Facing::East);
	CHECK(visible.encounterMode() == EncounterMode::Random);

	GameData everywhere = test::data();
	CHECK(!everywhere.config.randomEncountersEverywhere);
	everywhere.config.randomEncountersEverywhere = true;
	Session random(everywhere, SessionOptions());
	random.newGame();
	test::confirmLines(random);
	CHECK(random.encounterMode() == EncounterMode::Random);
	CHECK(random.world().enemies.empty());
	CHECK(random.devWarp("reed_path", "west_gate").empty());
	CHECK(random.world().enemies.empty());
	CHECK(random.encounterMode() == EncounterMode::Random);
}

// F2: the barge is walked aboard at a ship's port, sailed from its helm to the open ports of the same ship only, and
// left again by its gangway; a save aboard keeps the port it boarded from.
TEST(theShipIsWalkedAboardSailedFromItsHelmAndLeftByItsGangway) {
	SessionOptions options;
	options.saveDirectory = test::freshDirectory("world_ship");
	Session s(test::data(), options);
	test::placeParty(s, "cinder_quay", "dock_front", Facing::South);
	CHECK_EQ(s.interact().key, std::string("game.refuse.port_closed"));
	s.world().flags["travel.dock_open"] = true;
	s.world().flags["travel.isles_open"] = true;
	CHECK(s.interact().empty());
	CHECK_EQ(s.world().area, std::string("lantern_barge"));
	CHECK_EQ(s.world().shipPort, std::string("quay_dock"));
	s.world().position = {4, 2};
	s.world().facing = Facing::North;
	CHECK(s.interact().empty());
	CHECK(s.menuKind() == MenuKind::Port);
	const std::vector<TravelDestination> to = s.travelDestinations(TravelKind::Port);
	CHECK_EQ(to.size(), std::size_t(1));
	CHECK(!to.empty() && to.front().id == "fell_dock");
	CHECK(s.closeMenu().empty());
	CHECK(s.openMenu().empty());
	CHECK(s.saveManual("manual_0").empty());
	CHECK(s.closeMenu().empty());
	for (int i = 0; i < 4; ++i) {
		CHECK(s.walk(Facing::West).empty());
	}
	CHECK_EQ(s.world().area, std::string("cinder_quay"));
	CHECK(s.world().position == test::data().area("cinder_quay")->markers.at("dock_front"));
	CHECK(s.world().shipPort.empty());
	s.quitToTitle();
	CHECK(s.load("manual_0").empty());
	CHECK_EQ(s.world().shipPort, std::string("quay_dock"));
	s.world().position = {4, 2};
	s.world().facing = Facing::North;
	CHECK(s.interact().empty());
	CHECK_EQ(s.travel(TravelKind::Port, "quay_pier").key, std::string("game.refuse.no_destination"));
	CHECK(s.travel(TravelKind::Port, "fell_dock").empty());
	CHECK_EQ(s.world().area, std::string("ashfall_market"));
	CHECK(s.world().shipPort.empty());
}

// F3: a vehicle comes from the item list outdoors only, crosses its own terrain, and the borer breaks a barrier for
// good; a vehicle whose breaks do not last finds the barrier whole once the party has left the area.
TEST(vehiclesAreCalledOutdoorsAndTheBorerBreaksBarriers) {
	for (bool forGood : {true, false}) {
		GameData d = test::data();
		for (FieldVehicle& v : d.config.vehicles) {
			v.breaksForGood = v.id == "rime_borer" ? forGood : v.breaksForGood;
		}
		Session s(d, SessionOptions());
		test::placeParty(s, "ashfall_market", "east_gate", Facing::East);
		s.world().items["rime_bell"] = 1;
		s.world().items["dune_horn"] = 1;
		CHECK(s.openMenu().empty());
		CHECK_EQ(s.useItem("rime_bell", "").key, std::string("game.refuse.vehicle_not_owned"));
		s.world().vehicles["rime_borer"] = VehiclePlace{"fell_moor", {4, 2}, Facing::South, "fell_moor", {4, 2}};
		s.world().vehicles["dune_crawler"] = VehiclePlace{"fell_moor", {2, 2}, Facing::South, "fell_moor", {2, 2}};
		CHECK_EQ(s.useItem("rime_bell", "").key, std::string("game.refuse.call_outdoors"));
		CHECK(s.closeMenu().empty());
		CHECK(s.walk(Facing::East).empty());
		CHECK_EQ(s.world().area, std::string("fell_moor"));
		CHECK(s.openMenu().empty());
		CHECK(s.useItem("dune_horn", "").empty());
		CHECK_EQ(s.world().aboard, std::string("dune_crawler"));
		CHECK(s.mode() == Mode::Field);
		for (int i = 0; i < 9 && s.mode() == Mode::Field; ++i) {
			s.walk(Facing::East);
			test::fightToTheEnd(s);
		}
		CHECK_EQ(s.world().position.x, 10);
		CHECK(s.leaveVehicle().empty());
		CHECK(s.openMenu().empty());
		CHECK(s.useItem("rime_bell", "").empty());
		s.world().position = {12, 4};
		s.world().vehicles["rime_borer"].cell = {12, 4};
		CHECK_EQ(tile(s, {13, 4}), '%');
		s.walk(Facing::East);
		test::fightToTheEnd(s);
		CHECK_EQ(tile(s, {13, 4}), '.');
		CHECK(s.world().brokenCells.count("fell_moor") == (forGood ? 1u : 0u));
		CHECK(s.leaveVehicle().empty());
		CHECK(s.devWarp("ashfall_market", "east_gate").empty());
		CHECK(s.devWarp("fell_moor", "town_gate").empty());
		CHECK_EQ(tile(s, {13, 4}), forGood ? '.' : '%');
	}
}

// F7 and F10: shops of each kind, stock that grows with a story flag, a pair that opens later, and shops that close
// while the market is emptied.
TEST(shopsHaveKindsAndTheirStockAndDoorsFollowTheStory) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "ashfall_market", "tools_front", Facing::North);
	const Area& market = *s.area();
	std::vector<ShopKind> kinds;
	for (const Shop& shop : market.shops) {
		kinds.push_back(shop.kind);
	}
	for (ShopKind kind : {ShopKind::Weapon, ShopKind::Armor, ShopKind::Tool, ShopKind::Gift}) {
		CHECK(std::count(kinds.begin(), kinds.end(), kind) >= 1);
	}
	const Shop& tools = *market.shop("ashfall_tools");
	CHECK_EQ(s.stock(tools).size(), std::size_t(2));
	s.world().flags["fells.bridge_mended"] = true;
	CHECK_EQ(s.stock(tools).size(), std::size_t(3));
	CHECK(s.interact().empty());
	CHECK(s.mode() == Mode::Shop);
	CHECK(s.buy("spark_flask", 1).empty());
	CHECK(s.closeMenu().empty());

	s.world().flags["fells.ash_storm"] = true;
	CHECK_EQ(s.interact().key, std::string("game.refuse.shop_closed"));
	s.world().flags["fells.ash_storm"] = false;
	CHECK(!s.shopOpen(*market.shop("ashfall_fine_arms")));
	s.world().flags["fells.guild_open"] = true;
	CHECK(s.shopOpen(*market.shop("ashfall_fine_arms")));
}

// F8 and F10: cells that change by flag (the bridge, the sealed shrine, the vault's gate, door and illusion), a pit
// that drops to the floor below, a transport tile, and a door that opens only for a party holding its key.
TEST(mapCellsChangeByFlagAndTheVaultsMechanismsWork) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "ashfall_market", "bridge_west", Facing::East);
	CHECK_EQ(s.walk(Facing::East).key, std::string("game.refuse.blocked"));
	s.world().flags["fells.bridge_mended"] = true;
	CHECK_EQ(tile(s, {7, 4}), '.');
	CHECK(s.walk(Facing::East).empty());
	CHECK_EQ(s.world().position.x, 7);

	CHECK(s.devWarp("fell_vault", "lock_front").empty());
	s.world().facing = Facing::East;
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(!s.world().flag("fells.vault_door_open"));
	CHECK_EQ(tile(s, {8, 4}), '#');
	s.world().items["vault_key"] = 1;
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(s.world().flag("fells.vault_door_open"));
	CHECK_EQ(tile(s, {8, 4}), '.');

	s.world().position = {6, 5};
	CHECK(s.walk(Facing::South).empty());
	CHECK_EQ(s.world().area, std::string("fell_vault_lower"));
	CHECK(test::outputHas(s.drainOutput(), "The floor gives way!"));
	CHECK_EQ(tile(s, {4, 2}), '#');
	s.world().flags["fells.vault_cleared"] = true;
	CHECK_EQ(tile(s, {4, 2}), '.');

	CHECK(s.devWarp("fell_vault", "warden_front").empty());
	s.world().position = {13, 6};
	CHECK(s.walk(Facing::South).empty());
	CHECK_EQ(s.world().area, std::string("fell_vault"));
	CHECK(s.world().position == test::data().area("fell_vault")->markers.at("glyph_landing"));
}

// Names in the profile's language come from the text tables' `name.<kind>.<id>` keys, the data's names otherwise.
TEST(namesComeFromTheTextTablesInEachLanguage) {
	Session s(test::data(), SessionOptions());
	CHECK_EQ(s.displayName("squall_kite"), std::string("Squall Kite"));
	Profile ja = s.profile();
	ja.textLanguage = "ja";
	CHECK(s.setProfile(ja).empty());
	CHECK_EQ(s.displayName("squall_kite"), std::string("突風の凧鳥"));
	CHECK_EQ(s.displayName("kelp_cleaver"), std::string("昆布の大鉈"));
	CHECK_EQ(s.displayName("lancer"), std::string("槍使い"));
	CHECK_EQ(s.difficulties().front().name, std::string("ハード"));
	CHECK(namesKnownThing(test::data().db, "name.vehicle.rime_borer"));
	CHECK(!namesKnownThing(test::data().db, "name.vehicle.ferry"));
	CHECK(!namesKnownThing(test::data().db, "name.weather.rain"));
}
