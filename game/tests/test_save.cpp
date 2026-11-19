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

const char* kToTheMarsh = "new\nwalk guide_front\nface north\ntalk\nchoose 0\nwalk 13 4\nwalk 10 4\nwalk 14 3\n";

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
	rules::Result<SaveFile> loaded = parseSave(text, s.world().settings);
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
	rules::Result<SaveFile> save = parseSave(text, Settings());
	CHECK(save.ok());
	CHECK(!save.value->midEvent.empty());
	CHECK(save.value->autosave);
	CHECK(sameWorld(save.value->world, atBattle));

	Session b(test::data(), options);
	CHECK(b.load(slot).empty());
	CHECK(b.mode() == Mode::Battle);
	CHECK(b.battleSource() == BattleSource::Event);
	CHECK_EQ(b.battleEncounter(), std::string("warden"));
}

TEST(unknownChunksArePreserved) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	SaveFile save = s.makeSave(false);
	std::string text = serializeSave(save);
	const std::string chunk = "chunk quests 3 21\njob harbor_nets open\n\n";
	text.insert(text.rfind("chunk random"), chunk);
	text = resign(text);
	rules::Result<SaveFile> loaded = parseSave(text, Settings());
	CHECK(loaded.ok());
	CHECK_EQ(loaded.value->world.unknownChunks.size(), 1u);
	CHECK_EQ(loaded.value->world.unknownChunks[0].type, std::string("quests"));
	CHECK_EQ(loaded.value->world.unknownChunks[0].version, 3);
	CHECK_EQ(loaded.value->world.unknownChunks[0].payload, std::string("job harbor_nets open\n"));
	const std::string again = serializeSave(*loaded.value);
	CHECK(again.find(chunk) != std::string::npos);
	rules::Result<SaveFile> twice = parseSave(again, Settings());
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
	rules::Result<SaveFile> r = parseSave(damaged, Settings());
	CHECK(!r.ok());
	CHECK(r.error.find("damaged") != std::string::npos);

	std::string newer = text;
	newer.replace(0, std::string("PSR-SAVE 1").size(), "PSR-SAVE 2");
	r = parseSave(resign(newer), Settings());
	CHECK(!r.ok());
	CHECK(r.error.find("newer build") != std::string::npos);

	std::string newerChunk = text;
	newerChunk.replace(newerChunk.find("chunk party 1"), 13, "chunk party 2");
	r = parseSave(resign(newerChunk), Settings());
	CHECK(!r.ok());
	CHECK(r.error.find("newer build") != std::string::npos);

	CHECK(!parseSave(text.substr(0, text.size() / 2), Settings()).ok());
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
