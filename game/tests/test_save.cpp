#include "test.h"

#include "game/runner.h"
#include "game/save.h"

#include <cstdio>
#include <filesystem>
#include <sstream>

using namespace game;

namespace {

std::uint64_t fnv(const std::string& text) {
	std::uint64_t hash = 1469598103934665603ULL;
	for (unsigned char c : text) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	return hash;
}

// Replaces the trailer so an edited body still passes its checksum.
std::string resign(const std::string& text) {
	const std::string body = text.substr(0, text.rfind("\nend ") + 1);
	char sum[17];
	std::snprintf(sum, sizeof sum, "%016llx", static_cast<unsigned long long>(fnv(body)));
	return body + "end " + sum + "\n";
}

void playSome(Session& s, const std::string& script) {
	std::ostringstream ignored;
	RunnerOptions r;
	r.autoBattle = true;
	Runner runner(s, ignored, r);
	runner.runScript(script);
	CHECK_EQ(runner.errors(), 0);
}

const char* kToTheMarsh = "new\nwalk guide_front\nface north\ntalk\nchoose 0\nname Bryn\nwalk 13 4\nwalk 10 4\nwalk 14 3\n";

}

TEST(saveRoundTripsMidPlaythrough) {
	Session s(test::data(), SessionOptions());
	playSome(s, kToTheMarsh);
	s.world().autoMacro = "opener";
	s.world().vars["test.counter"] = -42;
	s.world().equipmentBag["grip_bands"] = 2;
	CHECK(!s.world().enemies.empty());

	SaveFile save = s.makeSave(false);
	save.writeTime = 1234;
	const std::string text = serializeSave(save);
	rules::Result<SaveFile> loaded = parseSave(text);
	CHECK(loaded.ok());
	std::string diff;
	CHECK(sameWorld(loaded.value->world, s.world(), &diff));
	CHECK_EQ(diff, std::string(""));
	CHECK_EQ(loaded.value->writeTime, 1234);
	CHECK_EQ(serializeSave(*loaded.value), text);
}

TEST(loadingASaveRestoresTheWorldAndPlayContinuesTheSame) {
	const std::string dir = test::freshDirectory("continue");
	SessionOptions options;
	options.saveDirectory = dir;
	Session a(test::data(), options);
	playSome(a, kToTheMarsh);
	CHECK(a.openMenu().empty());
	CHECK(a.saveManual("manual_3").empty());
	a.closeMenu();
	const World atSave = a.world();
	playSome(a, "walk 18 4\nwalk 2 2\n");

	Session b(test::data(), options);
	CHECK(b.load("manual_3").empty());
	CHECK(sameWorld(b.world(), atSave));
	playSome(b, "walk 18 4\nwalk 2 2\n");
	std::string diff;
	CHECK(sameWorld(a.world(), b.world(), &diff));
	CHECK_EQ(diff, std::string(""));
}

TEST(preBattleAutosaveStoresTheEventAndResumesIntoTheBattle) {
	const std::string dir = test::freshDirectory("midevent");
	SessionOptions options;
	options.saveDirectory = dir;
	Session a(test::data(), options);
	test::placeParty(a, "bell_cove", "bell_front", Facing::East);
	a.interact();
	test::confirmLines(a);
	CHECK(a.mode() == Mode::Battle);
	const World atBattle = a.world();
	const std::string slot = a.store()->latest();
	std::string text;
	CHECK(a.store()->read(slot, text));
	rules::Result<SaveFile> save = parseSave(text);
	CHECK(save.ok());
	CHECK(!save.value->midEvent.empty());
	CHECK(save.value->autosave);
	// The save holds the encounter stream from before the battle seed was drawn.
	World expected = atBattle;
	expected.encounterStream = save.value->world.encounterStream;
	CHECK(sameWorld(save.value->world, expected));
	CHECK(save.value->world.encounterStream.state() != atBattle.encounterStream.state());

	Session b(test::data(), options);
	CHECK(b.load(slot).empty());
	CHECK(b.mode() == Mode::Battle);
	CHECK(b.battleSource() == BattleSource::Event);
	CHECK_EQ(b.battleEncounter(), std::string("warden"));
	CHECK_EQ(b.battle()->setup().seed, a.battle()->setup().seed);
	CHECK(sameWorld(b.world(), atBattle));
	// Setting the battle up again from its own autosave writes no second one, and the presentation the scene
	// had put up comes back with instant calls.
	const std::vector<Output> output = b.drainOutput();
	CHECK(!test::outputHas(output, "Autosaved"));
	CHECK(test::outputHas(output, "music play brass_alarm (instant)"));
	CHECK_EQ(b.store()->latest(), slot);
}

