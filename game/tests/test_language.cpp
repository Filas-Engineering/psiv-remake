#include "test.h"

#include "game/profile.h"
#include "game/save.h"

#include "events/compiler.h"
#include "rules/progression.h"

#include <filesystem>

using namespace game;

namespace {

void addMember(Session& s, const std::string& id, int level) {
	s.world().roster.push_back(rules::makePartyMember(test::data().db, *test::data().db.character(id), level));
	s.world().lineup.push_back(id);
}

// A copy of data/ with one file's text replaced, for load errors.
std::string dataWith(const std::string& name, const std::string& file, const std::string& from, const std::string& to) {
	const std::string dir = test::freshDirectory(name);
	std::filesystem::copy("data", dir, std::filesystem::copy_options::recursive);
	std::string text;
	CHECK(readFile(dir + "/" + file, text));
	const std::size_t at = text.find(from);
	CHECK(at != std::string::npos);
	text.replace(at, from.size(), to);
	std::string error;
	CHECK(writeFileAtomic(dir + "/" + file, text, error));
	// core_data is relative to data/.
	CHECK(readFile(dir + "/game.json", text));
	text.replace(text.find("../../core/data"), 15, "../../../../core/data");
	CHECK(writeFileAtomic(dir + "/game.json", text, error));
	return dir;
}

}

TEST(theProfileLanguagePicksTheTables) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	CHECK_EQ(s.displayName("tidewater"), std::string("Tidewater Landing"));
	CHECK_EQ(s.displayName("guide"), std::string("Harbor Guide"));
	Profile p = s.profile();
	p.textLanguage = "ja";
	CHECK(s.setProfile(p).empty());
	CHECK_EQ(s.displayName("tidewater"), std::string("潮見の船着き場"));
	CHECK_EQ(s.displayName("guide"), std::string("港の案内人"));
	CHECK_EQ(s.questLog().objective, std::string("掲示板のそばにいる港の案内人を探す。"));
	CHECK_EQ(s.render(s.openMenu()), std::string());
	CHECK_EQ(s.render(s.openMenu()), std::string("メニューはフィールドで開きます。"));
	// Core ships English battle text only, so every language shows it.
	CHECK_EQ(s.render({"command.escape_forbidden", {}}), test::data().languages.front().text.at("command.escape_forbidden"));

	p.textLanguage = "xx";
	const rules::Message refused = s.setProfile(p);
	CHECK_EQ(refused.key, std::string("game.refuse.unknown_language"));
	CHECK_EQ(s.profile().textLanguage, std::string("ja"));
}

TEST(refusalsAreTextKeys) {
	Session s(test::data(), SessionOptions());
	const rules::Message walk = s.walk(Facing::East);
	CHECK_EQ(walk.key, std::string("game.refuse.walk_field_only"));
	CHECK_EQ(s.render(walk), std::string("You can only walk in the field."));
	s.newGame();
	test::confirmLines(s);
	CHECK(s.openMenu().empty());
	const rules::Message buy = s.buy("tonic", 1);
	CHECK_EQ(buy.key, std::string("game.refuse.no_shop"));
	const rules::Message slot = s.saveManual("manual_99");
	CHECK_EQ(slot.key, std::string("game.refuse.saving_disabled"));
	for (const std::string& key : sessionTextKeys()) {
		for (const LanguageTables& l : test::data().languages) {
			CHECK(l.text.count(key));
		}
	}
}

TEST(everyLanguageHasEveryKey) {
	rules::Result<GameData> missing = loadGameData(dataWith("lang_missing", "text/ja.json", "\t\t\"game.used\": \"{0}を使った。\",\n", ""));
	CHECK(!missing.ok() && missing.error.find("text/ja.json: key 'game.used' of the 'en' table is missing") != std::string::npos);
	rules::Result<GameData> untranslated = loadGameData(dataWith("lang_strings", "text/ja.strings", "speaker.guide = 港の案内人\n", ""));
	CHECK(!untranslated.ok() && untranslated.error.find("'speaker.guide' is missing from the 'ja' table") != std::string::npos);
}

TEST(battleDropsGoToTheInventoryAndAreShown) {
	GameData d = test::data();
	d.db.enemies.at("gel_mite").drops = {{"loosen_oil", 100, 2}};
	d.db.enemies.at("rust_hound").drops = {};
	Session s(d, SessionOptions());
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
	addMember(s, "mystic", 12);
	s.world().member("fighter")->level = 12;
	s.world().items.erase("loosen_oil");
	VisibleEnemy e;
	e.id = 7;
	e.zone = "west_reeds";
	e.encounter = "training";
	e.spawn = e.position = {2, 4};
	s.world().enemies.push_back(e);
	s.walk(Facing::East);
	CHECK(s.mode() == Mode::Battle);
	for (int i = 0; i < 40 && s.mode() == Mode::Battle; ++i) {
		s.fillFromPolicy();
		s.resolveRound();
	}
	CHECK(s.mode() == Mode::Field);
	CHECK_EQ(s.world().items["loosen_oil"], 4);
	CHECK(test::outputHas(s.drainOutput(), "The enemies dropped 4 Loosen Oil."));
}

TEST(theRegistryRejectsAWarpOrMoveToAnotherAreasMarker) {
	events::CompileOptions options;
	options.registry = &test::data().registry;
	const events::CompileResult wrong = events::compile({{"x.ev", "event tidewater_guide unskippable\n\tmove guide to camp_front\n\twarp tidewater west_gate\nend\n"}}, options);
	CHECK_EQ(wrong.diagnostics.size(), 2u);
	CHECK(events::format_diagnostic(wrong.diagnostics[0]).find("marker 'camp_front' is not on map 'tidewater'") != std::string::npos);
	CHECK(events::format_diagnostic(wrong.diagnostics[1]).find("marker 'west_gate' is not on map 'tidewater'; it is on bell_cove, reed_path") != std::string::npos);
	const events::CompileResult right = events::compile({{"x.ev", "event x unskippable\n\twarp reed_path west_gate\nend\n"}}, options);
	CHECK(right.diagnostics.empty());
}
