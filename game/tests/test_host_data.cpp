#include "test.h"

#include "game/save.h"
#include "rules/progression.h"

#include <algorithm>
#include <filesystem>

using namespace game;

namespace {

namespace fs = std::filesystem;

// Copies of data/ and ../core/data under build/, side by side as game.json's core_data expects.
std::string copyGameAndCore(const std::string& name) {
	const std::string root = test::freshDirectory("host_" + name);
	fs::create_directories(root + "/game");
	fs::create_directories(root + "/core");
	fs::copy("data", root + "/game/data", fs::copy_options::recursive);
	fs::copy("../core/data", root + "/core/data", fs::copy_options::recursive);
	return root;
}

void replaceIn(const std::string& path, const std::string& from, const std::string& to) {
	std::string text;
	CHECK(readFile(path, text));
	const std::size_t at = text.find(from);
	CHECK(at != std::string::npos);
	if (at != std::string::npos) {
		text.replace(at, from.size(), to);
	}
	std::string error;
	CHECK(writeFileAtomic(path, text, error));
}

bool hasProblem(const rules::Result<GameData>& loaded, const std::string& key) {
	return std::any_of(loaded.problems.begin(), loaded.problems.end(), [&](const rules::Problem& p) { return p.message.key == key; });
}

void walkOut(Session& s, const std::string& area) {
	for (const Exit& e : s.area()->exits) {
		if (e.toArea == area) {
			PathWalk walk = s.beginWalk(e.at);
			while (s.walkStep(walk) == WalkStatus::Walking) {
				test::confirmLines(s);
			}
			return;
		}
	}
}

bool stepFor(Session& s, Facing direction) {
	const Cell from = s.world().position;
	for (int i = 0; i < 40 && s.world().position == from && s.mode() == Mode::Field; ++i) {
		s.update(s.data().config.tickMs, {true, direction});
	}
	CHECK(s.world().position != from);
	return s.mode() == Mode::Battle;
}

// One step along the Cogway's row `y` between columns 2 and 9; a battle it starts is won.
bool pace(Session& s, int y) {
	const Cell at = s.world().position;
	CHECK_EQ(at.y, y);
	const Facing direction = at.x >= 9 ? Facing::West : at.x <= 2 ? Facing::East : s.world().facing;
	const bool battle = stepFor(s, direction);
	if (battle) {
		CHECK(s.devEndBattle(true).empty());
		test::confirmLines(s);
		CHECK(s.mode() == Mode::Field);
	}
	return battle;
}

}

// R113: a per_member rest point asks cost times the lineup, refuses below that total and takes it.
TEST(perMemberRestPointsPriceTheWholeLineup) {
	GameData d = test::data();
	RestPoint& inn = d.areas.at("tidewater").rests.at(0);
	inn.perMember = true;
	Session s(d, SessionOptions());
	test::placeParty(s, "tidewater", "inn_front", Facing::West);
	s.world().roster.push_back(rules::makePartyMember(d.db, *d.db.character("mystic"), 3));
	s.world().lineup.push_back("mystic");
	s.drainOutput();
	CHECK(s.interact().empty());
	CHECK(s.inn() != nullptr);
	CHECK_EQ(s.restCost(*s.inn()), 20);
	CHECK(test::outputHas(s.drainOutput(), "costs 20"));
	s.world().money = 15;
	CHECK(!s.rest().empty());
	CHECK_EQ(s.world().money, 15);
	s.world().money = 25;
	CHECK(s.rest().empty());
	CHECK_EQ(s.world().money, 5);
	inn.perMember = false;
	CHECK_EQ(s.restCost(inn), 10);
}