TEST(unknownChunksArePreserved) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	SaveFile save = s.makeSave(false);
	std::string text = serializeSave(save);
	const std::string chunk = "chunk vehicles 3 21\njob harbor_nets open\n\n";
	text.insert(text.rfind("chunk random"), chunk);
	text = resign(text);
	rules::Result<SaveFile> loaded = parseSave(text);
	CHECK(loaded.ok());
	CHECK_EQ(loaded.value->world.unknownChunks.size(), 1u);
	CHECK_EQ(loaded.value->world.unknownChunks[0].type, std::string("vehicles"));
	CHECK_EQ(loaded.value->world.unknownChunks[0].version, 3);
	CHECK_EQ(loaded.value->world.unknownChunks[0].payload, std::string("job harbor_nets open\n"));
	const std::string again = serializeSave(*loaded.value);
	CHECK(again.find(chunk) != std::string::npos);
	rules::Result<SaveFile> twice = parseSave(again);
	CHECK(twice.ok());
	CHECK_EQ(serializeSave(*twice.value), again);
}

TEST(damagedAndNewerSavesAreRefused) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	const std::string text = serializeSave(s.makeSave(false));

	std::string damaged = text;
	damaged[damaged.find("money ") + 6] = '9';
	rules::Result<SaveFile> r = parseSave(damaged);
	CHECK(!r.ok());
	CHECK(r.error.find("damaged") != std::string::npos);

	std::string newer = text;
	newer.replace(0, std::string("PSR-SAVE 5").size(), "PSR-SAVE 6");
	r = parseSave(resign(newer));
	CHECK(!r.ok());
	CHECK(r.error.find("newer build") != std::string::npos);

	std::string newerChunk = text;
	newerChunk.replace(newerChunk.find("chunk party 2"), 13, "chunk party 3");
	r = parseSave(resign(newerChunk));
	CHECK(!r.ok());
	CHECK(r.error.find("newer build") != std::string::npos);

	CHECK(!parseSave(text.substr(0, text.size() / 2)).ok());
}

TEST(autosavesRotateThroughThreeSlots) {
	const std::string dir = test::freshDirectory("rotation");
	SessionOptions options;
	options.saveDirectory = dir;
	Session s(test::data(), options);
	s.newGame();
	test::confirmLines(s);
	CHECK_EQ(s.autosaveCount(), 1);
	s.world().money = 1000;
	test::placeParty(s, "tidewater", "inn_front", Facing::West);
	for (int i = 0; i < 3; ++i) {
		CHECK(s.interact().empty());
		CHECK(s.mode() == Mode::Menu);
		CHECK(s.rest().empty());
		CHECK(s.mode() == Mode::Field);
	}
	CHECK_EQ(s.autosaveCount(), 4);
	std::map<std::string, std::uint64_t> sequence;
	for (const SaveSummary& summary : s.store()->list()) {
		CHECK(summary.autosave);
		CHECK(!summary.damaged);
		sequence[summary.slot] = summary.sequence;
	}
	CHECK_EQ(sequence.size(), 3u);
	CHECK_EQ(sequence["auto_0"], 4u);
	CHECK_EQ(sequence["auto_1"], 2u);
	CHECK_EQ(sequence["auto_2"], 3u);
	CHECK_EQ(s.store()->latest(), std::string("auto_0"));
	CHECK_EQ(s.store()->nextAutosaveSlot(), std::string("auto_1"));
	for (const auto& entry : std::filesystem::directory_iterator(dir)) {
		CHECK(entry.path().extension() == ".sav");
	}
}

TEST(continuePicksTheNewestSaveAndSkipsDamagedOnes) {
	const std::string dir = test::freshDirectory("latest");
	SessionOptions options;
	options.saveDirectory = dir;
	Session s(test::data(), options);
	s.newGame();
	test::confirmLines(s);
	s.openMenu();
	CHECK(s.saveManual("manual_0").empty());
	s.closeMenu();
	CHECK_EQ(s.store()->latest(), std::string("manual_0"));
	std::string text;
	s.store()->read("manual_0", text);
	text[text.size() / 2] ^= 1;
	std::string error;
	CHECK(writeFileAtomic(s.store()->path("manual_0"), text, error));
	CHECK_EQ(s.store()->latest(), std::string("auto_0"));
	s.quitToTitle();
	CHECK(s.continueGame().empty());
	CHECK(s.mode() == Mode::Field);
}

TEST(manualSavesAreFieldMenuOnly) {
	const std::string dir = test::freshDirectory("manual");
	SessionOptions options;
	options.saveDirectory = dir;
	Session s(test::data(), options);
	s.newGame();
	test::confirmLines(s);
	CHECK(!s.saveManual("manual_0").empty());
	test::placeParty(s, "tidewater", "guide_front", Facing::North);
	s.interact();
	CHECK(s.mode() == Mode::Event);
	CHECK(!s.saveManual("manual_0").empty());
	CHECK(!s.openMenu().empty());
}

