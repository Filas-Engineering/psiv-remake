#include "test.h"

#include "game/check.h"
#include "game/save.h"

#include <algorithm>
#include <filesystem>

using namespace game;

namespace {

namespace fs = std::filesystem;

// A copy of data/ under build/ with the fixture areas of tests/fixtures/check added to game.json's list.
std::string copyData(const std::string& name, const std::vector<std::string>& fixtures) {
	const std::string root = test::freshDirectory("check_" + name);
	const std::string dir = root + "/data";
	fs::copy("data", dir, fs::copy_options::recursive);
	std::string config;
	readFile(dir + "/game.json", config);
	std::string added;
	for (const std::string& f : fixtures) {
		fs::copy_file("tests/fixtures/check/" + f, dir + "/areas/" + f);
		added += ", \"areas/" + f + "\"";
	}
	config.insert(config.find(']', config.find("\"areas\": [")), added);
	std::string error;
	writeFileAtomic(dir + "/game.json", config, error);
	return dir;
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

bool ends(const std::string& text, const std::string& end) {
	return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
}

// The number of problems with this key, in a file whose path ends with `file`, at `line`.
int count(const CheckReport& r, const std::string& file, int line, const std::string& key) {
	return static_cast<int>(std::count_if(r.problems.begin(), r.problems.end(), [&](const rules::Problem& p) {
		return ends(p.file, file) && p.line == line && p.message.key == key;
	}));
}

const rules::Problem* find(const CheckReport& r, const std::string& key) {
	for (const rules::Problem& p : r.problems) {
		if (p.message.key == key) {
			return &p;
		}
	}
	return nullptr;
}

}

TEST(checkPassesTheShippedDataAndCountsIt) {
	const CheckReport r = checkData(DataDirectories{"data", ""});
	CHECK(r.ok());
	CHECK(r.complete);
	CHECK_EQ(r.loaded.size(), 2u + test::data().languages.size());
	const std::string text = formatCheckReport(r);
	CHECK(text.find("areas " + std::to_string(test::data().areas.size()) + ",") != std::string::npos);
	CHECK(text.find("events " + std::to_string(test::data().program.events.size()) + ",") != std::string::npos);
	CHECK(text.find("encounters " + std::to_string(test::data().db.encounters.size()) + ",") != std::string::npos);
	CHECK(text.find("text ja: game text keys ") != std::string::npos);
	CHECK(ends(text, "OK: no problems.\n"));
}

TEST(checkReportsCellsZonesIdsAndReachWithFileAndLine) {
	const std::string dir = copyData("islet", {"islet.json", "rock.json"});
	const CheckReport r = checkData(DataDirectories{dir, "../core/data"});
	CHECK(!r.ok());
	CHECK(r.complete);
	// The loader's own cross-references, now with the area file and line.
	CHECK_EQ(count(r, "/text/en.json", 0, "data.text_not_in_table"), 2);
	CHECK_EQ(count(r, "/areas/islet.json", 14, "data.exit_leads_unknown_area"), 1);
	// What only the check finds.
	CHECK_EQ(count(r, "/areas/islet.json", 12, "check.marker_on_object"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 18, "check.hook_id_taken"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 18, "check.step_hook_on_object"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 21, "check.shop_empty"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 24, "check.chest_id_taken"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 25, "check.cell_shared"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 29, "check.zone_id_taken"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 29, "check.zone_outside_map"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 29, "check.zones_overlap"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 30, "check.zone_backwards"), 1);
	// The chest at [3, 1] walls the dock off from the rest of the islet.
	CHECK_EQ(count(r, "/areas/islet.json", 14, "check.exit_not_reached"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 21, "check.object_not_reached"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 24, "check.object_not_reached"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 0, "check.area_not_reached"), 1);
	CHECK_EQ(count(r, "/areas/rock.json", 0, "check.area_no_entrance"), 1);
	CHECK_EQ(r.problems.size(), 18u);

	const rules::Problem* taken = find(r, "check.chest_id_taken");
	CHECK(taken && taken->message.params == std::vector<std::string>({"tidewater_crate", "tidewater"}));
	const std::string text = formatCheckReport(r);
	CHECK(text.find("Problems (18):\n") == 0);
	CHECK(text.find(dir + "/areas/islet.json:25: chest 'islet_b' and hook 'sign' stand on the same cell [3, 1]\n") != std::string::npos);
	CHECK(text.find(dir + "/areas/islet.json:14: exit leads to area 'nowhere', which no file in game.json's areas defines\n") != std::string::npos);
	CHECK(ends(text, "FAILED: 18 problems.\n"));
}

TEST(checkFollowsTheStartAcrossWalls) {
	const std::string dir = copyData("maze", {"maze.json"});
	replaceIn(dir + "/game.json", "\"area\": \"tidewater\",\n\t\t\"marker\": \"start\"", "\"area\": \"maze\",\n\t\t\"marker\": \"start\"");
	const CheckReport r = checkData(DataDirectories{dir, "../core/data"});
	CHECK_EQ(count(r, "/text/en.json", 0, "data.text_not_in_table"), 1);
	CHECK_EQ(count(r, "/areas/maze.json", 11, "check.marker_not_reached"), 1);
	CHECK_EQ(count(r, "/areas/maze.json", 13, "check.exit_not_reached"), 1);
	CHECK_EQ(count(r, "/areas/maze.json", 16, "check.hook_not_reached"), 1);
	CHECK_EQ(count(r, "/areas/maze.json", 19, "check.object_not_reached"), 1);
	// The chest by the start and the areas behind the exit are fine: an exit counts for the area graph even
	// when it is walled off, and that exit is reported once, above.
	CHECK_EQ(r.problems.size(), 5u);
}

TEST(checkKeepsGoingPastBrokenBattleData) {
	const std::string dir = copyData("core", {"islet.json", "rock.json"});
	const std::string core = dir + "/../core";
	fs::copy("../core/data", core, fs::copy_options::recursive);
	replaceIn(core + "/abilities.json", "\"cost\": 3", "\"cost\": \"three\"");
	const CheckReport r = checkData(DataDirectories{dir, core});
	CHECK(!r.complete);
	const rules::Problem* broken = find(r, "load.field_wrong_type");
	CHECK(broken && broken->file == core + "/abilities.json" && broken->line > 0);
	// The areas were still read and checked, and what the loader did not reach is checked here.
	CHECK_EQ(count(r, "/areas/islet.json", 14, "data.exit_leads_unknown_area"), 1);
	CHECK_EQ(count(r, "/areas/islet.json", 25, "check.cell_shared"), 1);
	CHECK_EQ(count(r, "/text/en.json", 0, "check.area_name_missing"), 2);
	CHECK_EQ(count(r, "/text/ja.json", 0, "check.area_name_missing"), 2);
	const std::string text = formatCheckReport(r);
	CHECK(text.find("Not checked: the scripts") != std::string::npos);
	CHECK(text.find("core data") == std::string::npos);
	CHECK(text.find("game data " + dir + ": areas " + std::to_string(test::data().areas.size() + 2) + ",") != std::string::npos);
}
