#include "test.h"

#include "game/check.h"
#include "game/save.h"

#include <algorithm>
#include <filesystem>

using namespace game;

namespace {

namespace fs = std::filesystem;

// A copy of data/ under build/ with the fixture areas of tests/fixtures/check added to game.json's list.
std::string copyData(const std::string& name, const std::vector<std::string>& fixtures, const std::string& source = "data") {
	const std::string root = test::freshDirectory("check_" + name);
	const std::string dir = root + "/data";
	fs::copy(source, dir, fs::copy_options::recursive);
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

// R99, R100: vehicle and area music load outside the content hash and are checked against the vehicles, areas, flags and
// audio ids.
TEST(vehicleAndAreaMusicAreCheckedAndLeftOutOfTheContentHash) {
	const std::string dir = copyData("music", {});
	const DataDirectories directories{dir, "../core/data"};
	const std::string encounters = "\"encounters\": {\"marsh\": \"battle_marsh_ambush\", \"warden\": \"battle_brass_warden\"}";
	replaceIn(dir + "/game.json", encounters, encounters + ", \"vehicles\": {\"reed_skimmer\": \"battle_brass_warden\", \"dune_crawler\": {\"field\": \"title_harbor_dawn\"}}, \"areas\": {\"cogway\": "
		"[{\"flag\": \"cogway.loud\", \"track\": \"dungeon_firing_floor\"}, \"title_harbor_dawn\"], \"tidewater\": \"defeat_low_tide\"}");
	rules::Result<GameData> edited = loadGameData(directories);
	CHECK(edited.ok());
	if (edited.ok()) {
		const MusicNames& music = edited.value->config.presentation.music;
		CHECK_EQ(edited.value->contentHash, test::data().contentHash);
		CHECK_EQ(music.vehicles.at("reed_skimmer"), std::string("battle_brass_warden"));
		CHECK(!music.vehicles.count("dune_crawler") && music.vehiclesField.at("dune_crawler") == "title_harbor_dawn");
		const std::vector<AreaTrack>& cogway = music.areas.at("cogway");
		CHECK(cogway.size() == 2 && cogway[0].flag == "cogway.loud" && cogway[0].track == "dungeon_firing_floor" && cogway[1].flag.empty());
		CHECK(music.areas.at("tidewater").size() == 1 && music.areas.at("tidewater")[0].track == "defeat_low_tide");
	}

	replaceIn(dir + "/game.json", "\"reed_skimmer\": \"battle_brass_warden\"", "\"raft\": \"battle_brass_warden\", \"reed_skimmer\": \"no_song\"");
	replaceIn(dir + "/game.json", "[{\"flag\": \"cogway.loud\", \"track\": \"dungeon_firing_floor\"}, \"title_harbor_dawn\"]",
		"[\"title_harbor_dawn\", {\"flag\": \"cogway.hushed\", \"track\": \"dungeon_firing_floor\"}]");
	replaceIn(dir + "/game.json", "\"tidewater\": \"defeat_low_tide\"", "\"atlantis\": \"defeat_low_tide\"");
	const CheckReport r = checkData(directories);
	CHECK(find(r, "data.music_unknown_vehicle") && find(r, "data.music_unknown_vehicle")->message.params[0] == "raft");
	CHECK(find(r, "data.music_unknown_area") && find(r, "data.music_unknown_area")->message.params[0] == "atlantis");
	const rules::Problem* flag = find(r, "data.flag_not_declared");
	CHECK(flag && flag->message.params[0] == "cogway.hushed");
	CHECK(find(r, "data.music_after_default"));
	const rules::Problem* track = find(r, "data.music_not_audio");
	CHECK(track && track->message.params[0] == "no_song");

	replaceIn(dir + "/game.json", "\"atlantis\": \"defeat_low_tide\"", "\"tidewater\": [7, {\"track\": \"defeat_low_tide\"}]");
	const CheckReport bad = checkData(directories);
	CHECK(find(bad, "data.music_not_text"));
	const rules::Problem* missing = find(bad, "data.missing_field");
	CHECK(missing && missing->message.params[0] == "flag");
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

TEST(anActorCellApartFromItsTalkPointIsSolidButNotAPoint) {
	const std::string dir = copyData("nook", {"nook.json"}, test::actorDataDirectory());
	replaceIn(dir + "/game.json", "\"area\": \"tidewater\",\n\t\t\"marker\": \"start\"", "\"area\": \"nook\",\n\t\t\"marker\": \"start\"");
	replaceIn(dir + "/text/en.json", "\t\t\"area.reed_path\"", "\t\t\"area.nook\": \"Nook\",\n\t\t\"area.reed_path\"");
	replaceIn(dir + "/text/ja.json", "\t\t\"area.reed_path\"", "\t\t\"area.nook\": \"すみ\",\n\t\t\"area.reed_path\"");
	// The keeper stands in the only gap, so the far side is cut off; its own cell needs no floor beside it, and the hook's
	// cell, the point the party talks to, is used from the row above.
	const CheckReport r = checkData(DataDirectories{dir, "../core/data"});
	CHECK(find(r, "check.marker_not_reached") && find(r, "check.marker_not_reached")->message.params[0] == "far");
	CHECK(!find(r, "check.object_not_reached"));
	CHECK_EQ(r.problems.size(), 1u);

	// At runtime the cell blocks the party while the keeper is shown there.
	rules::Result<GameData> loaded = loadGameData(DataDirectories{dir, "../core/data"});
	CHECK(loaded.ok());
	if (loaded.ok()) {
		Session s(*loaded.value, SessionOptions());
		CHECK(s.newGame().empty());
		test::confirmLines(s);
		s.world().position = {3, 1};
		CHECK_EQ(s.walk(Facing::South).key, std::string("game.refuse.blocked"));
		s.world().actors["tallykeeper"] = ActorPlace{"nook", {3, 2}, Facing::South, false};
		CHECK(s.walk(Facing::South).empty());
		CHECK(s.world().position == (Cell{3, 2}));
	}

	// Moved off the gap, the far side is reached; on a marker, the actor cell is reported like any solid thing.
	replaceIn(dir + "/areas/nook.json", "\"actor_at\": [3, 2]", "\"actor_at\": [1, 3]");
	const CheckReport moved = checkData(DataDirectories{dir, "../core/data"});
	CHECK(find(moved, "check.marker_on_object") && find(moved, "check.marker_on_object")->message.params[1] == "actor 'tallykeeper'");
	CHECK(!find(moved, "check.marker_not_reached"));
}

TEST(actorHomesFiguresAndActorCellsAreChecked) {
	const std::string shapes = copyData("actor_shapes", {}, test::actorDataDirectory());
	replaceIn(shapes + "/game.json", "\"kilnwright\",", "{\"id\": \"kilnwright\", \"sprite\": {\"enemy\": \"gel_mite\", \"look\": \"rag\"}}, {\"id\": \"half\", \"at\": [1, 1]}, 7,");
	const CheckReport bad = checkData(DataDirectories{shapes, "../core/data"});
	CHECK(find(bad, "data.bad_actor_sprite"));
	CHECK(find(bad, "data.actor_home_needs_area"));
	CHECK(find(bad, "data.actor_shape"));

	const std::string dir = copyData("actor_defs", {}, test::actorDataDirectory());
	replaceIn(dir + "/game.json", "\"sprite\": {\"enemy\": \"aegis_sentinel\"}", "\"sprite\": {\"enemy\": \"no_such_enemy\"}");
	replaceIn(dir + "/game.json", "\"kilnwright\",", "{\"id\": \"kilnwright\", \"area\": \"nowhere\", \"at\": \"gate\"},");
	replaceIn(dir + "/game.json", "}, \"tallykeeper\", ", "}, {\"id\": \"tallykeeper\", \"area\": \"wren_hollow\", \"at\": \"west_post\"}, ");
	const CheckReport r = checkData(DataDirectories{dir, "../core/data"});
	CHECK(find(r, "data.actor_sprite_unknown_enemy"));
	CHECK(find(r, "data.actor_home_unknown_area"));
	CHECK(find(r, "data.actor_home_twice"));

	const char* stepWithActor = R"({"id": "a", "map": ["...", "..."], "markers": {"start": [0, 0]}, "hooks": [{"id": "x", "at": [1, 1], "trigger": "step", "event": "e", "actor_at": [0, 1]}]})";
	rules::Result<Area> step = parseArea("a.json", stepWithActor);
	CHECK(!step.ok() && step.problems[0].message.key == "data.actor_at_interact_hooks_only");
	const char* onItself = R"({"id": "a", "map": ["...", "..."], "markers": {"start": [0, 0]}, "hooks": [{"id": "x", "at": [1, 1], "event": "e", "actor_at": [1, 1]}]})";
	rules::Result<Area> same = parseArea("a.json", onItself);
	CHECK(!same.ok() && same.problems[0].message.key == "data.actor_at_on_hook");
	const char* offMap = R"({"id": "a", "map": ["...", "..."], "markers": {"start": [0, 0]}, "hooks": [{"id": "x", "at": [1, 1], "event": "e", "actor_at": [9, 1]}]})";
	rules::Result<Area> off = parseArea("a.json", offMap);
	CHECK(!off.ok() && off.problems[0].message.key == "data.not_on_map");

	const std::string hooks = copyData("actor_at_hook", {});
	replaceIn(hooks + "/areas/reed_path.json", "\"event\": \"reed_path_lookout\"", "\"event\": \"reed_path_lookout\", \"actor_at\": [1, 2]");
	const CheckReport notActor = checkData(DataDirectories{hooks, "../core/data"});
	CHECK(find(notActor, "data.actor_at_not_actor"));
}

TEST(wanderTouchHooksKeepersAndTheBackgroundLimitAreChecked) {
	const CheckReport good = checkData(DataDirectories{test::backgroundDataDirectory(), "../core/data"});
	CHECK(good.problems.empty());

	const std::string dir = copyData("wander", {}, test::backgroundDataDirectory());
	replaceIn(dir + "/game.json", "\"wander\": {\"radius\": 2, \"speed_percent\": 100, \"pause_ms\": [250, 750]}",
		"\"wander\": {\"radius\": 2, \"from\": [1, 1], \"to\": [2, 2], \"pause_ms\": [750, 250], \"terrain\": \"#\"}");
	replaceIn(dir + "/game.json", "\"background_limit\": 2", "\"background_limit\": 40");
	replaceIn(dir + "/game.json", "\"kilnwright\",", "{\"id\": \"kilnwright\", \"wander\": {\"radius\": 1}},");
	const CheckReport bad = checkData(DataDirectories{dir, "../core/data"});
	CHECK(find(bad, "data.wander_radius_or_region"));
	CHECK(find(bad, "data.bad_wander_pause"));
	CHECK(find(bad, "data.bad_wander_terrain"));
	CHECK(find(bad, "data.wander_needs_home"));
	CHECK(find(bad, "data.field_out_of_range"));

	const std::string refs = copyData("wander_refs", {}, test::backgroundDataDirectory());
	replaceIn(refs + "/game.json", "\"wander\": {\"radius\": 2,", "\"wander\": {\"from\": [6, 1], \"to\": [8, 2],");
	replaceIn(refs + "/areas/wren_yard.json", "\"actor\": \"yard_goat\"", "\"actor\": \"yard_ghost\"");
	replaceIn(refs + "/areas/wren_yard.json", "\"keeper\": \"yard_trader\"", "\"keeper\": \"stall_cat\"");
	const CheckReport r = checkData(DataDirectories{refs, "../core/data"});
	CHECK(find(r, "data.wander_home_outside") && find(r, "data.wander_home_outside")->message.params[0] == "yard_goat");
	CHECK(find(r, "data.touch_unknown_actor") && find(r, "data.touch_unknown_actor")->message.params[1] == "yard_ghost");
	CHECK(find(r, "data.keeper_not_actor") && find(r, "data.keeper_not_actor")->message.params[1] == "stall_cat");

	const char* touchNoActor = R"({"id": "a", "map": ["...", "..."], "markers": {"start": [0, 0]}, "hooks": [{"id": "x", "trigger": "touch", "event": "e"}]})";
	rules::Result<Area> missing = parseArea("a.json", touchNoActor);
	CHECK(!missing.ok() && missing.problems[0].message.key == "data.missing_field");
	const char* actorOnStep = R"({"id": "a", "map": ["...", "..."], "markers": {"start": [0, 0]}, "hooks": [{"id": "x", "at": [1, 1], "trigger": "step", "event": "e", "actor": "goat"}]})";
	rules::Result<Area> step = parseArea("a.json", actorOnStep);
	CHECK(!step.ok() && step.problems[0].message.key == "data.actor_touch_hooks_only");
}

// A wanderer's or patrol's home is no wall for reach, since it walks off it; a standing solid actor's home still is: the
// bell, used only from the cell below it, is reached through the goat's home while the goat wanders.
TEST(aMovingActorsHomeDoesNotBlockReach) {
	const std::string dir = copyData("moving_home", {}, test::backgroundDataDirectory());
	replaceIn(dir + "/areas/wren_yard.json", "\"#............#\",", "\"#.#.#........#\",");
	replaceIn(dir + "/game.json", "\"area\": \"wren_yard\", \"at\": [3, 4], \"wander\"", "\"area\": \"wren_yard\", \"at\": [3, 1], \"wander\"");
	CHECK(checkData(DataDirectories{dir, "../core/data"}).problems.empty());
	replaceIn(dir + "/game.json", "\"wander\": {\"radius\": 2, \"speed_percent\": 100, \"pause_ms\": [250, 750]}", "\"solid\": true");
	const CheckReport standing = checkData(DataDirectories{dir, "../core/data"});
	CHECK(find(standing, "check.object_not_reached") || find(standing, "check.hook_not_reached"));
}

// A wanderer's talk point in its home area is refused unless it answers on the actor (`talk_at: actor`), whose home is
// then where it starts and is used from; talk_at, turns and touch_from are refused on hooks they do not apply to.
TEST(aWanderersTalkPointLoadsOnlyWhenItAnswersOnTheActor) {
	const std::string dir = copyData("talk_at", {}, test::backgroundDataDirectory());
	replaceIn(dir + "/areas/wren_yard.json", "{\"id\": \"yard_goat_touch\", \"trigger\": \"touch\", \"event\": \"yard_goat_bleat\", \"actor\": \"yard_goat\"}",
		"{\"id\": \"yard_goat\", \"at\": [3, 4], \"event\": \"yard_goat_bleat\"}");
	CHECK(find(checkData(DataDirectories{dir, "../core/data"}), "data.actor_home_twice"));
	replaceIn(dir + "/areas/wren_yard.json", "\"event\": \"yard_goat_bleat\"", "\"event\": \"yard_goat_bleat\", \"talk_at\": \"actor\", \"turns\": true");
	const CheckReport good = checkData(DataDirectories{dir, "../core/data"});
	CHECK(good.problems.empty());
	replaceIn(dir + "/areas/wren_yard.json", "\"talk_at\": \"actor\", ", "");
	replaceIn(dir + "/game.json", "\"background_limit\": 2", "\"background_limit\": 2, \"talk_at\": \"actor\"");
	CHECK(checkData(DataDirectories{dir, "../core/data"}).problems.empty());

	replaceIn(dir + "/areas/wren_yard.json", "\"id\": \"yard_bell\", \"at\": [3, 0], \"event\": \"yard_bell\"", "\"id\": \"yard_bell\", \"at\": [3, 0], \"event\": \"yard_bell\", \"talk_at\": \"actor\"");
	CHECK(find(checkData(DataDirectories{dir, "../core/data"}), "data.talk_at_interact_hooks_only"));
	const auto refused = [](const char* hook) {
		const std::string text = std::string(R"({"id": "a", "map": ["...", "..."], "markers": {"start": [0, 0]}, "hooks": [)") + hook + "]}";
		rules::Result<Area> parsed = parseArea("a.json", text);
		return parsed.ok() ? std::string() : parsed.problems[0].message.key;
	};
	CHECK_EQ(refused(R"({"id": "x", "at": [1, 1], "trigger": "step", "event": "e", "talk_at": "actor"})"), std::string("data.talk_at_interact_hooks_only"));
	CHECK_EQ(refused(R"({"id": "x", "at": [1, 1], "event": "e", "actor_at": [0, 1], "talk_at": "actor"})"), std::string("data.talk_at_interact_hooks_only"));
	CHECK_EQ(refused(R"({"id": "x", "at": [1, 1], "event": "e", "talk_at": "near"})"), std::string("data.bad_talk_at"));
	CHECK_EQ(refused(R"({"id": "x", "at": [1, 1], "trigger": "step", "event": "e", "turns": true})"), std::string("data.turns_actor_hooks_only"));
	CHECK_EQ(refused(R"({"id": "x", "at": [1, 1], "event": "e", "touch_from": "keys"})"), std::string("data.touch_from_touch_hooks_only"));
	CHECK_EQ(refused(R"({"id": "x", "trigger": "touch", "actor": "goat", "event": "e", "touch_from": "path"})"), std::string("data.bad_touch_from"));
	const char* exit = R"({"id": "a", "map": ["...", "..."], "markers": {"start": [0, 0]}, "exits": [{"at": [1, 1], "to": "a", "marker": "start", "followers": "aside"}]})";
	rules::Result<Area> badExit = parseArea("a.json", exit);
	CHECK(!badExit.ok() && badExit.problems[0].message.key == "data.bad_followers_arrive");
	const auto town = [](const std::string& keys) {
		rules::Result<Area> parsed = parseArea("t.json", R"({"id": "t", "kind": "town", "map": ["...", "..."], "markers": {"start": [0, 0]}, )" + keys + "}");
		return parsed.ok() ? std::string() : parsed.problems[0].message.key;
	};
	CHECK_EQ(town(R"("arrival": "start", "arrival_followers": "right")"), std::string());
	CHECK_EQ(town(R"("return": {"area": "b", "marker": "gate", "followers": "behind"})"), std::string());
	CHECK_EQ(town(R"("ports": [{"id": "p", "at": [1, 1], "marker": "start", "followers": "up"}])"), std::string());
	CHECK_EQ(town(R"("arrival": "start", "arrival_followers": "aside")"), std::string("data.bad_followers_arrive"));
	CHECK_EQ(town(R"("return": {"area": "b", "marker": "gate", "followers": "aside"})"), std::string("data.bad_followers_arrive"));
	CHECK_EQ(town(R"("ports": [{"id": "p", "at": [1, 1], "marker": "start", "followers": "aside"}])"), std::string("data.bad_followers_arrive"));
	CHECK_EQ(town(R"("arrival_followers": "up")"), std::string("data.arrival_followers_unused"));
	CHECK_EQ(town(R"("arrival": "start", "arrival_followers": "up", "return": {"area": "b", "marker": "gate"})"), std::string("data.arrival_followers_unused"));
}

TEST(animationsMayGiveTheirLength) {
	const std::string dir = copyData("animation_lengths", {});
	replaceIn(dir + "/game.json", "\"animations\": [\"bow\",", "\"animations\": [{\"id\": \"bow\", \"seconds\": 0.75}, {\"id\": \"dissolve\", \"seconds\": 1.5},");
	rules::Result<GameData> loaded = loadGameData(DataDirectories{dir, "../core/data"});
	CHECK(loaded.ok());
	if (loaded.ok()) {
		const PresentationNames& names = loaded.value->config.presentation;
		CHECK_EQ(names.animationLength("bow"), 0.75);
		CHECK_EQ(names.animationLength("dissolve"), 1.5);
		CHECK_EQ(names.animationLength("hop"), 0.3);
		CHECK_EQ(names.animationLength("nod"), 0.0);
		CHECK(std::find(names.animations.begin(), names.animations.end(), "bow") != names.animations.end());
	}
	replaceIn(dir + "/game.json", "{\"id\": \"bow\", \"seconds\": 0.75}", "{\"id\": \"bow\"}, 3");
	const CheckReport bad = checkData(DataDirectories{dir, "../core/data"});
	CHECK(find(bad, "data.animation_shape"));
	CHECK(find(bad, "data.missing_field"));
}

// R101: a dungeon's `dungeon_exit` loads, and its area, place, facing and kinds are checked.
TEST(aDungeonsWayOutIsLoadedAndChecked) {
	const std::string dir = copyData("dungeon_exit", {});
	const DataDirectories directories{dir, "../core/data"};
	const std::string region = "\"region\": \"coast\",";
	replaceIn(dir + "/areas/kiln_upper.json", region, region + "\n\t\"dungeon_exit\": {\"area\": \"tidewater\", \"marker\": \"inn_front\", \"facing\": \"north\"},");
	rules::Result<GameData> loaded = loadGameData(directories);
	CHECK(loaded.ok());
	if (loaded.ok()) {
		const std::optional<DungeonExitTarget>& way = loaded.value->area("kiln_upper")->dungeonExit;
		CHECK(way && way->area == "tidewater" && way->marker == "inn_front" && way->facing == Facing::North);
	}
	const std::vector<std::pair<std::string, std::string>> cases = {
		{"{\"area\": \"atlantis\", \"at\": [1, 1]}", "data.dungeon_exit_unknown_area"},
		{"{\"area\": \"kiln_lower\", \"marker\": \"up_stair\"}", "data.dungeon_exit_into_dungeon"},
		{"{\"area\": \"tidewater\", \"marker\": \"pier\"}", "data.dungeon_exit_missing_marker"},
		{"{\"area\": \"tidewater\", \"at\": [0, 0]}", "data.dungeon_exit_not_walkable"},
		{"{\"area\": \"tidewater\", \"at\": [40, 1]}", "data.dungeon_exit_not_walkable"},
		{"{\"area\": \"tidewater\"}", "data.dungeon_exit_needs_place"},
		{"{\"area\": \"tidewater\", \"at\": [1, 1], \"marker\": \"start\"}", "data.dungeon_exit_needs_place"},
		{"{\"area\": \"tidewater\", \"at\": [1, 1], \"facing\": \"up\"}", "data.bad_facing"},
	};
	std::string current = "{\"area\": \"tidewater\", \"marker\": \"inn_front\", \"facing\": \"north\"}";
	for (const auto& [target, key] : cases) {
		replaceIn(dir + "/areas/kiln_upper.json", current, target);
		current = target;
		CHECK_EQ(find(checkData(directories), key) ? key : "nothing for " + target, key);
	}
	// Only a dungeon names a way out.
	replaceIn(dir + "/areas/kiln_upper.json", current, "{\"area\": \"tidewater\", \"at\": [1, 1]}");
	replaceIn(dir + "/areas/tidewater.json", "\"region\": \"coast\",", "\"region\": \"coast\",\n\t\"dungeon_exit\": {\"area\": \"saltgrass\", \"marker\": \"east_gate\"},");
	CHECK(find(checkData(directories), "data.dungeon_exit_not_dungeon"));
}

// R102: game.json's credits load outside the content hash; their entries, text keys and music are checked, and their ids
// are the names a script's `credits` may use.
TEST(creditsAreLoadedCheckedAndNamedForScripts) {
	const std::string dir = copyData("credits", {});
	const DataDirectories directories{dir, "../core/data"};
	replaceIn(dir + "/game.json", "\"title\":", "\"credits\": {\"finale\": {\"music\": \"title_harbor_dawn\", \"entries\": [{\"heading\": \"area.tidewater\", \"gap\": 1, \"pause_ms\": 900}, "
		"{\"line\": \"area.saltgrass\"}]}},\n\t\"title\":");
	rules::Result<GameData> loaded = loadGameData(directories);
	CHECK(loaded.ok());
	if (loaded.ok()) {
		CHECK_EQ(loaded.value->contentHash, test::data().contentHash);
		const Credits* credits = loaded.value->config.creditsById("finale");
		CHECK(credits && credits->music == "title_harbor_dawn" && credits->entries.size() == 2);
		CHECK(credits && credits->entries[0].kind == CreditsEntryKind::Heading && credits->entries[0].gap == 1 && credits->entries[0].pauseMs == 900);
		CHECK(credits && credits->entries[1].kind == CreditsEntryKind::Line && credits->entries[1].pauseMs == 2000);
		CHECK(loaded.value->registry.has(events::NameKind::credits, "finale"));
	}
	replaceIn(dir + "/game.json", "\"title_harbor_dawn\", \"entries\"", "\"no_song\", \"entries\"");
	replaceIn(dir + "/game.json", "{\"line\": \"area.saltgrass\"}", "{\"line\": \"credits.nobody\"}");
	const CheckReport r = checkData(directories);
	CHECK(find(r, "data.music_not_audio") && find(r, "data.music_not_audio")->message.params[0] == "no_song");
	CHECK(find(r, "data.text_not_in_table") && find(r, "data.text_not_in_table")->message.params[0] == "credits.nobody");
	replaceIn(dir + "/game.json", "{\"line\": \"credits.nobody\"}", "{\"line\": \"area.saltgrass\", \"heading\": \"area.tidewater\"}");
	CHECK(find(checkData(directories), "data.credits_entry_kind"));
	replaceIn(dir + "/game.json", "\"entries\": [", "\"entries\": [], \"unused\": [");
	const CheckReport empty = checkData(directories);
	CHECK(find(empty, "data.credits_empty"));
	CHECK(find(empty, "data.unknown_field"));
}

// Arriving on an exit cell does not take the exit, so the check walks out from an arrival marker standing on one: both
// rooms behind such doors are reached, and a room walled off from them is still reported.
TEST(theCheckWalksOutFromArrivalsOnExitCells) {
	const std::string dir = copyData("arrival_on_exit", {"door_a.json", "door_b.json"});
	const CheckReport r = checkData(DataDirectories{dir, ""});
	int unreached = 0;
	std::string which;
	for (const rules::Problem& p : r.problems) {
		if (p.message.key == "check.marker_not_reached" && (ends(p.file, "door_a.json") || ends(p.file, "door_b.json"))) {
			++unreached;
			which = p.message.params.empty() ? "" : p.message.params[0];
		}
	}
	CHECK_EQ(unreached, 1);
	CHECK_EQ(which, std::string("sealed"));
}

// A town's `return` must lead into another existing area, onto floor the party can walk to from that area's ways in;
// only a town has one.
TEST(aTownsReturnIsChecked) {
	const auto withReturn = [](const std::string& name, const std::string& value) {
		const std::string dir = copyData(name, {});
		replaceIn(dir + "/areas/tidewater.json", "\"kind\": \"town\",", "\"kind\": \"town\", \"return\": " + value + ",");
		return dir;
	};
	std::string dir = withReturn("return_ok", "{\"area\": \"reed_path\", \"at\": [2, 4], \"facing\": \"west\"}");
	CHECK(checkData(DataDirectories{dir, "../core/data"}).ok());

	dir = withReturn("return_islet", "{\"area\": \"brine_flats\", \"at\": [21, 1]}");
	replaceIn(dir + "/game.json", "\"crosses\": [\"floor\", \"water\"]", "\"crosses\": [\"floor\"]");
	const CheckReport islet = checkData(DataDirectories{dir, "../core/data"});
	const rules::Problem* stranded = find(islet, "check.return_not_reached");
	CHECK(stranded && stranded->message.params[0] == "tidewater" && stranded->message.params[1] == "brine_flats" && ends(stranded->file, "tidewater.json"));

	dir = withReturn("return_wall", "{\"area\": \"reed_path\", \"at\": [0, 0]}");
	CHECK(find(checkData(DataDirectories{dir, "../core/data"}), "data.return_not_walkable"));
	dir = withReturn("return_own", "{\"area\": \"tidewater\", \"marker\": \"start\"}");
	CHECK(find(checkData(DataDirectories{dir, "../core/data"}), "data.return_into_own_area"));
	dir = withReturn("return_marker", "{\"area\": \"reed_path\", \"marker\": \"nowhere\"}");
	CHECK(find(checkData(DataDirectories{dir, "../core/data"}), "data.return_missing_marker"));
	dir = copyData("return_field", {});
	replaceIn(dir + "/areas/reed_path.json", "\"kind\": \"field\",", "\"kind\": \"field\", \"return\": {\"area\": \"tidewater\", \"marker\": \"start\"},");
	CHECK(find(checkData(DataDirectories{dir, "../core/data"}), "data.return_not_town"));
}