TEST(schemaOneSavesMigrateToTheCurrentSchema) {
	auto chunk = [](const std::string& type, const std::string& payload) {
		return "chunk " + type + " 1 " + std::to_string(payload.size()) + "\n" + payload + "\n";
	};
	std::string text = "PSR-SAVE 1\n";
	text += chunk("header", "schema 1\nbuild game-0.1\nrules 2\ncontent 0\nwritten 5\nsequence 1\nautosave 0\nplay 1000\narea tidewater\nparty fighter:3\ndifficulty original\n");
	text += chunk("location", "area tidewater\nposition 3 4\nfacing east\ncamera -\nvehicle -\n");
	text += chunk("party", "lineup fighter\nmember fighter 3 0 86 8\nwear weapon copper_blade\n");
	text += chunk("inventory", "money 50\nitem tonic 1\n");
	text += chunk("world", "flag tidewater.arrived 1\nvisited tidewater\ncontrol 1\nnext_enemy 1\ngrace 0\nrandom 0 0\nplay 1000\n");
	text += chunk("battle", "macro duo\nstep fighter attack - enemy - 0\nstep mystic ability spark default - -1\nauto duo\ncombo storm_spark\n");
	text += chunk("random", "stream 7\n");
	text += chunk("difficulty", "difficulty original\nreward 1\n");
	text += chunk("midevent", "state events-save%202%20old\n");
	text = resign(text + "end 0\n");

	rules::Result<SaveFile> save = parseSave(text);
	CHECK(save.ok());
	const World& w = save.value->world;
	CHECK_EQ(w.macros.size(), 1u);
	CHECK_EQ(w.macros[0].name, std::string("duo"));
	CHECK_EQ(w.macros[0].steps.size(), 2u);
	CHECK(w.macros[0].steps[1].kind == rules::CommandKind::Ability);
	CHECK_EQ(w.autoMacro, std::string("duo"));
	CHECK_EQ(w.comboLog.entries().size(), 1u);
	CHECK_EQ(w.comboLog.entries()[0].recipe, std::string("storm_spark"));
	CHECK_EQ(w.comboLog.entries()[0].playTimeMs, 0);
	CHECK(save.value->midEvent.empty());
	CHECK(w.jobs.empty());
	CHECK_EQ(w.encounterStream.state(), 7u);

	const std::string again = serializeSave(*save.value);
	CHECK(again.compare(0, 11, "PSR-SAVE 5\n") == 0);
	CHECK(again.find("chunk battle 2 ") != std::string::npos);
	CHECK(again.find("chunk quests 1 ") != std::string::npos);
	CHECK(again.find("chunk actors 1 ") != std::string::npos);
	CHECK(again.find("chunk talk 1 ") != std::string::npos);
	CHECK(again.find("chunk travel 1 ") != std::string::npos);

	// Loading fills the quests and unlocks the schema 1 save did not have from its flags.
	SessionOptions options;
	options.saveDirectory = test::freshDirectory("migrate");
	Session s(test::data(), options);
	std::string error;
	CHECK(writeFileAtomic(s.store()->path("manual_0"), text, error));
	CHECK(s.load("manual_0").empty());
	CHECK(s.mode() == Mode::Field);
	CHECK(s.world().jobs.at("lost_cache") == JobState::Available);
	CHECK(s.world().unlocked.count("tidewater"));
}

TEST(persistentStatusesAndNamesRoundTrip) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	s.world().member("fighter")->statuses = {{"poison", 2}, {"gear_lock", 0}};
	s.world().memberNames["fighter"] = "Rook Tide";
	const std::string text = serializeSave(s.makeSave(false));
	rules::Result<SaveFile> loaded = parseSave(text);
	CHECK(loaded.ok());
	std::string diff;
	CHECK(sameWorld(loaded.value->world, s.world(), &diff));
	CHECK_EQ(diff, std::string(""));
	CHECK_EQ(serializeSave(*loaded.value), text);
}

// Paths are UTF-8, which a narrow path is not on Windows, where it is taken in the ANSI code page.
TEST(dataAndSavesWorkInDirectoriesWithNonAsciiNames) {
	namespace fs = std::filesystem;
	const std::string root = test::freshDirectory("utf8") + "/caf\xc3\xa9";
	std::error_code failed;
	fs::create_directories(fs::u8path(root + "/core"), failed);
	fs::create_directories(fs::u8path(root + "/game"), failed);
	fs::copy("../core/data", fs::u8path(root + "/core/data"), fs::copy_options::recursive, failed);
	fs::copy("data", fs::u8path(root + "/game/data"), fs::copy_options::recursive, failed);
	CHECK(!failed);
	const rules::Result<GameData> loaded = loadGameData(root + "/game/data");
	CHECK(loaded.ok());
	if (!loaded.ok()) {
		return;
	}
	SessionOptions options;
	options.saveDirectory = root + "/saves";
	Session s(*loaded.value, options);
	test::placeParty(s, "tidewater", "guide_front", Facing::North);
	CHECK(s.openMenu().empty());
	CHECK(s.saveManual("manual_0").empty());
	CHECK(fs::is_regular_file(fs::u8path(root + "/saves/manual_0.sav")));
	Session again(*loaded.value, options);
	CHECK(again.load("manual_0").empty());
	CHECK_EQ(again.world().area, std::string("tidewater"));
}
