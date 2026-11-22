#include "test.h"

#include "game/save.h"

#include "rules/progression.h"

#include <algorithm>
#include <filesystem>

using namespace game;

namespace {

bool hasKey(const std::vector<Output>& output, const std::string& key) {
	return std::any_of(output.begin(), output.end(), [&](const Output& o) { return o.message.key == key; });
}

}

TEST(devCommandsRefuseAtTheTitleAndInTheWrongMode) {
	Session s(test::data(), SessionOptions());
	CHECK_EQ(s.devWarp("reed_path", "west_gate").key, std::string("game.refuse.no_game"));
	CHECK_EQ(s.devSetFlag("tidewater.met_guide", true).key, std::string("game.refuse.no_game"));
	CHECK_EQ(s.devEncounters(false).key, std::string("game.refuse.no_game"));
	std::string text;
	CHECK_EQ(s.saveText(text).key, std::string("game.refuse.no_game"));
	CHECK(!s.devMarked());
	s.newGame();
	CHECK(s.mode() == Mode::Event);
	CHECK_EQ(s.devWarp("reed_path", "west_gate").key, std::string("game.refuse.dev_field_only"));
	test::confirmLines(s);
	CHECK_EQ(s.devWarp("nowhere", "gate").key, std::string("game.refuse.dev_unknown_area"));
	CHECK_EQ(s.devWarp("reed_path", "nowhere").key, std::string("game.refuse.dev_unknown_marker"));
	CHECK_EQ(s.devWarpCell("reed_path", {0, 0}).key, std::string("game.refuse.dev_bad_cell"));
	CHECK_EQ(s.devSetFlag("not.a.flag", true).key, std::string("game.refuse.dev_unknown_flag"));
	CHECK_EQ(s.devGive("tonic", 0).key, std::string("game.refuse.dev_bad_count"));
	CHECK_EQ(s.devGive("moonstone", 1).key, std::string("game.refuse.dev_unknown_thing"));
	CHECK_EQ(s.devSetLevel("medic", 5).key, std::string("game.refuse.dev_unknown_member"));
	CHECK_EQ(s.devSetLevel("fighter", 100).key, std::string("game.refuse.dev_bad_level"));
	CHECK_EQ(s.devBattle("nothing").key, std::string("game.refuse.dev_unknown_encounter"));
	CHECK_EQ(s.devEndBattle(true).key, std::string("game.refuse.no_battle"));
	CHECK_EQ(s.devVehicle("reed_skimmer", "tidewater", "start").key, std::string("game.refuse.dev_vehicle_outdoors"));
	CHECK_EQ(s.devVehicle("raft", "reed_path", "west_gate").key, std::string("game.refuse.dev_unknown_vehicle"));
	CHECK_EQ(s.devRunEvent("no_such_event").key, std::string("game.refuse.dev_unknown_event"));
	// A refusal changes nothing, so the game is still unmarked.
	CHECK(!s.devMarked());
	for (const LanguageTables& l : test::data().languages) {
		CHECK(l.text.count("game.refuse.dev_bad_level"));
	}
}

