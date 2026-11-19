#include "test.h"

#include "events/compiler.h"
#include "game/save.h"
#include "game/session.h"
#include "rules/progression.h"

using namespace game;

namespace {

// Leaves every non-instant action running, the way an engine's animation would be.
class SlowPresenter : public Presenter {
public:
	bool begin(std::uint64_t, const events::Action& action) override {
		begun.push_back(events::describe(action) + (action.instant ? " instant" : ""));
		return false;
	}
	bool finished(std::uint64_t) override { return false; }
	void cancel(std::uint64_t handle) override { cancelled.push_back(handle); }

	std::vector<std::string> begun;
	std::vector<std::uint64_t> cancelled;
};

void addMember(Session& s, const std::string& id, int level) {
	s.world().roster.push_back(rules::makePartyMember(test::data().db, *test::data().db.character(id), level));
	s.world().lineup.push_back(id);
}

void enemyAhead(Session& s, Cell at) {
	VisibleEnemy e;
	e.id = 50;
	e.zone = "west_reeds";
	e.encounter = "training";
	e.spawn = e.position = at;
	e.facing = Facing::East;
	s.world().enemies.push_back(e);
}

// Test events on the tidewater hooks: the guide opens a save prompt, the dockhand a name entry.
const char* kTidewaterTest = R"(event tidewater_arrival skippable
	set tidewater.arrived
end
event tidewater_guide unskippable
	lock player
	save prompt
	say guide "After the prompt."
	unlock player
end
event tidewater_dockhand unskippable
	name entry fighter
	say fighter "A name of my own."
end
)";

}

TEST(skipFastForwardsLogsSkippedLinesAndRebuildsInstantly) {
	SlowPresenter presenter;
	SessionOptions options;
	options.presenter = &presenter;
	Session s(test::data(), options);
	s.newGame();
	CHECK(s.mode() == Mode::Event);
	CHECK(!s.linePending());
	CHECK(s.canSkip());
	CHECK(s.skip());
	CHECK(s.mode() == Mode::Field);
	CHECK(s.world().flag("tidewater.arrived"));
	CHECK(s.world().playerControl);
	CHECK_EQ(presenter.cancelled.size(), 1u);
	CHECK_EQ(presenter.begun.back(), std::string("fade in 0 instant"));
	CHECK(test::outputHas(s.drainOutput(), "fade in 0 (instant)"));
	const std::vector<std::string> log = s.textLog();
	CHECK_EQ(log.size(), 2u);
	CHECK_EQ(log[1], std::string("(skipped) Dockhand: Mind the ropes. The guide waits by the noticeboard."));

	// Watching the scene to the end leaves the same world (EVT-042).
	Session watched(test::data(), SessionOptions());
	watched.newGame();
	test::confirmLines(watched);
	std::string diff;
	CHECK(sameWorld(watched.world(), s.world(), &diff));
	CHECK_EQ(diff, std::string(""));
}

TEST(scriptOpeningReachesTheBattle) {
	const GameData d = test::withScript("events/reed_path.ev", "event reed_path_scout unskippable\n\tbattle training opening enemy_first\n\twin\n\tend\nend\n");
	Session s(d, SessionOptions());
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
	s.world().position = {9, 4};
	s.walk(Facing::East);
	CHECK(s.mode() == Mode::Battle);
	CHECK(s.battle()->setup().opening == rules::Opening::EnemyFirst);
	CHECK(!s.battle()->sideActsNextRound(rules::Side::Party));
}

TEST(scriptShopOpensFromTheEventAndClosesBackToIt) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "reed_path", "peddler_front", Facing::South);
	s.world().money = 100;
	CHECK(s.interact().empty());
	CHECK(s.lineText().find("Reed Peddler: ") == 0);
	s.confirmLine();
	CHECK(s.mode() == Mode::Shop);
	CHECK(s.shop() && s.shop()->id == "reed_peddler");
	CHECK(!s.canSkip());
	CHECK(s.buy("loosen_oil", 1).empty());
	CHECK(!s.buy("second_wind", 1).empty());
	CHECK(s.closeMenu().empty());
	CHECK(s.mode() == Mode::Event);
	CHECK(s.lineText().find("Mind the wisps") != std::string::npos);
	s.confirmLine();
	CHECK(s.mode() == Mode::Field);
	CHECK_EQ(s.world().items["loosen_oil"], 1);
	CHECK(s.modes().refused().empty());
}

