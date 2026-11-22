#include "test.h"

#include "game/runner.h"
#include "rules/progression.h"

#include <sstream>

using namespace game;

namespace {

void openStall(Session& s) {
	test::placeParty(s, "tidewater", "stall_front", Facing::North);
	CHECK(s.interact().empty());
	CHECK(s.mode() == Mode::Shop);
	CHECK(s.shop() != nullptr);
}

}

TEST(shopBuysWithinMoneyAndCarryLimits) {
	Session s(test::data(), SessionOptions());
	openStall(s);
	s.world().money = 50;
	s.world().items["tonic"] = 1;
	CHECK(s.buy("tonic", 3).empty());
	CHECK_EQ(s.world().money, 26);
	CHECK_EQ(s.world().items["tonic"], 4);
	CHECK(!s.buy("second_wind", 1).empty());
	CHECK_EQ(s.world().money, 26);
	CHECK(!s.buy("wake_chime", 1).empty());
	CHECK(!s.buy("tonic", 0).empty());
	s.world().money = 10000;
	CHECK(!s.buy("tonic", 96).empty());
	CHECK(s.buy("tonic", 95).empty());
	CHECK_EQ(s.world().items["tonic"], 99);
	CHECK(s.buy("grip_bands", 1).empty());
	CHECK_EQ(s.world().equipmentBag["grip_bands"], 1);
	CHECK(s.closeMenu().empty());
	CHECK(s.mode() == Mode::Field);
}

TEST(shopSellsAtItsPercentAndNeverWornEquipment) {
	Session s(test::data(), SessionOptions());
	openStall(s);
	s.world().money = 0;
	s.world().items["second_wind"] = 2;
	CHECK(s.sell("second_wind", 2).empty());
	CHECK_EQ(s.world().money, 30);
	CHECK(!s.world().items.count("second_wind"));
	CHECK(!s.sell("second_wind", 1).empty());
	CHECK(!s.sell("rivet_plate", 1).empty());
	CHECK(!s.sell("copper_blade", 1).empty());
	CHECK_EQ(s.world().member("fighter")->equipment.at(rules::EquipSlot::Weapon), std::string("copper_blade"));
}

TEST(restCostsMoneyRefillsTheRosterAndAutosaves) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "inn_front", Facing::West);
	rules::PartyMember& fighter = *s.world().member("fighter");
	fighter.hp = 3;
	fighter.tp = 0;
	s.world().money = 5;
	CHECK(s.interact().empty());
	CHECK(s.inn() != nullptr);
	CHECK(!s.rest().empty());
	CHECK_EQ(fighter.hp, 3);
	s.world().money = 25;
	const int before = s.autosaveCount();
	CHECK(s.rest().empty());
	CHECK_EQ(s.world().money, 15);
	const rules::Stats stats = rules::memberStats(test::data().db, fighter);
	CHECK_EQ(fighter.hp, stats.maxHp);
	CHECK_EQ(fighter.tp, stats.maxTp);
	CHECK(s.mode() == Mode::Field);
	CHECK_EQ(s.autosaveCount(), before + 1);
}

TEST(chestsOpenOnceAndRemember) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "bell_cove", "box_front", Facing::South);
	const int tonics = s.world().items["tonic"];
	CHECK(s.interact().empty());
	CHECK_EQ(s.world().items["tonic"], tonics + 1);
	CHECK_EQ(s.world().equipmentBag["rivet_plate"], 1);
	CHECK(s.world().flag("chest.cove_box"));
	CHECK(s.interact().empty());
	CHECK_EQ(s.world().items["tonic"], tonics + 1);
	CHECK(test::outputHas(s.drainOutput(), "empty"));
}

TEST(fieldItemsUseCoreFormulasAndAreKeptWhenUseless) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	s.world().items = {{"tonic", 2}, {"second_wind", 1}, {"spark_flask", 1}};
	CHECK(s.openMenu().empty());
	rules::PartyMember& fighter = *s.world().member("fighter");
	CHECK(!s.useItem("tonic", "fighter").empty());
	CHECK_EQ(s.world().items["tonic"], 2);
	fighter.hp = 10;
	CHECK(s.useItem("tonic", "fighter").empty());
	CHECK_EQ(fighter.hp, 40);
	CHECK_EQ(s.world().items["tonic"], 1);
	CHECK(!s.useItem("second_wind", "fighter").empty());
	fighter.hp = 0;
	CHECK(!s.useItem("tonic", "fighter").empty());
	CHECK(s.useItem("second_wind", "fighter").empty());
	CHECK_EQ(fighter.hp, rules::memberStats(test::data().db, fighter).maxHp * 30 / 100);
	CHECK(!s.useItem("spark_flask", "fighter").empty());
	CHECK(!s.useItem("tonic", "mystic").empty());
}

