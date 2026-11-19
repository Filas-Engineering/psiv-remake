#include "test.h"

#include "events/compiler.h"
#include "game/save.h"
#include "game/session.h"
#include "rules/progression.h"

using namespace game;

namespace {

void setParty(Session& s, const std::vector<std::string>& ids, int level) {
	World& w = s.world();
	w.roster.clear();
	w.lineup.clear();
	for (const std::string& id : ids) {
		w.roster.push_back(rules::makePartyMember(test::data().db, *test::data().db.character(id), level));
		w.lineup.push_back(id);
	}
}

void weaken(Session& s) {
	setParty(s, {"fighter"}, 1);
	s.world().roster[0].hp = 1;
}

void fightToTheEnd(Session& s) {
	test::fightToTheEnd(s);
}

// The real adventure with the boss event replaced by one that has no `lose` section.
GameData withoutLoseBranch() {
	return test::withScript("events/bell_cove.ev", "event cove_bell unskippable\n\tbattle warden\n\twin\n\t\tset cove.warden_beaten\n\tend\nend\n");
}

}

TEST(eventBattleWinReturnsToTheWinBranch) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "bell_cove", "bell_front", Facing::East);
	setParty(s, {"fighter", "mystic", "medic"}, 30);
	CHECK(s.interact().empty());
	CHECK(s.mode() == Mode::Event);
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Battle);
	CHECK(s.battleSource() == BattleSource::Event);
	CHECK_EQ(s.battleEncounter(), std::string("warden"));
	CHECK(!s.battle()->escapeAllowed());
	fightToTheEnd(s);
	CHECK(s.mode() == Mode::Event);
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Field);
	CHECK(s.world().flag("cove.warden_beaten"));
	CHECK(!s.world().flag("cove.warden_lost"));
	CHECK_EQ(s.world().items.at("spark_flask"), 1);
	CHECK(s.world().playerControl);
	CHECK(s.modes().refused().empty());
	CHECK_EQ(s.pacing().bosses().size(), 1u);
	CHECK_EQ(s.pacing().bosses()[0].averageLevelX100, 3000);
}

TEST(eventBattleLossRunsTheLoseBranch) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "bell_cove", "bell_front", Facing::East);
	weaken(s);
	s.interact();
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Battle);
	fightToTheEnd(s);
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Field);
	CHECK(!s.gameOver());
	CHECK(s.world().flag("cove.warden_lost"));
	CHECK(!s.world().flag("cove.warden_beaten"));
	CHECK_EQ(s.world().area, std::string("tidewater"));
	CHECK(s.world().position == test::data().area("tidewater")->markers.at("inn_front"));
	CHECK_EQ(s.world().roster[0].hp, 1);
	CHECK(s.world().playerControl);
}

TEST(eventBattleLossWithoutBranchIsGameOverAndRetryReloads) {
	const GameData d = withoutLoseBranch();
	SessionOptions options;
	options.saveDirectory = test::freshDirectory("retry");
	Session s(d, options);
	test::placeParty(s, "bell_cove", "bell_front", Facing::East);
	weaken(s);
	s.interact();
	CHECK(s.mode() == Mode::Battle);
	CHECK(test::outputHas(s.drainOutput(), "before an event battle"));
	const std::uint64_t firstSeed = s.battle()->setup().seed;
	fightToTheEnd(s);
	CHECK(s.mode() == Mode::Title);
	CHECK(s.gameOver());
	CHECK(s.canRetry());
	CHECK(s.retryBattle().empty());
	CHECK(s.mode() == Mode::Battle);
	CHECK_EQ(s.battleEncounter(), std::string("warden"));
	CHECK(s.battle()->setup().seed != firstSeed);
	CHECK_EQ(s.world().roster.size(), 1u);
	CHECK_EQ(s.world().roster[0].hp, 1);
}

TEST(fieldBattleDefeatIsGameOverWithoutRetry) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
	weaken(s);
	VisibleEnemy e;
	e.id = 99;
	e.zone = "west_reeds";
	e.encounter = "training";
	e.spawn = e.position = {2, 4};
	e.facing = Facing::West;
	s.world().enemies.push_back(e);
	s.walk(Facing::East);
	CHECK(s.mode() == Mode::Battle);
	CHECK(s.battleSource() == BattleSource::Visible);
	fightToTheEnd(s);
	CHECK(s.mode() == Mode::Title);
	CHECK(s.gameOver());
	CHECK(!s.canRetry());
}

TEST(choiceBranchesAndPartyChangesFromEvents) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "guide_front", Facing::North);
	s.interact();
	CHECK(s.mode() == Mode::Event);
	CHECK(s.linePending());
	CHECK(s.lineText().find("Harbor Guide: ") == 0);
	s.confirmLine();
	CHECK(s.choice() != nullptr);
	CHECK(!s.choose(2).empty());
	CHECK(s.choose(1).empty());
	s.confirmLine();
	CHECK(s.mode() == Mode::Field);
	CHECK_EQ(s.world().lineup.size(), 1u);

	s.interact();
	s.confirmLine();
	CHECK(s.choose(0).empty());
	s.confirmLine();
	CHECK(s.mode() == Mode::Field);
	CHECK_EQ(s.world().lineup.size(), 3u);
	CHECK_EQ(s.world().member("mystic")->level, 3);
	CHECK(s.world().flag("tidewater.met_guide"));
	CHECK(test::outputHas(s.drainOutput(), "event end"));
}

TEST(shippedNamesSettingRendersTheGlossary) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "bell_cove", "bell_front", Facing::East);
	s.interact();
	s.confirmLine();
	CHECK(s.lineText().find("Bell Warden") != std::string::npos);
	Profile profile = s.profile();
	profile.showShippedNames = true;
	CHECK(s.setProfile(profile).empty());
	CHECK(s.lineText().find("BelWrdn") != std::string::npos);
}