// R113: a keeper's voice is a text key prefix the data check wants at least one line of.
TEST(keeperVoicesNeedALineInTheTables) {
	const std::string root = copyGameAndCore("voice");
	replaceIn(root + "/game/data/areas/tidewater.json", "\"cost\": 10}", "\"cost\": 10, \"voice\": \"voice.tide_innkeeper\"}");
	CHECK(hasProblem(loadGameData(root + "/game/data"), "data.voice_unused"));
	replaceIn(root + "/game/data/text/en.json", "\"ui.inn.cost\"", "\"voice.tide_innkeeper.thanks\": \"Sleep sound.\", \"ui.inn.cost\"");
	replaceIn(root + "/game/data/text/ja.json", "\"ui.inn.cost\"", "\"voice.tide_innkeeper.thanks\": \"ぐっすりと。\", \"ui.inn.cost\"");
	const rules::Result<GameData> loaded = loadGameData(root + "/game/data");
	CHECK(loaded.ok());
	if (loaded.ok()) {
		CHECK_EQ(loaded.value->rest("tidewater_inn")->voice, std::string("voice.tide_innkeeper"));
	}
}

// R114: game.json party_talk_ambient false leaves area entry quiet; asking for party talk still plays.
TEST(partyTalkAmbientOffKeepsAreaEntryQuiet) {
	GameData d = test::data();
	d.config.partyTalkAmbient = false;
	Session s(d, SessionOptions());
	test::placeParty(s, "tidewater", "east_gate", Facing::East);
	World& w = s.world();
	w.flags["tidewater.met_guide"] = true;
	w.flags["reed.scout_joined"] = true;
	for (const std::string id : {"mystic", "medic"}) {
		w.roster.push_back(rules::makePartyMember(d.db, *d.db.character(id), 3));
		w.lineup.push_back(id);
	}
	walkOut(s, "reed_path");
	CHECK_EQ(s.world().area, std::string("reed_path"));
	const std::vector<Output> arrived = s.drainOutput();
	CHECK(std::none_of(arrived.begin(), arrived.end(), [](const Output& o) { return o.kind == OutputKind::Ambient; }));
	CHECK(s.world().ambientHeard.empty());
	CHECK(s.partyTalk().empty());
	CHECK(test::outputHas(s.drainOutput(), "Test Mystic: Every step out here squelches."));
}

// R115: max_steps 0 needs a step_chance, which it rolls only once min_steps steps are taken, with no cap.
TEST(aCounterWithNoMaximumRollsItsChanceOnlyPastMinSteps) {
	const std::string zone = "{\"id\": \"a\", \"kind\": \"field\", \"map\": [\"...\"], \"markers\": {\"m\": [0, 0]}, \"zones\": [{\"id\": \"z\", \"from\": [0, 0], \"to\": [2, 0], "
		"\"table\": [{\"encounter\": \"training\", \"weight\": 1}], \"random\": {";
	const rules::Result<Area> refused = parseArea("a.json", zone + "\"max_steps\": 0}}]}");
	CHECK(!refused.ok() && refused.problems.size() == 1 && refused.problems[0].message.key == "data.max_steps_zero_needs_chance");
	const rules::Result<Area> open = parseArea("a.json", zone + "\"min_steps\": 3, \"max_steps\": 0, \"step_chance\": 40}}]}");
	CHECK(open.ok() && open.value->zones[0].random.maxSteps == 0);

	GameData d = test::data();
	d.config.graceMs = 0;
	RandomRules& r = d.areas.at("cogway").zones.at(0).random;
	r.graceSteps = 0;
	r.safeNearExits = false;
	r.stepChance = 1;
	r.minSteps = 3;
	r.maxSteps = 0;
	Session s(d, SessionOptions());
	s.newGame();
	test::confirmLines(s);
	CHECK(s.devWarpCell("cogway", {6, 4}).empty());
	test::confirmLines(s);
	s.devEncounters(true);
	s.face(Facing::East);
	const int y = s.world().position.y;
	for (int round = 0; round < 2; ++round) {
		CHECK(!pace(s, y));
		CHECK(!pace(s, y));
		CHECK(pace(s, y));
	}
}

