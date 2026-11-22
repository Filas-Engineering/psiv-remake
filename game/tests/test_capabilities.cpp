#include "test.h"

#include "game/capabilities.h"
#include "game/check.h"
#include "game/save.h"

#include "events/language.h"
#include "rules/data_keys.h"
#include "rules/json.h"
#include "rules/names.h"
#include "rules/types.h"

#include <filesystem>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace game;

namespace {

std::set<std::pair<std::string, std::string>> namedInSource(const std::string& path, const std::string& function) {
	std::string text;
	CHECK(readFile(path, text));
	std::set<std::pair<std::string, std::string>> named;
	const std::regex call(function + "\\(\"([^\"]*)\", \"([^\"]*)\"\\)");
	for (auto it = std::sregex_iterator(text.begin(), text.end(), call); it != std::sregex_iterator(); ++it) {
		named.insert({(*it)[1].str(), (*it)[2].str()});
	}
	return named;
}

// The unknown-field problems of the data after every key of `set` the file does not hold yet, and one key no set
// lists, are written into the object that starts at the first '{' after `anchor` in `file`.
std::set<std::string> unknownAfterAddingEveryKey(const std::string& name, const std::string& file, const std::string& anchor, const rules::DataKeySet& set) {
	namespace fs = std::filesystem;
	const std::string root = test::freshDirectory(name);
	std::error_code error;
	fs::create_directories(root + "/game", error);
	fs::create_directories(root + "/core", error);
	fs::copy("data", root + "/game/data", fs::copy_options::recursive, error);
	fs::copy("../core/data", root + "/core/data", fs::copy_options::recursive, error);
	CHECK(!error);
	std::string text;
	CHECK(readFile(root + "/" + file, text));
	const std::size_t at = text.find('{', text.find(anchor));
	CHECK(at != std::string::npos);
	std::string added;
	for (const std::string& key : set.keys) {
		if (text.find("\"" + key + "\":") == std::string::npos) {
			added += "\"" + key + "\": null, ";
		}
	}
	added += "\"zz_unreported\": null, ";
	text.insert(at + 1, added);
	std::string problem;
	CHECK(writeFileAtomic(root + "/" + file, text, problem));
	const CheckReport report = checkData(DataDirectories{root + "/game/data", root + "/core/data"});
	std::set<std::string> unknown;
	for (const rules::Problem& p : report.problems) {
		if ((p.message.key == "data.unknown_field" || p.message.key == "load.unknown_field") && !p.message.params.empty()) {
			unknown.insert(p.message.params[0]);
		}
	}
	return unknown;
}

}

TEST(theLoadersCheckKeysOnlyAgainstTheReportedSets) {
	std::set<std::pair<std::string, std::string>> core = namedInSource("../core/src/loader.cpp", "dataKeys");
	std::set<std::pair<std::string, std::string>> game = namedInSource("src/data.cpp", "gameKeys");
	for (const auto& named : namedInSource("src/area.cpp", "gameKeys")) {
		game.insert(named);
	}
	for (int k = 0; k <= static_cast<int>(rules::EffectKind::GrantAffinity); ++k) {
		core.insert({"shared", std::string("effects[] ") + rules::toString(static_cast<rules::EffectKind>(k))});
	}
	std::set<std::pair<std::string, std::string>> coreTable;
	for (const rules::DataKeySet& set : rules::coreDataKeys()) {
		coreTable.insert({set.file, set.object});
	}
	std::set<std::pair<std::string, std::string>> gameTable;
	for (const rules::DataKeySet& set : gameDataKeys()) {
		gameTable.insert({set.file, set.object});
	}
	// Every set a loader names is reported, and every reported set is one a loader checks.
	CHECK(core == coreTable);
	CHECK(game == gameTable);
	CHECK_EQ(coreTable.size(), rules::coreDataKeys().size());
	CHECK_EQ(gameTable.size(), gameDataKeys().size());
}

TEST(everyReportedKeyLoadsAndAnUnreportedOneIsRejected) {
	const std::set<std::string> onlyTheStranger = {"zz_unreported"};
	CHECK(unknownAfterAddingEveryKey("caps_game", "game/data/game.json", "{", gameKeys("game.json", "")) == onlyTheStranger);
	CHECK(unknownAfterAddingEveryKey("caps_presentation", "game/data/game.json", "\"presentation\": {", gameKeys("game.json", "presentation")) == onlyTheStranger);
	CHECK(unknownAfterAddingEveryKey("caps_area", "game/data/areas/bell_cove.json", "{", gameKeys("area", "")) == onlyTheStranger);
	CHECK(unknownAfterAddingEveryKey("caps_exit", "game/data/areas/bell_cove.json", "\"exits\": [", gameKeys("area", "exits[]")) == onlyTheStranger);
	CHECK(unknownAfterAddingEveryKey("caps_rules", "core/data/rules.json", "{", rules::dataKeys("rules.json", "")) == onlyTheStranger);
	CHECK(unknownAfterAddingEveryKey("caps_ability", "core/data/abilities.json", "\"abilities\": [", rules::dataKeys("abilities.json", "abilities[]")) == onlyTheStranger);
}

TEST(theCapabilitiesReportIsJsonWithEveryTable) {
	const std::string text = capabilitiesJson();
	const rules::Result<rules::JsonValue> parsed = rules::parseJson(text);
	CHECK(parsed.ok());
	if (!parsed.ok()) {
		return;
	}
	const rules::JsonValue& root = *parsed.value;
	CHECK(root.keys.size() == 6 && root.keys[0] == "capabilities_schema" && root.keys[1] == "versions");
	const rules::JsonValue* versions = root.find("versions");
	CHECK(versions && versions->find("save_schema") && versions->find("save_schema")->number == kSaveSchemaVersion);
	CHECK(versions && versions->find("abi") && versions->find("abi")->number == kAbiVersion);
	const rules::JsonValue* data = root.find("data");
	CHECK(data && data->items.size() == rules::coreDataKeys().size() + gameDataKeys().size());
	const rules::JsonValue* script = root.find("script");
	const rules::JsonValue* commands = script ? script->find("commands") : nullptr;
	CHECK(commands && commands->items.size() == events::script_commands().size());
	CHECK(text.find("\"picture show <picture>\"") != std::string::npos);
	CHECK(text.find("\"area_kind\": [\"town\", \"field\", \"dungeon\", \"vessel\"]") != std::string::npos);
	// Stable: the same text every time.
	CHECK(capabilitiesJson() == text);
}
