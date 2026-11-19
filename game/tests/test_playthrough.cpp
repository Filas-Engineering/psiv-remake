#include "test.h"

#include "game/runner.h"
#include "game/save.h"

#include <sstream>

using namespace game;

TEST(scriptedPlaythroughReachesItsFixedEnding) {
	std::string script;
	CHECK(readFile("data/scripts/playthrough.txt", script));
	SessionOptions options;
	options.seed = 7;
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
	CHECK_EQ(s.pacing().bosses().size(), 1u);
	CHECK_EQ(s.pacing().bosses()[0].averageLevelX100, 325);
	CHECK_EQ(s.pacing().totalBattles(), 5);
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
