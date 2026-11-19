#include "test.h"

#include "game/session.h"
#include "rules/progression.h"
#include "rules/text.h"

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

// The hold fight in Tidewater: training, enemy first, until the party lasts 6 rounds. The fighter starts one
// experience point short of level 7, so any reward levels it up.
void startHoldBattle(Session& s) {
	test::placeParty(s, "tidewater", "hold_front", Facing::North);
	setParty(s, {"fighter", "mystic", "medic"}, 6);
	s.world().member("fighter")->experience = 15 * 6 * 6 + 10 - 1;
	s.interact();
	test::confirmLines(s);
}

}

TEST(theLineIsStructuredData) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "guide_front", Facing::North);
	s.interact();
	const std::optional<LineView> guide = s.line();
	CHECK(guide.has_value());
	if (guide) {
		CHECK_EQ(guide->speakerId, std::string("guide"));
		CHECK_EQ(guide->speakerName, std::string("Harbor Guide"));
		CHECK_EQ(guide->textId, std::string("tidewater.guide.bell"));
		CHECK(guide->text.find("You came about the bell?") == 0);
		CHECK_EQ(guide->portrait, std::string("guide_smile"));
	}

	Session cove(test::data(), SessionOptions());
	test::placeParty(cove, "bell_cove", "bell_front", Facing::East);
	cove.world().memberNames["mystic"] = "Iri";
	cove.interact();
	const std::optional<LineView> frame = cove.line();
	CHECK(frame && frame->speakerId.empty() && frame->speakerName.empty() && frame->portrait.empty());
	CHECK(frame && frame->textId == "cove.bell.frame");
	cove.confirmLine();
	const std::optional<LineView> warden = cove.line();
	CHECK(warden && warden->speakerId == "mystic" && warden->speakerName == "Iri" && warden->portrait == "mystic_worried");
	CHECK(warden && warden->text == "That is no bell keeper. That is a Bell Warden!");
	Profile profile = cove.profile();
	profile.showShippedNames = true;
	CHECK(cove.setProfile(profile).empty());
	CHECK(cove.line() && cove.line()->text == "That is no bell keeper. That is a BelWrdn!");
	CHECK_EQ(cove.lineText(), std::string("Iri: That is no bell keeper. That is a BelWrdn!"));
	cove.confirmLine();
	CHECK(!cove.line());
}

TEST(askAnswersComeFromTheStringTables) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "guide_front", Facing::North);
	s.interact();
	s.confirmLine();
	const std::optional<ChoiceView> english = s.choiceView();
	CHECK(english && english->yesNo && english->prompt == "Will you look into it?");
	CHECK(english && english->options == (std::vector<std::string>{"Yes", "No"}));
	Profile profile = s.profile();
	profile.textLanguage = "ja";
	CHECK(s.setProfile(profile).empty());
	const std::optional<ChoiceView> japanese = s.choiceView();
	CHECK(japanese && japanese->options == (std::vector<std::string>{"はい", "いいえ"}));
	CHECK(japanese && japanese->prompt == "調べてくれるかい？");
}

TEST(faceTurnsInPlaceAndTheAutoMacroIsChecked) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "guide_front", Facing::North);
	const Cell at = s.world().position;
	CHECK(s.face(Facing::West).empty());
	CHECK(s.world().facing == Facing::West && s.world().position == at);
	s.world().playerControl = false;
	CHECK_EQ(s.face(Facing::East).key, std::string("game.refuse.no_control_move"));
	s.world().playerControl = true;
	CHECK(s.openMenu().empty());
	CHECK_EQ(s.face(Facing::East).key, std::string("game.refuse.walk_field_only"));
	CHECK(s.world().facing == Facing::West);

	const rules::Message unknown = s.setAutoMacro("whirlwind");
	CHECK_EQ(unknown.key, std::string("game.refuse.no_macro"));
	CHECK(unknown.params == std::vector<std::string>{"whirlwind"});
	CHECK(s.world().autoMacro.empty());
	CHECK(s.setAutoMacro("tempest").empty());
	CHECK_EQ(s.world().autoMacro, std::string("tempest"));
	CHECK(s.setAutoMacro("").empty());
	CHECK(s.world().autoMacro.empty());
}

TEST(theEndedBattleStaysReadableUntilDismissed) {
	Session s(test::data(), SessionOptions());
	startHoldBattle(s);
	CHECK(s.mode() == Mode::Battle);
	CHECK(s.endedBattle() == nullptr);
	const AutoBattle played = s.playBattle();
	CHECK(played.ended);
	CHECK(played.refusal.empty());
	CHECK(!played.rounds.empty());
	CHECK(s.mode() != Mode::Battle);
	const EndedBattle* ended = s.endedBattle();
	CHECK(ended != nullptr);
	if (!ended) {
		return;
	}
	CHECK_EQ(ended->encounter, std::string("training"));
	CHECK(ended->source == BattleSource::Event);
	CHECK(ended->outcome == rules::Outcome::Victory || ended->outcome == rules::Outcome::ScriptedEnd);
	CHECK(ended->outcome == ended->battle.outcome());
	CHECK(!ended->gameOver);
	CHECK(ended->rewards.experience > 0);
	CHECK_EQ(ended->battle.combatants().size(), std::size_t(6));
	CHECK(ended->battle.round() >= 1);
	bool fighterLevelled = false;
	for (const LevelUpView& up : ended->levelUps) {
		CHECK_EQ(up.level, s.world().member(up.member)->level);
		fighterLevelled = fighterLevelled || up.member == "fighter";
	}
	CHECK(fighterLevelled);
	s.dismissBattle();
	CHECK(s.endedBattle() == nullptr);
}