TEST(theFirstDevCommandMarksTheGameOnce) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	s.drainOutput();
	CHECK(s.devWarp("reed_path", "west_gate").empty());
	CHECK_EQ(s.world().area, std::string("reed_path"));
	CHECK(s.world().position == (Cell{1, 4}));
	CHECK(s.devMarked());
	std::vector<Output> out = s.drainOutput();
	CHECK(hasKey(out, "game.dev_marked"));
	CHECK(hasKey(out, "game.arrive"));
	CHECK(s.devWarpCell("reed_path", {3, 4}).empty());
	CHECK(s.world().position == (Cell{3, 4}));
	CHECK(!hasKey(s.drainOutput(), "game.dev_marked"));

	CHECK(s.devGive("tonic", 5).empty());
	CHECK_EQ(s.world().items.at("tonic"), 6);
	CHECK(s.devGive("tonic", -10).empty());
	CHECK(!s.world().items.count("tonic"));
	CHECK(s.devMoney(-1000).empty());
	CHECK_EQ(s.world().money, 0);
	CHECK(s.devMoney(test::data().config.moneyCap + 5).empty());
	CHECK_EQ(s.world().money, test::data().config.moneyCap);
	s.world().member("fighter")->hp = 0;
	CHECK(s.devSetLevel("fighter", 12).empty());
	const rules::PartyMember* fighter = s.world().member("fighter");
	CHECK_EQ(fighter->level, 12);
	CHECK_EQ(fighter->experience, 0);
	CHECK_EQ(fighter->hp, rules::memberStats(test::data().db, *fighter).maxHp);
	CHECK(s.devVehicle("reed_skimmer", "reed_path", "skimmer_front").empty());
	CHECK(s.world().vehicles.count("reed_skimmer"));
	CHECK(s.devSetFlag("tidewater.met_guide", true).empty());
	CHECK(s.world().flag("tidewater.met_guide"));
	CHECK(s.devSetFlag("tidewater.met_guide", false).empty());
	CHECK(!s.world().flag("tidewater.met_guide"));
}

TEST(theMarkIsSavedAndShownInTheSlotList) {
	SessionOptions options;
	options.saveDirectory = test::freshDirectory("dev_mark");
	Session s(test::data(), options);
	s.newGame();
	test::confirmLines(s);
	CHECK(s.openMenu().empty());
	CHECK(s.saveManual("manual_0").empty());
	CHECK(s.closeMenu().empty());
	CHECK(s.devMoney(10).empty());
	CHECK(s.openMenu().empty());
	CHECK(s.saveManual("manual_1").empty());
	std::vector<SaveSummary> slots = s.store()->list();
	CHECK(slots.size() >= 2u && slots[0].slot == "manual_0" && slots[1].slot == "manual_1");
	CHECK(slots.size() >= 2u && !slots[0].dev && slots[1].dev);
	std::string text;
	CHECK(readFile(s.store()->path("manual_1"), text));
	CHECK(text.find("chunk dev 1 9\nmarked 1\n") != std::string::npos);
	CHECK(text.find("\ndev 1\n") != std::string::npos);
	s.quitToTitle();
	CHECK(s.load("manual_1").empty());
	CHECK(s.devMarked());
	s.quitToTitle();
	CHECK(s.load("manual_0").empty());
	CHECK(!s.devMarked());
}

TEST(aMarkedGameWritesNoClearRecordAndStopsPacing) {
	SessionOptions options;
	options.saveDirectory = test::freshDirectory("dev_clear");
	Session s(test::data(), options);
	test::placeParty(s, "bell_cove", "west_gate", Facing::South);
	const int battles = s.pacing().totalBattles();
	CHECK(s.devBattle("training").empty());
	CHECK(s.mode() == Mode::Battle);
	CHECK(s.battleSource() == BattleSource::Random);
	std::vector<rules::Event> events;
	CHECK(s.devEndBattle(true, &events).empty());
	CHECK(!events.empty() && events.back().type == rules::EventType::BattleEnded);
	CHECK(s.endedBattle() && s.endedBattle()->outcome == rules::Outcome::Victory);
	CHECK(s.mode() == Mode::Field);
	CHECK_EQ(s.pacing().totalBattles(), battles);
	s.drainOutput();
	CHECK(s.devRunEvent("cove_quiet").empty());
	test::confirmLines(s);
	CHECK(s.world().flag(test::data().config.clearFlag));
	CHECK(hasKey(s.drainOutput(), "game.clear_not_recorded"));
	CHECK(!std::filesystem::exists(s.store()->clearRecordPath()));
}