TEST(equipmentSwapsThroughTheBag) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	s.world().equipmentBag["rivet_plate"] = 1;
	s.world().equipmentBag["ember_rod"] = 1;
	CHECK(s.openMenu().empty());
	CHECK(!s.equip("fighter", "ember_rod").empty());
	CHECK(s.equip("fighter", "rivet_plate").empty());
	CHECK_EQ(s.world().member("fighter")->equipment.at(rules::EquipSlot::Body), std::string("rivet_plate"));
	CHECK(!s.world().equipmentBag.count("rivet_plate"));
	CHECK_EQ(s.world().equipmentBag["padded_coat"], 1);
	CHECK(s.unequip("fighter", rules::EquipSlot::Body).empty());
	CHECK_EQ(s.world().equipmentBag["rivet_plate"], 1);
}

TEST(levelUpsReportLearnedAbilities) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
	s.world().roster.push_back(rules::makePartyMember(test::data().db, *test::data().db.character("medic"), 3));
	s.world().lineup.push_back("medic");
	rules::PartyMember& medic = s.world().roster.back();
	medic.experience = 144;
	s.world().roster[0] = rules::makePartyMember(test::data().db, *test::data().db.character("fighter"), 20);
	VisibleEnemy e;
	e.id = 7;
	e.zone = "west_reeds";
	e.encounter = "training";
	e.spawn = e.position = {2, 4};
	s.world().enemies.push_back(e);
	s.walk(Facing::East);
	CHECK(s.mode() == Mode::Battle);
	test::fightToTheEnd(s);
	CHECK(s.mode() == Mode::Field);
	CHECK_EQ(s.world().member("medic")->level, 4);
	const std::vector<Output> output = s.drainOutput();
	CHECK(test::outputHas(output, "Test Medic reached level 4."));
	CHECK(test::outputHas(output, "Test Medic learned Renew."));
	CHECK(s.world().enemies.empty());
	CHECK_EQ(s.world().graceMs, test::data().config.graceMs);
}

// game.json bag_slots (R174): shops refuse what the bag cannot hold, whole purchases only; a chest that does not fit stays
// shut and opens later; under bag_stacking single every item is a slot; equipment takes none.
TEST(theBagHasSlotsWhenTheDataGivesThem) {
	GameData data = test::data();
	data.config.bagSlots = 2;
	Session s(data, SessionOptions());
	openStall(s);
	s.world().money = 10000;
	s.world().items = {{"tonic", 1}};
	CHECK_EQ(s.bagSlotsUsed(), 1);
	CHECK(s.buy("clear_gel", 1).empty());
	CHECK_EQ(s.bagRoom("spark_flask"), 0);
	CHECK_EQ(s.buy("spark_flask", 1).key, std::string("game.refuse.bag_full"));
	CHECK(s.buy("tonic", 5).empty());
	CHECK(s.buy("grip_bands", 1).empty());
	CHECK(s.closeMenu().empty());
	test::placeParty(s, "tidewater", "crate_front", Facing::South);
	s.world().items = {{"clear_gel", 1}, {"spark_flask", 1}};
	CHECK(s.interact().empty());
	CHECK(test::outputHas(s.drainOutput(), "the chest stays shut"));
	CHECK(!s.world().flag("chest.tidewater_crate"));
	s.world().items.erase("spark_flask");
	CHECK(s.interact().empty());
	CHECK(s.world().flag("chest.tidewater_crate") && s.world().items.at("tonic") == 2);

	data.config.bagSingle = true;
	Session single(data, SessionOptions());
	openStall(single);
	single.world().money = 10000;
	single.world().items = {{"tonic", 1}};
	CHECK_EQ(single.bagRoom("tonic"), 1);
	CHECK_EQ(single.buy("tonic", 2).key, std::string("game.refuse.bag_full"));
	CHECK(single.buy("tonic", 1).empty());
	CHECK_EQ(single.bagSlotsUsed(), 2);
}

// game.json autosave false (R175): no autosave is written at any point, and the session says so.
TEST(autosavesCanBeTurnedOff) {
	GameData data = test::data();
	data.config.autosave = false;
	SessionOptions options;
	options.saveDirectory = test::freshDirectory("no_autosave");
	Session s(data, options);
	CHECK(!s.autosaveOn());
	CHECK(s.newGame().empty());
	test::confirmLines(s);
	CHECK(s.devWarp("reed_path", "west_gate").empty());
	for (const Output& o : s.drainOutput()) {
		CHECK(o.kind != OutputKind::Autosave);
	}
	CHECK_EQ(s.autosaveCount(), 0);
	data.config.autosave = true;
	data.config.autosaveSlots = 0;
	Session none(data, SessionOptions());
	CHECK(!none.autosaveOn());
}
