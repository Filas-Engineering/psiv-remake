#include "test.h"

#include "game/runner.h"
#include "game/save.h"
#include "rules/progression.h"

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
	newer.replace(0, std::string("PSR-SAVE 12").size(), "PSR-SAVE 13");
	r = parseSave(resign(newer));
	CHECK(!r.ok());
	CHECK(r.error.find("newer build") != std::string::npos);

	std::string newerChunk = text;
	newerChunk.replace(newerChunk.find("chunk party 5"), 13, "chunk party 6");
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

// R98: erasing refuses what load cannot read, deletes damaged saves and autosaves in any mode, and leaves the game alone.
TEST(erasingASaveDeletesItsFileWhereLoadCouldNotReadIt) {
	Session off(test::data(), SessionOptions());
	CHECK_EQ(off.eraseSave("manual_0").key, std::string("game.refuse.slot_unreadable"));

	const std::string dir = test::freshDirectory("erase");
	SessionOptions options;
	options.saveDirectory = dir;
	Session s(test::data(), options);
	s.newGame();
	test::confirmLines(s);
	s.openMenu();
	CHECK(s.saveManual("manual_0").empty());
	CHECK(s.saveManual("manual_1").empty());
	s.closeMenu();
	CHECK(std::filesystem::exists(s.store()->path("auto_0")));
	const rules::Message unknown = s.eraseSave("manual_99");
	CHECK_EQ(unknown.key, std::string("game.refuse.slot_unreadable"));
	CHECK(unknown.params == std::vector<std::string>{"manual_99"});
	CHECK_EQ(s.eraseSave("manual_2").key, std::string("game.refuse.slot_unreadable"));
	CHECK_EQ(s.eraseSave("../profile").key, std::string("game.refuse.slot_unreadable"));

	std::string text;
	s.store()->read("manual_1", text);
	text[text.size() / 2] ^= 1;
	std::string error;
	CHECK(writeFileAtomic(s.store()->path("manual_1"), text, error));
	CHECK(s.store()->list()[1].damaged);
	const Cell at = s.world().position;
	CHECK(s.eraseSave("manual_1").empty());
	CHECK(test::outputHas(s.drainOutput(), "Erased manual_1."));
	CHECK(s.eraseSave("auto_0").empty());
	CHECK(s.mode() == Mode::Field && s.world().position == at);
	CHECK(!std::filesystem::exists(s.store()->path("manual_1")) && !std::filesystem::exists(s.store()->path("auto_0")));
	CHECK_EQ(s.eraseSave("manual_1").key, std::string("game.refuse.slot_unreadable"));

	s.quitToTitle();
	CHECK(s.eraseSave("manual_0").empty());
	CHECK(s.store()->list().empty());
	CHECK_EQ(s.continueGame().key, std::string("game.refuse.no_save"));
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
	CHECK(again.compare(0, 12, "PSR-SAVE 12\n") == 0);
	CHECK(again.find("chunk battle 3 ") != std::string::npos);
	CHECK(again.find("chunk quests 1 ") != std::string::npos);
	CHECK(again.find("chunk actors 2 ") != std::string::npos);
	CHECK(again.find("chunk talk 1 ") != std::string::npos);
	CHECK(again.find("chunk travel 3 ") != std::string::npos);
	CHECK(again.find("chunk dev 1 0\n") != std::string::npos);
	CHECK(!save.value->world.devMarked);

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

namespace {

// Replaces text inside one chunk, keeping its byte count and the trailer right, as a hand edit that knows the format.
std::string editSave(const std::string& text, const std::string& from, const std::string& to) {
	const std::size_t at = text.find(from);
	const std::size_t header = text.rfind("\nchunk ", at) + 1;
	const std::size_t sizeAt = text.rfind(' ', text.find('\n', header)) + 1;
	const std::size_t lineEnd = text.find('\n', header);
	const long long size = std::stoll(text.substr(sizeAt, lineEnd - sizeAt)) + static_cast<long long>(to.size()) - static_cast<long long>(from.size());
	std::string out = text.substr(0, at) + to + text.substr(at + from.size());
	out = out.substr(0, sizeAt) + std::to_string(size) + out.substr(lineEnd);
	return resign(out);
}

}

TEST(damagedSavesAreRefusedBeforeAnythingIndexesWithThem) {
	Session base(test::data(), SessionOptions());
	base.newGame();
	test::confirmLines(base);
	SaveFile good;
	good.world = base.world();
	const std::vector<std::pair<const char*, void (*)(World&)>> damages = {
		{"position outside the area", [](World& w) { w.position = {-1, 9999}; }},
		{"broken cell outside the area", [](World& w) { w.brokenCells[w.area].insert({500, 500}); }},
		{"broken cell in an unknown area", [](World& w) { w.brokenCells["nowhere"].insert({0, 0}); }},
		{"broken cell here outside the area", [](World& w) { w.brokenHere.insert({-3, 0}); }},
		{"unknown member", [](World& w) { w.roster.push_back(w.roster.front()); w.roster.back().characterId = "nobody"; }},
		{"lineup member not in the roster", [](World& w) { w.lineup.push_back("medic"); }},
		{"empty lineup", [](World& w) { w.lineup.clear(); }},
		{"negative hit points", [](World& w) { w.roster.front().hp = -5; }},
		{"level past the cap", [](World& w) { w.roster.front().level = 1000; }},
		{"unknown piece", [](World& w) { w.roster.front().equipment[rules::EquipSlot::Head] = "crown_of_nothing"; }},
		{"unknown item", [](World& w) { w.items["mystery"] = 1; }},
		{"oversized count", [](World& w) { w.items["tonic"] = 1000000; }},
		{"negative count", [](World& w) { w.equipmentBag["copper_blade"] = -2; }},
		{"negative money", [](World& w) { w.money = -1; }},
		{"undeclared flag", [](World& w) { w.flags["made.up"] = true; }},
		{"macro enemy slot out of range", [](World& w) { w.macros.push_back({"bad", {}}); w.macros.back().steps.push_back({}); w.macros.back().steps.back().member = "fighter"; w.macros.back().steps.back().target.kind = rules::MacroTargetKind::Enemy; w.macros.back().steps.back().target.enemySlot = 40; }},
		{"macro step for an unknown vehicle", [](World& w) { w.macros.push_back({"bad", {}}); w.macros.back().steps.push_back({}); w.macros.back().steps.back().member = "hovercart"; }},
		{"vehicle macro step with an item", [](World& w) { w.macros.push_back({"bad", {{"reed_skimmer", rules::CommandKind::Item, "tonic", {}}}}); }},
		{"unknown vehicle", [](World& w) { w.vehicles["hovercart"] = {w.area, w.position, Facing::South, w.area, w.position}; }},
		{"vehicle outside its area", [](World& w) { w.vehicles["reed_skimmer"] = {"brine_flats", {900, 2}, Facing::South, "brine_flats", {1, 1}}; }},
		{"aboard a vehicle not owned", [](World& w) { w.aboard = "reed_skimmer"; }},
		{"unknown follower", [](World& w) { w.follower = "ghost"; }},
		{"actor outside its area", [](World& w) { w.actors["peddler"] = {w.area, {-1, -1}, Facing::South, true}; }},
		{"way out outside its area", [](World& w) { w.dungeonExitArea = w.area; w.dungeonExitCell = {0, -7}; }},
		{"unknown ship port", [](World& w) { w.shipPort = "atlantis"; }},
		{"unknown job", [](World& w) { w.jobs["imaginary"] = JobState::Accepted; }},
		{"negative grace", [](World& w) { w.graceMs = -1; }},
		{"unknown member in a party snapshot", [](World& w) { w.partySnapshots["odd"] = {{"fighter", "stranger"}, {}}; }},
		{"member twice in a party snapshot", [](World& w) { w.partySnapshots["odd"] = {{"fighter"}, {"fighter"}}; }},
	};
	{
		Session s(test::data(), SessionOptions());
		CHECK(s.loadSaveText(serializeSave(good)).empty());
	}
	for (const auto& [what, damage] : damages) {
		SaveFile bad = good;
		damage(bad.world);
		Session s(test::data(), SessionOptions());
		const rules::Message refused = s.loadSaveText(serializeSave(bad));
		const bool refusedAsBad = refused.key == "game.refuse.save_invalid" || refused.key == "game.refuse.save_damaged";
		CHECK_EQ(std::string(what) + (refusedAsBad ? " refused" : " loaded"), std::string(what) + " refused");
		CHECK(s.mode() == Mode::Title);
	}
	const std::string text = serializeSave(good);
	const std::string money = "money " + std::to_string(good.world.money) + "\n";
	for (const std::string& edited : {editSave(text, money, "money 99999999999\n"), editSave(text, money, "money -99999999999\n")}) {
		Session s(test::data(), SessionOptions());
		CHECK_EQ(s.loadSaveText(edited).key, std::string("game.refuse.save_damaged"));
		CHECK(s.mode() == Mode::Title);
	}
}

// R95: a macro with steps for vehicles beside party and piece steps is written to the save as core writes it, loads
// again, mid-battle aboard too, and the editor keeps working on it.
TEST(macrosWithVehicleStepsLoadAgain) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "reed_path", "skimmer_front", Facing::North);
	World& w = s.world();
	w.roster.clear();
	w.lineup.clear();
	w.macros.clear();
	for (const char* id : {"fighter", "mystic", "scout", "medic", "engineer"}) {
		w.roster.push_back(rules::makePartyMember(test::data().db, *test::data().db.character(id), 6));
		w.lineup.push_back(id);
	}
	CHECK(s.devVehicle("reed_skimmer", "reed_path", "skimmer_front").empty());
	auto step = [](const char* member, rules::CommandKind kind, const char* id, rules::MacroTargetKind target = rules::MacroTargetKind::Default, int enemy = -1) {
		rules::MacroStep made;
		made.member = member;
		made.kind = kind;
		made.id = id;
		made.target.kind = target;
		made.target.enemySlot = enemy;
		return made;
	};
	rules::MacroStep fan = step("scout", rules::CommandKind::Item, "squall_fan", rules::MacroTargetKind::Enemy, 1);
	fan.piece = true;
	CHECK(s.openMenu().empty());
	CHECK(s.createMacro("mixed").empty());
	const std::vector<rules::MacroStep> steps = {step("fighter", rules::CommandKind::Attack, ""), step("reed_skimmer", rules::CommandKind::Ability, "hull_vent", rules::MacroTargetKind::Enemy, 0),
		fan, step("mystic", rules::CommandKind::Ability, "spark"), step("medic", rules::CommandKind::Ability, "mend", rules::MacroTargetKind::Self),
		step("engineer", rules::CommandKind::Attack, ""), step("dune_crawler", rules::CommandKind::Escape, "")};
	for (std::size_t i = 0; i < steps.size(); ++i) {
		CHECK(s.setMacroStep("mixed", static_cast<int>(i), steps[i]).empty());
	}
	CHECK(s.setAutoMacro("mixed").empty());
	const std::string text = serializeSave(s.makeSave(false));
	CHECK(text.find("step reed_skimmer ability hull_vent enemy - 0\nstep scout piece squall_fan enemy - 1\n") != std::string::npos);
	CHECK(text.find("step dune_crawler escape - default - -1\n") != std::string::npos);

	Session loaded(test::data(), SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	if (loaded.world().macros.size() != 1) {
		return;
	}
	CHECK_EQ(rules::serializeMacros(loaded.world().macros), rules::serializeMacros(s.world().macros));
	CHECK_EQ(loaded.world().autoMacro, std::string("mixed"));
	CHECK(loaded.openMenu().empty());
	const std::vector<MacroView> views = loaded.macroViews();
	CHECK(views.size() == 1 && views[0].steps.size() == steps.size() && views[0].steps[1].memberName == "Reed Skimmer");
	CHECK(loaded.moveMacroStep("mixed", 1, 0).empty());
	CHECK(loaded.removeMacroStep("mixed", 6).empty());
	CHECK(loaded.renameMacro("mixed", "afloat").empty());
	CHECK(loaded.world().macros[0].steps[0].member == "reed_skimmer" && loaded.world().macros[0].steps.size() == steps.size() - 1);
	Session again(test::data(), SessionOptions());
	CHECK(again.loadSaveText(serializeSave(loaded.makeSave(false))).empty());
	CHECK_EQ(rules::serializeMacros(again.world().macros), rules::serializeMacros(loaded.world().macros));
	CHECK(loaded.deleteMacro("afloat").empty());
	CHECK(loaded.world().macros.empty() && loaded.world().autoMacro.empty());

	CHECK(s.closeMenu().empty());
	w.aboard = "reed_skimmer";
	CHECK(s.devBattle("training").empty());
	CHECK(s.battle() && s.battle()->setup().vehicle == "reed_skimmer");
	std::string midBattle;
	CHECK(s.saveText(midBattle).empty());
	Session aboard(test::data(), SessionOptions());
	CHECK(aboard.loadSaveText(midBattle).empty());
	CHECK_EQ(rules::serializeMacros(aboard.world().macros), rules::serializeMacros(s.world().macros));
	CHECK_EQ(aboard.world().aboard, std::string("reed_skimmer"));
}