TEST(scriptRestChargesTheRestPointAndCuresCarriedStatuses) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "bell_cove", "camp_front", Facing::North);
	rules::PartyMember& fighter = *s.world().member("fighter");
	fighter.hp = 20;
	fighter.statuses = {{"poison", 3}};
	s.world().money = 3;
	s.interact();
	test::confirmLines(s);
	CHECK(s.choose(0).empty());
	test::confirmLines(s);
	// The campfire offers a save prompt after resting, whether the rest was paid for or not.
	CHECK(s.menuKind() == MenuKind::SavePrompt);
	CHECK(s.closeMenu().empty());
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Field);
	CHECK(test::outputHas(s.drainOutput(), "not enough money"));
	CHECK_EQ(fighter.hp, 20);
	CHECK_EQ(fighter.statuses.size(), 1u);

	s.world().money = 10;
	s.interact();
	test::confirmLines(s);
	s.choose(0);
	test::confirmLines(s);
	s.closeMenu();
	test::confirmLines(s);
	CHECK_EQ(s.world().money, 5);
	CHECK_EQ(fighter.hp, rules::memberStats(test::data().db, fighter).maxHp);
	CHECK(fighter.statuses.empty());
	CHECK(test::outputHas(s.drainOutput(), "after rest"));
}

TEST(savePromptWritesAnOrdinarySaveThatLoadsIntoTheField) {
	const GameData d = test::withScript("events/tidewater.ev", kTidewaterTest);
	SessionOptions options;
	options.saveDirectory = test::freshDirectory("prompt");
	Session s(d, options);
	test::placeParty(s, "tidewater", "guide_front", Facing::North);
	s.interact();
	CHECK(s.mode() == Mode::Menu);
	CHECK(s.menuKind() == MenuKind::SavePrompt);
	CHECK(!s.world().playerControl);
	CHECK(s.saveManual("manual_4").empty());
	CHECK(s.closeMenu().empty());
	CHECK(s.mode() == Mode::Event);
	CHECK(s.lineText().find("After the prompt.") != std::string::npos);
	s.confirmLine();
	CHECK(s.mode() == Mode::Field);

	std::string text;
	CHECK(s.store()->read("manual_4", text));
	rules::Result<SaveFile> save = parseSave(text);
	CHECK(save.ok());
	CHECK(save.value->midEvent.empty());
	CHECK(save.value->world.playerControl);
	s.quitToTitle();
	CHECK(s.load("manual_4").empty());
	CHECK(s.mode() == Mode::Field);
}

TEST(nameEntryNamesTheMemberEverywhere) {
	const GameData d = test::withScript("events/tidewater.ev", kTidewaterTest);
	Session s(d, SessionOptions());
	test::placeParty(s, "tidewater", "dock_front", Facing::North);
	s.interact();
	CHECK(s.menuKind() == MenuKind::NameEntry);
	CHECK_EQ(s.nameEntryMember(), std::string("fighter"));
	CHECK(!s.enterName("   ").empty());
	CHECK(!s.enterName("A name far too long to fit").empty());
	CHECK(s.enterName(" Rook ").empty());
	CHECK(s.mode() == Mode::Event);
	CHECK_EQ(s.lineText(), std::string("Rook: A name of my own."));
	s.confirmLine();
	CHECK_EQ(s.world().memberNames.at("fighter"), std::string("Rook"));
	rules::Result<SaveFile> save = parseSave(serializeSave(s.makeSave(false)));
	CHECK(save.ok() && save.value->world.memberNames.at("fighter") == "Rook");
}

