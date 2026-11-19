#include "test.h"

#include "game/runner.h"
#include "game/save.h"

#include <sstream>

using namespace game;

TEST(scriptedPlaythroughReachesItsFixedEnding) {
	std::string script;
	CHECK(readFile("data/scripts/playthrough.txt", script));
	SessionOptions options;
	options.seed = 3;
	options.saveDirectory = test::freshDirectory("playthrough");
	Session s(test::data(), options);
	std::ostringstream transcript;
	RunnerOptions r;
	r.autoBattle = true;
	Runner runner(s, transcript, r);
	CHECK(!runner.runScript(script));
	const std::string text = transcript.str();
	CHECK_EQ(runner.errors(), 0);
	const std::size_t boss = text.find("Enemies: Brass Warden, Bog Wisp.");
	CHECK(boss != std::string::npos);
	CHECK(text.find("Outcome: victory", boss) != std::string::npos);
	CHECK(text.find("That is a BelWrdn!") != std::string::npos);
	CHECK(text.find("the tide is the only sound") != std::string::npos);
	CHECK(text.find("Quiet nights again") != std::string::npos);
	// The poison from the first reed battle is carried out of it and cured by the campfire rest.
	const std::size_t poisoned = text.find("[Poison 3]");
	const std::size_t campfire = text.find("The warmth draws the ache out of everyone.");
	CHECK(poisoned != std::string::npos && campfire != std::string::npos && poisoned < campfire);
	CHECK(text.find("[Poison", campfire) == std::string::npos);
	CHECK(text.find("Reed Peddler: Psst.") != std::string::npos);
	CHECK(text.find("Auto-battle on with tempest.") != std::string::npos);
	CHECK(text.find("Auto-battle stops: a macro entry cannot be paid.") != std::string::npos);
	CHECK(text.find("Job completed: The dockhand's lost crate.") != std::string::npos);
	CHECK(text.find("Clear data recorded.") != std::string::npos);
	CHECK_EQ(s.pacing().bosses().size(), 1u);
	CHECK_EQ(s.pacing().bosses()[0].averageLevelX100, 325);
	// The hold battle counts once per attempt: two defeats and the win after the second retry.
	CHECK_EQ(s.pacing().totalBattles(), 8);
	const std::size_t lost = text.find("Game over.");
	const std::size_t retried = text.find("title> retry", lost);
	CHECK(lost != std::string::npos && retried != std::string::npos);
	CHECK(text.find("The hold falls quiet.", retried) != std::string::npos);
	CHECK(text.find("Test Fighter will be called Bryn.") != std::string::npos);
	CHECK(text.find("Bryn: Tell the ferry I am staying a while.") != std::string::npos);
	CHECK(text.find("Bryn uses ") != std::string::npos);
	const std::size_t skimmer = text.find("Enemies: Bog Wisp A, Bog Wisp B, Gel Mite.", text.find("Take the skimmer out"));
	CHECK(skimmer != std::string::npos && text.find("Reed Skimmer uses ", skimmer) != std::string::npos);
	CHECK(text.find("menu> save manual_1\nSaved to manual_1.") != std::string::npos);
	CHECK(s.modes().refused().empty());

	// The script ends by loading the manual save made at the inn, before the reeds.
	CHECK(s.mode() == Mode::Field);
	CHECK_EQ(s.world().area, std::string("tidewater"));
	CHECK(s.world().flag("tidewater.met_guide"));
	CHECK(!s.world().flag("cove.warden_beaten"));
	CHECK_EQ(s.world().lineup.size(), 3u);
}

TEST(botFinishesTheRouteInBothModes) {
	std::string route;
	CHECK(readFile("data/routes/main.route", route));
	const BotResult visible = runBot(test::data(), 7, EncounterMode::Visible, route);
	const BotResult random = runBot(test::data(), 7, EncounterMode::Random, route);
	CHECK(visible.finished);
	CHECK(random.finished);
	CHECK_EQ(visible.errors, 0);
	CHECK_EQ(random.errors, 0);
	CHECK(visible.table.find("reed_path") != std::string::npos);
	CHECK(visible.table.find("cove.warden_beaten") != std::string::npos);
	const BotResult again = runBot(test::data(), 7, EncounterMode::Visible, route);
	CHECK_EQ(again.table, visible.table);
}