// R110: game.json presentation's flash_default and flash_original load, and a bad level is a problem.
TEST(theDataNamesTheDefaultFlashingLevel) {
	CHECK(test::data().config.presentation.flashDefault == FlashLevel::Reduced);
	CHECK(test::data().config.presentation.flashOriginal);
	const std::string root = copyGameAndCore("flash");
	replaceIn(root + "/game/data/game.json", "\"presentation\": {", "\"presentation\": {\"flash_default\": \"original\", \"flash_original\": false, ");
	const rules::Result<GameData> loaded = loadGameData(root + "/game/data");
	CHECK(loaded.ok());
	if (loaded.ok()) {
		CHECK(loaded.value->config.presentation.flashDefault == FlashLevel::Original);
		CHECK(!loaded.value->config.presentation.flashOriginal);
		CHECK_EQ(loaded.value->contentHash, test::data().contentHash);
	}
	replaceIn(root + "/game/data/game.json", "\"flash_default\": \"original\"", "\"flash_default\": \"bright\"");
	CHECK(hasProblem(loadGameData(root + "/game/data"), "data.bad_flash_default"));
}

// R110: the chosen level and the notice seen are profile settings, written only when set; a chosen level sets
// reduced_flash to match.
TEST(theFlashingLevelAndTheNoticeAreProfileSettings) {
	CHECK(serializeProfile(Profile()).find("flashing ") == std::string::npos);
	CHECK(serializeProfile(Profile()).find("flash_notice_seen") == std::string::npos);
	Session s(test::data(), SessionOptions());
	Profile p = s.profile();
	p.reducedFlash = true;
	p.flashing = FlashLevel::Standard;
	p.flashNoticeSeen = true;
	CHECK(s.setProfile(p).empty());
	CHECK(!s.profile().reducedFlash);
	p.flashing = FlashLevel::Reduced;
	p.reducedFlash = false;
	CHECK(s.setProfile(p).empty());
	CHECK(s.profile().reducedFlash);
	const std::string text = serializeProfile(s.profile());
	CHECK(text.find("flashing reduced\n") != std::string::npos);
	CHECK(text.find("flash_notice_seen on\n") != std::string::npos);
	const rules::Result<Profile> back = parseProfile(text);
	CHECK(back.ok() && back.value->flashing == FlashLevel::Reduced && back.value->flashNoticeSeen);
	Profile bad;
	CHECK(!setProfileValue(bad, "flashing", "bright").empty());
	CHECK(setProfileValue(bad, "flashing", "original").empty() && bad.flashing == FlashLevel::Original);
}

// R111: a character's profile rows come through member detail in order; text keys are checked against the tables.
TEST(memberProfileRowsComeFromPartyJsonAndTheirKeysAreChecked) {
	const std::string root = copyGameAndCore("profile");
	replaceIn(root + "/core/data/party.json", "\"id\": \"fighter\",",
		"\"id\": \"fighter\", \"profile\": [{\"key\": \"ui.status.profile.level\", \"value\": 17}, {\"key\": \"ui.status.profile.kind\", \"value\": \"ui.title.continue\"}],");
	const rules::Result<GameData> loaded = loadGameData(root + "/game/data");
	CHECK(loaded.ok());
	if (loaded.ok()) {
		Session s(*loaded.value, SessionOptions());
		s.newGame();
		test::confirmLines(s);
		const std::optional<MemberDetail> detail = s.memberDetail("fighter");
		CHECK(detail && detail->profile.size() == 2);
		if (detail && detail->profile.size() == 2) {
			CHECK(detail->profile[0].value.number && detail->profile[0].value.text == "17");
			CHECK(!detail->profile[1].value.number && detail->profile[1].value.text == "ui.title.continue");
		}
		CHECK(test::data().db.character("fighter")->profile.empty());
	}
	replaceIn(root + "/core/data/party.json", "\"value\": \"ui.title.continue\"", "\"value\": \"ui.no_such_line\"");
	CHECK(hasProblem(loadGameData(root + "/game/data"), "data.text_not_in_table"));
	replaceIn(root + "/core/data/party.json", "\"value\": \"ui.no_such_line\"", "\"value\": true");
	CHECK(hasProblem(loadGameData(root + "/game/data"), "load.profile_side"));
}
