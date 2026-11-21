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

// Standing on an exit leaves the area, so a chest whose only open side is an exit cannot be opened.
TEST(anExitCellIsNoPlaceToOpenAnObjectFrom) {
	const std::string dir = copyData("ledge", {"ledge.json"});
	replaceIn(dir + "/game.json", "\"area\": \"tidewater\",\n\t\t\"marker\": \"start\"", "\"area\": \"ledge\",\n\t\t\"marker\": \"start\"");
	const CheckReport r = checkData(DataDirectories{dir, "../core/data"});
	CHECK_EQ(count(r, "/areas/ledge.json", 16, "check.object_not_reached"), 1);
	CHECK_EQ(count(r, "/areas/ledge.json", 13, "check.exit_not_reached"), 0);
}

// R46: a hook solid only while active that its flags or its actor can switch off may stand aside, so what lies behind
// it counts as reached. One that can never switch off (no flags, no actor) blocks as before.
TEST(aHookThatStandsAsideDoesNotCutTheAreaOff) {
	const std::string dir = copyData("alley", {"alley.json"});
	replaceIn(dir + "/game.json", "\"area\": \"tidewater\",\n\t\t\"marker\": \"start\"", "\"area\": \"alley\",\n\t\t\"marker\": \"start\"");
	const CheckReport r = checkData(DataDirectories{dir, "../core/data"});
	CHECK_EQ(count(r, "/text/en.json", 0, "data.text_not_in_table"), 1);
	CHECK_EQ(r.problems.size(), 1u);

	replaceIn(dir + "/areas/alley.json", "\"block_flag\": \"cogway.swept\", ", "");
	const CheckReport fixed = checkData(DataDirectories{dir, "../core/data"});
	CHECK_EQ(count(fixed, "/areas/alley.json", 9, "check.marker_not_reached"), 1);
	CHECK_EQ(count(fixed, "/areas/alley.json", 11, "check.exit_not_reached"), 1);
	CHECK_EQ(count(fixed, "/areas/alley.json", 15, "check.object_not_reached"), 1);
	CHECK_EQ(fixed.problems.size(), 4u);

	replaceIn(dir + "/areas/alley.json", "\"id\": \"sweeper\"", "\"id\": \"dockhand\"");
	CHECK_EQ(checkData(DataDirectories{dir, "../core/data"}).problems.size(), 1u);
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

TEST(descriptionsAreOptionalCheckedAndLeftOutOfTheContentHash) {
	const std::string dir = copyData("descriptions", {});
	const DataDirectories directories{dir, "../core/data"};
	replaceIn(dir + "/text/en.json", "A bitter herbal draught.", "A very bitter herbal draught.");
	replaceIn(dir + "/text/ja.json", "\t\t\"desc.item.clear_gel\": \"冷たい軟膏。味方一人の毒を消す。\",\n", "");
	replaceIn(dir + "/game.json", "\"cameras\": [", "\"cameras\": [\"spare_camera\", ");
	rules::Result<GameData> edited = loadGameData(directories);
	CHECK(edited.ok());
	if (edited.ok()) {
		CHECK_EQ(edited.value->contentHash, test::data().contentHash);
		Session s(*edited.value, SessionOptions());
		CHECK_EQ(s.description(DescriptionKind::Item, "tonic"), std::string("A very bitter herbal draught. Restores 30 HP to one ally."));
		Profile p = s.profile();
		p.textLanguage = "ja";
		CHECK(s.setProfile(p).empty());
		CHECK_EQ(s.description(DescriptionKind::Item, "clear_gel"), std::string());
		CHECK(!s.description(DescriptionKind::Item, "tonic").empty());
	}

	replaceIn(dir + "/text/en.json", "\"desc.item.tonic\"", "\"desc.item.no_such_item\": \"Nothing.\", \"desc.weapon.copper_blade\": \"Wrong kind.\", \"desc.item.tonic\"");
	const CheckReport r = checkData(directories);
	CHECK_EQ(count(r, "/text/en.json", 0, "data.description_unknown"), 2);
	const rules::Problem* unknown = find(r, "data.description_unknown");
	CHECK(unknown && (unknown->message.params[0] == "desc.item.no_such_item" || unknown->message.params[0] == "desc.weapon.copper_blade"));
	CHECK(!loadGameData(directories).ok());
	// A dialogue edit is content: it still changes the hash.
	replaceIn(dir + "/text/en.json", "\"desc.item.no_such_item\": \"Nothing.\", \"desc.weapon.copper_blade\": \"Wrong kind.\", ", "");
	replaceIn(dir + "/text/en.strings", "language en", "language en\n# A comment changes the file's text.");
	rules::Result<GameData> dialogue = loadGameData(directories);
	CHECK(dialogue.ok() && dialogue.value->contentHash != test::data().contentHash);
}

TEST(camerasMusicAndDurationsAreCheckedAndLeftOutOfTheContentHash) {
	const std::string dir = copyData("presentation", {});
	const DataDirectories directories{dir, "../core/data"};
	// Names alone still load; zooms, targets, tracks and timings change no hash.
	replaceIn(dir + "/game.json", "{\"id\": \"lookout_west\", \"area\": \"reed_path\", \"marker\": \"lookout_west\"}", "\"lookout_west\", {\"id\": \"wide\", \"follow\": \"leader\", \"zoom\": 0.5}");
	replaceIn(dir + "/game.json", "\"zoom\": 1.25", "\"zoom\": 2");
	replaceIn(dir + "/game.json", "{\"action\": \"panel_hide\", \"seconds\": 0.35}", "{\"action\": \"panel_hide\", \"seconds\": 1}");
	replaceIn(dir + "/areas/reed_path.json", "\"music\": \"field_marsh_reeds\"", "\"music\": \"marsh_wind\"");
	rules::Result<GameData> edited = loadGameData(directories);
	CHECK(edited.ok());
	if (edited.ok()) {
		CHECK_EQ(edited.value->contentHash, test::data().contentHash);
		const CameraDef* west = edited.value->config.presentation.camera("lookout_west");
		CHECK(west && west->target == CameraTarget::None && west->zoom == 1);
		const CameraDef* wide = edited.value->config.presentation.camera("wide");
		CHECK(wide && wide->target == CameraTarget::Leader && wide->follow.empty() && wide->zoom == 0.5);
		CHECK(edited.value->config.presentation.durations.at(events::ActionType::panel_hide) == 1);
	}

	replaceIn(dir + "/game.json", "\"marker\": \"lookout_front\"", "\"marker\": \"no_marker\"");
	replaceIn(dir + "/game.json", "\"follow\": \"peddler\"", "\"follow\": \"stranger\"");
	replaceIn(dir + "/game.json", "\"victory\": \"victory_bright_sail\"", "\"victory\": \"unlisted_song\"");
	replaceIn(dir + "/game.json", "\"warden\": \"battle_brass_warden\"", "\"warden\": \"battle_brass_warden\", \"kraken\": \"battle_marsh_ambush\"");
	replaceIn(dir + "/game.json", "\"action\": \"panel_clear\"", "\"action\": \"teleport\"");
	replaceIn(dir + "/game.json", "{\"id\": \"wide\", \"follow\": \"leader\", \"zoom\": 0.5}", "{\"id\": \"wide\", \"follow\": \"leader\", \"area\": \"reed_path\", \"zoom\": 9}");
	replaceIn(dir + "/areas/reed_path.json", "\"music\": \"marsh_wind\"", "\"music\": \"no_such_track\"");
	const CheckReport bad = checkData(directories);
	CHECK(find(bad, "data.unknown_action"));
	CHECK(find(bad, "data.camera_two_targets"));
	CHECK(find(bad, "data.number_out_of_range"));
	// The reader's problems stop the load before the references are checked; fix them and look again.
	replaceIn(dir + "/game.json", "\"action\": \"teleport\"", "\"action\": \"panel_clear\"");
	replaceIn(dir + "/game.json", "{\"id\": \"wide\", \"follow\": \"leader\", \"area\": \"reed_path\", \"zoom\": 9}", "{\"id\": \"wide\", \"follow\": \"leader\"}");
	const CheckReport r = checkData(directories);
	const rules::Problem* marker = find(r, "data.camera_marker_missing");
	CHECK(marker && marker->message.params == (std::vector<std::string>{"reed_path", "no_marker"}));
	CHECK(find(r, "data.camera_follows_unknown"));
	CHECK(find(r, "data.music_unknown_encounter"));
	CHECK_EQ(static_cast<int>(std::count_if(r.problems.begin(), r.problems.end(), [](const rules::Problem& p) { return p.message.key == "data.music_not_audio"; })), 2);
	CHECK_EQ(count(r, "/areas/reed_path.json", 0, "data.music_not_audio"), 1);
}

TEST(anInteractHookMayStandOnAWallTheFloorBesideItReaches) {
	const char* text = R"({"id": "cliff", "map": ["#####", "#...#", "#####"], "markers": {"start": [1, 1]},
		"hooks": [{"id": "sign", "at": [2, 0], "event": "e"}, {"id": "ladder", "at": [0, 0], "event": "e"}]})";
	rules::Result<Area> area = parseArea("cliff.json", text);
	CHECK(area.ok());
	const std::string offMap = R"({"id": "cliff", "map": ["###", "#.#"], "markers": {"start": [1, 1]}, "hooks": [{"id": "sign", "at": [5, 0], "event": "e"}]})";
	rules::Result<Area> outside = parseArea("cliff.json", offMap);
	CHECK(!outside.ok() && outside.problems[0].message.key == "data.not_on_map");
	const std::string stepOnWall = R"({"id": "cliff", "map": ["###", "#.#"], "markers": {"start": [1, 1]}, "hooks": [{"id": "trap", "at": [1, 0], "trigger": "step", "event": "e"}]})";
	rules::Result<Area> step = parseArea("cliff.json", stepOnWall);
	CHECK(!step.ok() && step.problems[0].message.key == "data.not_on_floor");

	// The placeholder lookout stands on the reed path's wall corner and is used from the floor below it.
	const Area* reeds = test::data().area("reed_path");
	CHECK(reeds && reeds->rows[1][1] == '#' && reeds->hookAt({1, 1}, HookTrigger::Interact));
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "reed_path", "lookout_front", Facing::North);
	CHECK(s.interact().empty());
	CHECK(s.mode() == Mode::Event);

	// The ladder in the corner has no floor beside it, which the data check reports.
	const std::string dir = copyData("wall_hook", {});
	replaceIn(dir + "/areas/reed_path.json", "\"at\": [1, 1], \"event\": \"reed_path_lookout\"", "\"at\": [0, 0], \"event\": \"reed_path_lookout\"");
	const CheckReport r = checkData(DataDirectories{dir, "../core/data"});
	const rules::Problem* cut = find(r, "check.object_not_reached");
	CHECK(cut && cut->message.params[0] == "hook 'lookout'");
}

