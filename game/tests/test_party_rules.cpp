#include "test.h"

#include "game/save.h"

#include "rules/progression.h"

#include <algorithm>
#include <filesystem>
#include <utility>

using namespace game;

namespace {

void toField(Session& s) {
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
}

void addMember(Session& s, const std::string& id, bool active) {
	const GameData& d = s.data();
	s.world().roster.push_back(rules::makePartyMember(d.db, *d.db.character(id), 1));
	if (active) {
		s.world().lineup.push_back(id);
	}
}

}

// B19: a vehicle battle starts from the skill uses left from the last one, and keeps what it used.
TEST(vehicleSkillUsesCarryFromBattleToBattle) {
	Session s(test::data(), SessionOptions());
	toField(s);
	CHECK(s.devVehicle("reed_skimmer", "reed_path", "skimmer_front").empty());
	s.world().aboard = "reed_skimmer";
	s.world().vehicles["reed_skimmer"].skillUses = {{"hull_vent", 0}};
	CHECK(s.devBattle("training").empty());
	CHECK(s.battle() && s.battle()->setup().vehicleSkillUses.at("hull_vent") == 0);
	CHECK(s.devEndBattle(true).empty());
	const std::map<std::string, int>& uses = s.world().vehicles.at("reed_skimmer").skillUses;
	CHECK_EQ(uses.at("hull_vent"), 0);
	CHECK(uses.count("hull_patch") == 1);

	const SaveFile save = s.makeSave(false);
	rules::Result<SaveFile> parsed = parseSave(serializeSave(save));
	CHECK(parsed.ok() && parsed.value->world.vehicles.at("reed_skimmer").skillUses == uses);
}

// SAV-032: a member who joined and is out of the party gets the same share as those in it, until the flag is set.
TEST(membersOutsideThePartyShareExperienceUntilTheFlag) {
	GameData data = test::data();
	data.config.outsideExperienceUntil = "cove.warden_beaten";
	Session s(data, SessionOptions());
	toField(s);
	addMember(s, "mystic", false);
	CHECK(s.devBattle("training").empty());
	CHECK(s.devEndBattle(true).empty());
	const int share = s.endedBattle()->rewards.memberExperience.at(0);
	CHECK(share > 0);
	CHECK_EQ(s.world().member("mystic")->experience, share);

	CHECK(s.devSetFlag("cove.warden_beaten", true).empty());
	CHECK(s.devBattle("training").empty());
	CHECK(s.devEndBattle(true).empty());
	CHECK_EQ(s.world().member("mystic")->experience, share);
	CHECK(s.world().member("fighter")->experience > share);
}

// P6: a standing machine member regains hit points with each step; a fallen one does not.
TEST(machineMembersRegainHitPointsAsThePartyWalks) {
	GameData data = test::data();
	data.config.machineStepHeal = 2;
	Session s(data, SessionOptions());
	toField(s);
	addMember(s, "cogwright", true);
	addMember(s, "mystic", true);
	s.world().member("cogwright")->hp = 10;
	s.world().member("mystic")->hp = 10;
	const Cell start = s.world().position;
	for (int i = 0; i < 3; ++i) {
		s.walk(Facing::East);
	}
	const int steps = std::abs(s.world().position.x - start.x) + std::abs(s.world().position.y - start.y);
	CHECK(steps > 0);
	CHECK_EQ(s.world().member("cogwright")->hp, 10 + 2 * steps);
	CHECK_EQ(s.world().member("mystic")->hp, 10);
}

// game.json machine_step_heal_fallen: the step heal reaches a fallen machine member too, which so stands up.
TEST(fallenMachineMembersStandUpWithTheStepHealWhenTheDataSaysSo) {
	GameData data = test::data();
	data.config.machineStepHeal = 2;
	data.config.machineStepHealFallen = MachineFallenHeal::Stand;
	Session s(data, SessionOptions());
	toField(s);
	addMember(s, "cogwright", true);
	s.world().member("cogwright")->hp = 0;
	const Cell start = s.world().position;
	s.walk(Facing::East);
	if (s.world().position != start) {
		CHECK_EQ(s.world().member("cogwright")->hp, 2);
		CHECK(test::outputHas(s.drainOutput(), "starts up again"));
	}
}