TEST(registryFromTheDataChecksScriptNames) {
	events::CompileOptions o;
	o.registry = &test::data().registry;
	auto compiles = [&](const std::string& body) { return events::compile({{"x.ev", "event x unskippable\n" + body + "end\n"}}, o).ok(); };
	CHECK(compiles("\tgive tonic\n\tgive rivet_plate\n\tset tidewater.arrived\n\tshop reed_peddler\n\trest cove_camp\n\tbattle warden\n\twin\n\tend\n"));
	CHECK(!compiles("\tgive mystery_box\n"));
	CHECK(!compiles("\tset made.up\n"));
	CHECK(!compiles("\tshop nowhere\n"));
	CHECK(!compiles("\tbattle dragons\n\twin\n\tend\n"));
	CHECK(!compiles("\tmusic play unknown_tune\n"));
	CHECK(!compiles("\tparty add stranger\n"));
}

TEST(autoBattleStopsOnItsRules) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
	addMember(s, "mystic", 10);
	addMember(s, "scout", 10);
	enemyAhead(s, {2, 4});
	s.walk(Facing::East);
	CHECK(s.mode() == Mode::Battle);
	CHECK(!s.startAuto().empty());
	s.world().autoMacro = "tempest";
	CHECK(s.startAuto().empty());
	CHECK(s.fillFromAuto());
	CHECK_EQ(s.battle()->command(1).id, std::string("spark"));
	s.resolveRound();
	if (s.mode() == Mode::Battle) {
		s.cancelAuto();
		CHECK(!s.fillFromAuto());
		CHECK(test::outputHas(s.drainOutput(), "Auto-battle cancelled."));
	}

	Session poor(test::data(), SessionOptions());
	test::placeParty(poor, "reed_path", "west_gate", Facing::East);
	addMember(poor, "mystic", 10);
	poor.world().member("mystic")->tp = 0;
	poor.world().autoMacro = "tempest";
	enemyAhead(poor, {2, 4});
	poor.walk(Facing::East);
	CHECK(poor.startAuto().empty());
	CHECK(!poor.fillFromAuto());
	CHECK(!poor.autoActive());
	CHECK(test::outputHas(poor.drainOutput(), "a macro entry cannot be paid"));
}

TEST(scriptedEndReturnsToTheWinBranch) {
	std::string script;
	CHECK(readFile("data/events/bell_cove.ev", script));
	const std::string plain = "\tbattle warden\n";
	CHECK(script.find(plain) != std::string::npos);
	script.replace(script.find(plain), plain.size(), "\tbattle warden until survive 1\n");
	GameData d = test::withScript("events/bell_cove.ev", script);
	Session s(d, SessionOptions());
	test::placeParty(s, "bell_cove", "bell_front", Facing::East);
	s.interact();
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Battle);
	CHECK(s.battle()->setup().scriptedEnd.kind == rules::ScriptedEndKind::SurviveRounds);
	test::autoRound(s);
	CHECK(s.mode() == Mode::Event);
	CHECK(test::outputHas(s.drainOutput(), "The battle is over."));
	test::confirmLines(s);
	CHECK(s.world().flag("cove.warden_beaten"));
}

TEST(fieldTechniquesAndItemsGoThroughCore) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	addMember(s, "medic", 5);
	rules::PartyMember& fighter = *s.world().member("fighter");
	fighter.hp = 10;
	fighter.statuses = {{"poison", 2}};
	CHECK(s.openMenu().empty());
	const int tp = s.world().member("medic")->tp;
	CHECK(s.useAbility("medic", "mend", "fighter").empty());
	CHECK(fighter.hp > 10);
	CHECK_EQ(s.world().member("medic")->tp, tp - 3);
	CHECK(s.useAbility("medic", "purge", "fighter").empty());
	CHECK(fighter.statuses.empty());
	CHECK(test::outputHas(s.drainOutput(), "The poison leaves Test Fighter."));
	CHECK(!s.useAbility("medic", "spark", "fighter").empty());
	CHECK(!s.useAbility("mystic", "mend", "fighter").empty());
	s.world().items["clear_gel"] = 1;
	CHECK_EQ(s.render(s.useItem("clear_gel", "fighter")), std::string("Clear Gel would have no effect."));
	CHECK_EQ(s.world().items["clear_gel"], 1);
}