TEST(vehiclesPortsAndArrivalsAreChecked) {
	// The islet of the brine flats is reached only across water, which the skimmer crosses, so nothing there is reported.
	CHECK(checkData(DataDirectories{"data", ""}).ok());

	std::string dir = copyData("vehicles", {});
	replaceIn(dir + "/events/flats.ev", "vehicle give reed_skimmer brine_flats mooring", "vehicle give reed_skimmer saltgrass west_gate");
	replaceIn(dir + "/events/flats.ev", "vehicle place reed_skimmer brine_flats mooring", "vehicle place reed_skimmer brine_flats islet_shore");
	replaceIn(dir + "/game.json", "\"crosses\": [\"floor\", \"water\"]", "\"crosses\": [\"floor\", \"lava\"]");
	CheckReport r = checkData(DataDirectories{dir, "../core/data"});
	CHECK(find(r, "data.unknown_terrain"));

	dir = copyData("vehicles_outdoors", {});
	replaceIn(dir + "/events/flats.ev", "vehicle give reed_skimmer brine_flats mooring", "vehicle give reed_skimmer saltgrass west_gate");
	r = checkData(DataDirectories{dir, "../core/data"});
	const rules::Problem* indoors = find(r, "data.vehicle_not_outdoors");
	CHECK(indoors && indoors->message.params[1] == "saltgrass" && ends(indoors->file, "flats.ev"));

	// Without a vehicle that crosses water, the islet's crate and beacon cannot be reached.
	dir = copyData("vehicles_water", {});
	replaceIn(dir + "/game.json", "\"crosses\": [\"floor\", \"water\"]", "\"crosses\": [\"floor\"]");
	r = checkData(DataDirectories{dir, "../core/data"});
	CHECK(std::any_of(r.problems.begin(), r.problems.end(),
		[](const rules::Problem& p) { return p.message.key == "check.object_not_reached" && p.message.params[0] == "chest 'flats_crate'"; }));

	// A port's marker and a town's arrival marker must exist, and only towns have one.
	dir = copyData("ports", {});
	replaceIn(dir + "/areas/cinder_quay.json", "\"marker\": \"pier_front\", \"flag\"", "\"marker\": \"nowhere\", \"flag\"");
	r = checkData(DataDirectories{dir, "../core/data"});
	CHECK(find(r, "data.port_marker_missing"));
	dir = copyData("arrival", {});
	replaceIn(dir + "/areas/ember_steps.json", "\"region\": \"isles\",", "\"region\": \"isles\",\n\t\"arrival\": \"west_gate\",");
	r = checkData(DataDirectories{dir, "../core/data"});
	CHECK(find(r, "data.arrival_not_town"));
}