// machine_step_heal_fallen `heal`: a fallen machine member banks the step heal and stays down; the bank, saved, is added once
// something stands it up. The first form of the key, a boolean, still reads.
TEST(fallenMachineMembersCanBankTheStepHealWhileDown) {
	GameData data = test::data();
	data.config.machineStepHeal = 2;
	data.config.machineStepHealFallen = MachineFallenHeal::Heal;
	Session s(data, SessionOptions());
	toField(s);
	addMember(s, "cogwright", true);
	s.world().member("cogwright")->hp = 0;
	const Cell start = s.world().position;
	s.walk(Facing::East);
	s.walk(Facing::West);
	CHECK(s.world().position == start);
	CHECK_EQ(s.world().member("cogwright")->hp, 0);
	CHECK(s.world().machineBank.count("cogwright") == 1 && s.world().machineBank["cogwright"] == 4);
	const int banked = s.world().machineBank["cogwright"];
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("machine_bank cogwright " + std::to_string(banked)) != std::string::npos);
	Session loaded(data, SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK_EQ(loaded.world().machineBank["cogwright"], banked);
	s.world().member("cogwright")->hp = 1;
	s.walk(Facing::East);
	CHECK(s.world().machineBank.empty());
	CHECK(s.world().member("cogwright")->hp >= 1 + banked);

	namespace fs = std::filesystem;
	const std::string root = test::freshDirectory("machine_fallen_words");
	std::error_code error;
	fs::create_directories(root + "/game", error);
	fs::create_directories(root + "/core", error);
	fs::copy("data", root + "/game/data", fs::copy_options::recursive, error);
	fs::copy("../core/data", root + "/core/data", fs::copy_options::recursive, error);
	std::string original;
	CHECK(readFile(root + "/game/data/game.json", original));
	const std::pair<const char*, int> cases[] = {{"true", 2}, {"\"stand\"", 2}, {"\"heal\"", 1}, {"false", 0}, {"\"later\"", -1}};
	for (const auto& [value, expected] : cases) {
		std::string text = original;
		text.insert(text.find('{') + 1, "\"machine_step_heal_fallen\": " + std::string(value) + ", ");
		std::string problem;
		CHECK(writeFileAtomic(root + "/game/data/game.json", text, problem));
		const rules::Result<GameData> read = loadGameData(root + "/game/data");
		CHECK_EQ(read.ok(), expected >= 0);
		if (read.ok()) {
			CHECK_EQ(static_cast<int>(read.value->config.machineStepHealFallen), expected);
		}
	}
}

// P8, F14: a script teaches an ability for good and sets a follower; both are saved.
TEST(scriptsTeachAbilitiesAndSetAFollower) {
	const GameData data = test::withScript("events/bell_cove.ev",
		"event cove_bell unskippable\n\tlearn fighter hallow\n\tfollower peddler\nend\n");
	Session s(data, SessionOptions());
	toField(s);
	CHECK(s.devRunEvent("cove_bell").empty());
	test::confirmLines(s);
	const std::vector<std::string> known = rules::knownAbilities(data.db, *s.world().member("fighter"));
	CHECK(std::find(known.begin(), known.end(), "hallow") != known.end());
	CHECK_EQ(s.world().follower, std::string("peddler"));
	const SaveFile save = s.makeSave(false);
	rules::Result<SaveFile> parsed = parseSave(serializeSave(save));
	std::string diff;
	CHECK(parsed.ok() && sameWorld(parsed.value->world, save.world, &diff));
	CHECK_EQ(diff, std::string(""));
	CHECK(parsed.ok() && parsed.value->world.member("fighter")->taught == std::vector<std::string>{"hallow"});
}