TEST(devEndBattleLosesAndEncountersCanBeTurnedOff) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
	CHECK(s.devBattle("training").empty());
	CHECK(s.devEndBattle(false).empty());
	CHECK(s.endedBattle() && s.endedBattle()->outcome == rules::Outcome::Defeat);
	CHECK(s.gameOver());
	CHECK(s.mode() == Mode::Title);

	SessionOptions random;
	random.encounterMode = EncounterMode::Random;
	for (bool on : {false, true}) {
		Session r(test::data(), random);
		test::placeParty(r, "reed_path", "west_gate", Facing::East);
		CHECK(r.devEncounters(on).empty());
		CHECK(r.encountersOn() == on);
		bool fought = false;
		for (int i = 0; i < 200 && !fought; ++i) {
			r.walk(i % 2 == 0 ? Facing::East : Facing::West);
			fought = r.mode() == Mode::Battle;
		}
		CHECK(fought == on);
	}
}

TEST(saveTextResumesTheEventAtItsLine) {
	Session s(test::data(), SessionOptions());
	CHECK(!s.eventPosition());
	s.newGame();
	CHECK(s.mode() == Mode::Event);
	const std::optional<EventPosition> at = s.eventPosition();
	CHECK(at && at->event == "tidewater_arrival" && at->file == "events/tidewater.ev" && at->line > 4);
	const std::optional<LineView> line = s.line();
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("chunk midevent 1 ") != std::string::npos);

	Session again(test::data(), SessionOptions());
	CHECK(again.loadSaveText(text).empty());
	CHECK(again.mode() == Mode::Event);
	CHECK(again.line() && line && again.line()->textId == line->textId);
	CHECK(again.eventPosition() && again.eventPosition()->line == at->line);
	CHECK(hasKey(again.drainOutput(), "game.loaded_text"));
	test::confirmLines(again);
	CHECK(again.mode() == Mode::Field);
	CHECK(again.saveText(text).empty());
	CHECK(text.find("chunk midevent") == std::string::npos);
}

TEST(devListsNameTheDataIds) {
	Session s(test::data(), SessionOptions());
	auto has = [&](DevList list, const std::string& id) {
		const std::vector<std::string> ids = s.devList(list);
		return std::find(ids.begin(), ids.end(), id) != ids.end();
	};
	CHECK(has(DevList::Areas, "brine_flats"));
	CHECK(has(DevList::Markers, "reed_path:west_gate"));
	CHECK(has(DevList::Flags, "tidewater.met_guide"));
	CHECK(has(DevList::Items, "tonic"));
	CHECK(has(DevList::Encounters, "training"));
	CHECK(has(DevList::Events, "cove_quiet"));
	CHECK(has(DevList::Vehicles, "reed_skimmer"));
	CHECK(has(DevList::Characters, "medic"));
	DevList parsed = DevList::Areas;
	CHECK(parseDevList("equipment", parsed) && parsed == DevList::Equipment);
	CHECK(!parseDevList("bogus", parsed));
}

// dev rest restores the roster and the vehicles' skill uses as a rest does and changes nothing else; it marks the game.
TEST(devRestRestoresAsARestDoes) {
	Session s(test::data(), SessionOptions());
	CHECK(!s.devRest().empty());
	s.newGame();
	test::confirmLines(s);
	rules::PartyMember& fighter = *s.world().member("fighter");
	fighter.hp = 1;
	fighter.tp = 0;
	fighter.statuses = {{"poison", 0}};
	VehiclePlace skimmer;
	skimmer.area = "brine_flats";
	skimmer.skillUses = {{"hull_vent", 0}};
	s.world().vehicles["reed_skimmer"] = skimmer;
	const World before = s.world();
	CHECK(s.devRest().empty());
	const rules::PartyMember& rested = *s.world().member("fighter");
	const rules::Stats stats = rules::memberStats(s.data().db, rested);
	CHECK(rested.hp == stats.maxHp && rested.tp == stats.maxTp && rested.statuses.empty());
	CHECK(s.world().vehicles.at("reed_skimmer").skillUses.empty());
	CHECK(s.devMarked());
	CHECK(s.world().money == before.money && s.world().position == before.position && s.world().playTimeMs == before.playTimeMs && rested.level == before.member("fighter")->level);
}