TEST(aChosenNameNamesThePartyCombatant) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "hold_front", Facing::North);
	s.world().memberNames["fighter"] = "Bryn";
	s.interact();
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Battle);
	CHECK_EQ(s.battle()->combatant(0).name, std::string("Bryn"));
	s.playBattle();
	CHECK(s.world().member("fighter")->name.empty());
	CHECK(s.endedBattle() && s.endedBattle()->battle.combatant(0).name == "Bryn");
}

TEST(queuedPresentationWaitsForTheHost) {
	std::vector<std::uint64_t> pushed;
	SessionOptions options;
	options.presentation = Presentation::Queued;
	options.onAction = [&](const PresentedAction& a) { pushed.push_back(a.handle); };
	Session s(test::data(), options);
	CHECK(s.newGame().empty());
	CHECK(s.mode() == Mode::Event);
	CHECK(!s.linePending());
	const std::vector<PresentedAction> actions = s.pollActions();
	CHECK_EQ(actions.size(), std::size_t(1));
	if (actions.size() != 1) {
		return;
	}
	CHECK(actions[0].phase == ActionPhase::Begin && actions[0].action.type == events::ActionType::fade_in);
	CHECK_EQ(actions[0].description, std::string("fade in 1"));
	CHECK(pushed == std::vector<std::uint64_t>{actions[0].handle});
	s.update(5000);
	CHECK(!s.linePending());
	s.finishAction(actions[0].handle);
	s.update(16);
	CHECK(s.linePending());
	CHECK(s.pollActions().empty());

	Session instant(test::data(), SessionOptions());
	CHECK(instant.newGame().empty());
	CHECK(instant.linePending());
	const std::vector<PresentedAction> done = instant.pollActions();
	CHECK(done.size() == 1 && done[0].action.type == events::ActionType::fade_in);
}

TEST(aTapShorterThanATickTurnsTheLeader) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "guide_front", Facing::East);
	const int tick = test::data().config.tickMs;
	const Cell at = s.world().position;
	s.update(1, {true, Facing::South});
	s.update(2 * tick, FieldInput());
	CHECK(s.world().facing == Facing::South);
	CHECK(s.world().position == at);
	CHECK(s.face(Facing::East).empty());
	s.update(2 * tick, FieldInput());
	CHECK(s.world().facing == Facing::East);
}

TEST(walkStepFollowsTheShortestPath) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	const Cell dock = test::data().area("tidewater")->markers.at("dock_front");
	PathWalk walk = s.beginWalk(dock);
	WalkStatus status = WalkStatus::Walking;
	int steps = 0;
	while ((status = s.walkStep(walk)) == WalkStatus::Walking) {
		test::confirmLines(s);
		++steps;
	}
	CHECK(status == WalkStatus::Arrived);
	CHECK(s.world().position == dock);
	CHECK_EQ(steps, 8);
	PathWalk wall = s.beginWalk({0, 0});
	CHECK(s.walkStep(wall) == WalkStatus::NoPath);
	PathWalk out = s.beginWalk({13, 4});
	while ((status = s.walkStep(out)) == WalkStatus::Walking) {
		test::confirmLines(s);
	}
	CHECK(status == WalkStatus::LeftArea);
	CHECK_EQ(s.world().area, std::string("reed_path"));
}

TEST(dataDirectoriesCanBeGivenAndProblemsCarryKeys) {
	const rules::Result<GameData> explicitCore = loadGameData(DataDirectories{"data", "../core/data"});
	CHECK(explicitCore.ok());
	CHECK(explicitCore.ok() && explicitCore.value->contentHash == test::data().contentHash);

	const rules::Result<GameData> noCore = loadGameData(DataDirectories{"data", "build/no_such_core"});
	CHECK(!noCore.ok() && noCore.problems.size() == 1);
	if (!noCore.problems.empty()) {
		CHECK_EQ(noCore.problems[0].message.key, std::string("load.cannot_open"));
		CHECK_EQ(noCore.problems[0].file, std::string("build/no_such_core/rules.json"));
	}
	CHECK_EQ(noCore.error, std::string("build/no_such_core/rules.json: cannot open file"));

	const rules::Result<GameData> noGame = loadGameData("build/no_such_game");
	CHECK(!noGame.ok() && noGame.problems.size() == 1);
	if (!noGame.problems.empty()) {
		CHECK_EQ(noGame.problems[0].message.key, std::string("data.cannot_read"));
		CHECK(noGame.problems[0].message.params == std::vector<std::string>{"build/no_such_game/game.json"});
		CHECK_EQ(noGame.error, rules::formatProblems(noGame.problems, problemText()));
	}
	for (const char* key : {"data.cannot_read", "load.cannot_open", "script.unknown_command"}) {
		CHECK(problemText().count(key) == 1);
	}
}