// F11: a job never accepted is missed for good once its flag is set; an accepted one is not.
TEST(aJobNotTakenInTimeIsMissedForGood) {
	GameData data = test::data();
	CHECK(!data.journal.jobs.empty());
	data.journal.jobs[0].missedFlag = "cove.warden_beaten";
	Session s(data, SessionOptions());
	toField(s);
	CHECK(s.devSetFlag(data.journal.jobs[0].availableFlag.empty() ? "cove.warden_seen" : data.journal.jobs[0].availableFlag, true).empty());
	CHECK(s.devSetFlag("cove.warden_beaten", true).empty());
	CHECK(s.world().jobs.at(data.journal.jobs[0].id) == JobState::Missed);
	CHECK(s.devSetFlag(data.journal.jobs[0].acceptedFlag, true).empty());
	CHECK(s.world().jobs.at(data.journal.jobs[0].id) == JobState::Missed);
}

// The Ember Steps: a console that hands out one flask per use (F9), a free spring (F5), and a riddle whose wrong answer
// starts a battle the party cannot win, which the story ends after three rounds (F12, B12, B11).
TEST(theEmberStepsExerciseTheConsoleTheSpringAndTheSentinel) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "ember_steps", "west_gate", Facing::East);
	CHECK(s.devEncounters(false).empty());
	for (int i = 0; i < 4; ++i) {
		CHECK(s.walk(Facing::East).empty());
	}
	CHECK(s.face(Facing::North).empty());
	const int before = s.world().items.count("tonic") ? s.world().items.at("tonic") : 0;
	for (int i = 0; i < 2; ++i) {
		CHECK(s.interact().empty());
		test::confirmLines(s);
	}
	CHECK_EQ(s.world().items.at("tonic"), before + 2);

	CHECK(s.walk(Facing::East).empty());
	CHECK(s.face(Facing::South).empty());
	s.world().member("fighter")->hp = 1;
	const int money = s.world().money;
	CHECK(s.interact().empty());
	CHECK(s.rest().empty());
	if (s.mode() != Mode::Field) {
		s.closeMenu();
	}
	CHECK_EQ(s.world().money, money);
	CHECK_EQ(s.world().member("fighter")->hp, rules::memberStats(s.data().db, *s.world().member("fighter")).maxHp);

	CHECK(s.walk(Facing::East).empty());
	CHECK(s.face(Facing::East).empty());
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(s.choose(0).empty());
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Battle);
	test::fightToTheEnd(s);
	const EndedBattle* ended = s.endedBattle();
	CHECK(ended && ended->battle.combatant(ended->battle.partyCount()).alive());
	CHECK(ended && ended->outcome != rules::Outcome::Victory);
	test::confirmLines(s);
	if (s.world().flag("steps.trial_survived")) {
		CHECK(s.world().jobs.at("steps_courier") == JobState::Missed);
	}
}

// A macro's automatic choice policy (R173): set from the field menu, saved, and the caller's auto play policy kept.
TEST(aMacrosPolicyIsSavedAndTheCallersIsKept) {
	Session s(test::data(), SessionOptions());
	CHECK(s.newGame().empty());
	test::confirmLines(s);
	CHECK(s.openMenu().empty());
	CHECK(s.createMacro("careful").empty());
	CHECK(s.setMacroPolicy("careful", rules::MacroPolicy::Expected).empty());
	CHECK(!s.setMacroPolicy("missing", rules::MacroPolicy::Power).empty());
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("macros 4\n") != std::string::npos);
	Session loaded(test::data(), SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	const std::vector<MacroView> views = loaded.macroViews();
	CHECK(std::any_of(views.begin(), views.end(), [](const MacroView& v) { return v.name == "careful" && v.policy == rules::MacroPolicy::Expected; }));
	CHECK(loaded.setAutoPlayMacro("careful", rules::MacroTrouble::Recover, rules::MacroPolicy::Power).empty());
	CHECK(loaded.autoPlayPolicy() == rules::MacroPolicy::Power);
}
