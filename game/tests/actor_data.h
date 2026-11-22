#pragma once

#include "game/save.h"

#include <filesystem>
#include <string>
#include <system_error>

// The placeholder adventure with Wren Hollow and Wren Loft added from tests/fixtures/actors: their areas and events, the
// hollow's actors in game.json (the sentinel in an enemy's figure with a home of its own), its flag and animation, and
// their text. The shipped data stays without them, so the MonoGame sample's runs, which list every actor and area, do
// not change.
namespace actor_data {

inline bool insertAfter(std::string& text, const std::string& anchor, const std::string& added) {
	const std::size_t at = text.find(anchor);
	if (at == std::string::npos) {
		return false;
	}
	text.insert(at + anchor.size(), added);
	return true;
}

inline bool appendToList(std::string& text, const std::string& key, const std::string& added) {
	const std::size_t at = text.find("\"" + key + "\": [");
	const std::size_t end = at == std::string::npos ? at : text.find(']', at);
	if (end == std::string::npos) {
		return false;
	}
	text.insert(end, ", " + added);
	return true;
}

// Copies GAME and CORE to OUT/game/data and OUT/core/data, where game.json's core_data looks, and adds the fixtures in
// FIXTURES. Returns the problem, or empty.
inline std::string make(const std::string& game, const std::string& core, const std::string& fixtures, const std::string& out) {
	namespace fs = std::filesystem;
	std::error_code error;
	fs::remove_all(out, error);
	const std::string data = out + "/game/data";
	fs::create_directories(out + "/game", error);
	fs::create_directories(out + "/core", error);
	fs::copy(game, data, fs::copy_options::recursive, error);
	if (!error) {
		fs::copy(core, out + "/core/data", fs::copy_options::recursive, error);
	}
	if (!error) {
		fs::copy_file(fixtures + "/wren_hollow.json", data + "/areas/wren_hollow.json", error);
	}
	if (!error) {
		fs::copy_file(fixtures + "/hollow.ev", data + "/events/hollow.ev", error);
	}
	if (!error) {
		fs::copy_file(fixtures + "/wren_loft.json", data + "/areas/wren_loft.json", error);
	}
	if (!error) {
		fs::copy_file(fixtures + "/loft.ev", data + "/events/loft.ev", error);
	}
	if (error) {
		return "cannot copy the data: " + error.message();
	}
	std::string config;
	if (!game::readFile(data + "/game.json", config)) {
		return "cannot read " + data + "/game.json";
	}
	const bool patched = appendToList(config, "areas", "\"areas/wren_hollow.json\", \"areas/wren_loft.json\"") &&
		appendToList(config, "events", "\"events/hollow.ev\", \"events/loft.ev\"") &&
		insertAfter(config, "\"nameless_actors\": [", "\"hollow_drifter\", \"hollow_sentinel\", ") &&
		insertAfter(config, "\"actors\": [", "\"hollow_drifter\", {\"id\": \"hollow_sentinel\", \"sprite\": {\"enemy\": \"aegis_sentinel\"}, \"area\": \"wren_hollow\", "
			"\"at\": [7, 4], \"facing\": \"west\"}, \"tallykeeper\", ") &&
		insertAfter(config, "\"animations\": [", "\"tally_nod\", ") && insertAfter(config, "\"flags\": [", "\"hollow.met\", ");
	std::string problem;
	if (!patched || !game::writeFileAtomic(data + "/game.json", config, problem)) {
		return "cannot add Wren Hollow to game.json " + problem;
	}
	for (const auto& [code, names] : {std::make_pair(std::string("en"), std::make_pair(std::string("Wren Hollow"), std::string("Wren Loft"))),
			 std::make_pair(std::string("ja"), std::make_pair(std::string("みそさざいの くぼち"), std::string("みそさざいの やねうら")))}) {
		std::string text;
		std::string lines;
		std::string loft;
		std::string strings;
		const std::string table = data + "/text/" + code;
		if (!game::readFile(table + ".json", text) || !game::readFile(table + ".strings", strings) || !game::readFile(fixtures + "/hollow_" + code + ".strings", lines) ||
			!game::readFile(fixtures + "/loft_" + code + ".strings", loft) ||
			!insertAfter(text, "\"text\": {", "\n\t\t\"area.wren_hollow\": \"" + names.first + "\",\n\t\t\"area.wren_loft\": \"" + names.second + "\",") ||
			!game::writeFileAtomic(table + ".json", text, problem) || !game::writeFileAtomic(table + ".strings", strings + lines + loft, problem)) {
			return "cannot add Wren Hollow's " + code + " text " + problem;
		}
	}
	return "";
}

// The actor data with Wren Cellar added from FIXTURES/../interact and game.json's interaction rules turned on: `interact`
// nearby, `run_speed_percent` 200 and `vehicle_opening` normal, for data/scripts/interact.txt. A copy of its own, so the
// actor and party scripts play with the rules off as before.
inline std::string makeInteract(const std::string& game, const std::string& core, const std::string& fixtures, const std::string& out) {
	std::string problem = make(game, core, fixtures, out);
	if (!problem.empty()) {
		return problem;
	}
	namespace fs = std::filesystem;
	std::error_code error;
	const std::string data = out + "/game/data";
	const std::string from = fixtures + "/../interact";
	fs::copy_file(from + "/wren_cellar.json", data + "/areas/wren_cellar.json", error);
	if (!error) {
		fs::copy_file(from + "/cellar.ev", data + "/events/cellar.ev", error);
	}
	if (error) {
		return "cannot copy Wren Cellar: " + error.message();
	}
	std::string config;
	if (!game::readFile(data + "/game.json", config)) {
		return "cannot read " + data + "/game.json";
	}
	const bool patched = insertAfter(config, "{", "\n\t\"interact\": \"nearby\",\n\t\"run_speed_percent\": 200,\n\t\"vehicle_opening\": \"normal\",") &&
		appendToList(config, "areas", "\"areas/wren_cellar.json\"") && appendToList(config, "events", "\"events/cellar.ev\"") &&
		insertAfter(config, "\"nameless_actors\": [", "\"cellar_cook\", ") && insertAfter(config, "\"actors\": [", "\"cellar_cook\", ") &&
		insertAfter(config, "\"flags\": [", "\"cellar.door_open\", ");
	if (!patched || !game::writeFileAtomic(data + "/game.json", config, problem)) {
		return "cannot add Wren Cellar to game.json " + problem;
	}
	for (const auto& [code, name] : {std::make_pair(std::string("en"), std::string("Wren Cellar")), std::make_pair(std::string("ja"), std::string("みそさざいの ちかしつ"))}) {
		std::string text;
		std::string strings;
		std::string lines;
		const std::string table = data + "/text/" + code;
		if (!game::readFile(table + ".json", text) || !game::readFile(table + ".strings", strings) || !game::readFile(from + "/cellar_" + code + ".strings", lines) ||
			!insertAfter(text, "\"text\": {", "\n\t\t\"area.wren_cellar\": \"" + name + "\",") || !game::writeFileAtomic(table + ".json", text, problem) ||
			!game::writeFileAtomic(table + ".strings", strings + lines, problem)) {
			return "cannot add Wren Cellar's " + code + " text " + problem;
		}
	}
	return "";
}

// The actor data with Wren Yard added from FIXTURES/../background, for data/scripts/background.txt: its area and events,
// its actors in game.json (the goat wandering about its pen, the lamplighter, the crow and the stall's trader), flags,
// variable and text, and `background_limit` 2; and Wren Den, a dungeon with its own way out whose plaque plays the
// credits game.json's `credits` gains and ends the game. A copy of its own, so the other scripts play without it.
inline std::string makeBackground(const std::string& game, const std::string& core, const std::string& fixtures, const std::string& out) {
	std::string problem = make(game, core, fixtures, out);
	if (!problem.empty()) {
		return problem;
	}
	namespace fs = std::filesystem;
	std::error_code error;
	const std::string data = out + "/game/data";
	const std::string from = fixtures + "/../background";
	fs::copy_file(from + "/wren_yard.json", data + "/areas/wren_yard.json", error);
	if (!error) {
		fs::copy_file(from + "/yard.ev", data + "/events/yard.ev", error);
	}
	if (!error) {
		fs::copy_file(from + "/wren_den.json", data + "/areas/wren_den.json", error);
	}
	if (!error) {
		fs::copy_file(from + "/den.ev", data + "/events/den.ev", error);
	}
	if (error) {
		return "cannot copy Wren Yard: " + error.message();
	}
	std::string config;
	if (!game::readFile(data + "/game.json", config)) {
		return "cannot read " + data + "/game.json";
	}
	const bool patched = insertAfter(config, "{", "\n\t\"background_limit\": 2,") &&
		insertAfter(config, "{", "\n\t\"credits\": {\"wren_end\": {\"music\": \"title_harbor_dawn\", \"entries\": [{\"heading\": \"credits.wren_end.title\", \"pause_ms\": 2500}, "
			"{\"line\": \"credits.wren_end.cast\", \"gap\": 1}, {\"heading\": \"credits.wren_end.thanks\", \"gap\": 2, \"pause_ms\": 1500}, "
			"{\"line\": \"credits.wren_end.player\"}]}},") &&
		appendToList(config, "areas", "\"areas/wren_yard.json\", \"areas/wren_den.json\"") && appendToList(config, "events", "\"events/yard.ev\", \"events/den.ev\"") &&
		insertAfter(config, "\"nameless_actors\": [", "\"yard_crow\", \"yard_goat\", \"yard_lamplighter\", ") &&
		insertAfter(config, "\"actors\": [",
			"{\"id\": \"yard_trader\", \"area\": \"wren_yard\", \"at\": [9, 1]}, "
			"{\"id\": \"yard_goat\", \"sprite\": {\"look\": \"goat\"}, \"area\": \"wren_yard\", \"at\": [3, 4], "
			"\"wander\": {\"radius\": 2, \"speed_percent\": 100, \"pause_ms\": [250, 750]}}, "
			"{\"id\": \"yard_lamplighter\", \"area\": \"wren_yard\", \"at\": [1, 2], \"facing\": \"east\"}, "
			"{\"id\": \"yard_crow\", \"sprite\": {\"look\": \"crow\"}, \"area\": \"wren_yard\", \"at\": [10, 1], \"solid\": false}, ") &&
		insertAfter(config, "\"flags\": [", "\"yard.lamps_lit\", \"yard.lit_seen\", ") && insertAfter(config, "\"vars\": {", "\"yard.rounds\": 0, ");
	if (!patched || !game::writeFileAtomic(data + "/game.json", config, problem)) {
		return "cannot add Wren Yard to game.json " + problem;
	}
	const std::string en = "\n\t\t\"area.wren_yard\": \"Wren Yard\",\n\t\t\"area.wren_den\": \"Wren Den\",\n\t\t\"credits.wren_end.title\": \"Wren Hollow\",\n"
		"\t\t\"credits.wren_end.cast\": \"The yard trader, the goat and the crow\",\n\t\t\"credits.wren_end.thanks\": \"With thanks\",\n"
		"\t\t\"credits.wren_end.player\": \"And to you, for walking every room\",";
	const std::string ja = "\n\t\t\"area.wren_yard\": \"みそさざいの にわ\",\n\t\t\"area.wren_den\": \"みそさざいの ほらあな\",\n\t\t\"credits.wren_end.title\": \"みそさざいの くぼち\",\n"
		"\t\t\"credits.wren_end.cast\": \"にわの あきんど、やぎ、そして からす\",\n\t\t\"credits.wren_end.thanks\": \"かんしゃを こめて\",\n"
		"\t\t\"credits.wren_end.player\": \"そして すべての へやを あるいた あなたに\",";
	for (const auto& [code, added] : {std::make_pair(std::string("en"), en), std::make_pair(std::string("ja"), ja)}) {
		std::string text;
		std::string strings;
		std::string lines;
		std::string den;
		const std::string table = data + "/text/" + code;
		if (!game::readFile(table + ".json", text) || !game::readFile(table + ".strings", strings) || !game::readFile(from + "/yard_" + code + ".strings", lines) ||
			!game::readFile(from + "/den_" + code + ".strings", den) || !insertAfter(text, "\"text\": {", added) ||
			!game::writeFileAtomic(table + ".json", text, problem) || !game::writeFileAtomic(table + ".strings", strings + lines + den, problem)) {
			return "cannot add Wren Yard's " + code + " text " + problem;
		}
	}
	return "";
}

// The actor data with Wren Gallery added from FIXTURES/../scene, for data/scripts/scene.txt: its area (an exit with a
// sound) and events, game.json's `presentation.pictures`, `music.fade_ms` 400 and the exit's cue, and its text. A copy of
// its own, so the other scripts play without it.
inline std::string makeScene(const std::string& game, const std::string& core, const std::string& fixtures, const std::string& out) {
	std::string problem = make(game, core, fixtures, out);
	if (!problem.empty()) {
		return problem;
	}
	namespace fs = std::filesystem;
	std::error_code error;
	const std::string data = out + "/game/data";
	const std::string from = fixtures + "/../scene";
	fs::copy_file(from + "/wren_gallery.json", data + "/areas/wren_gallery.json", error);
	if (!error) {
		fs::copy_file(from + "/gallery.ev", data + "/events/gallery.ev", error);
	}
	if (error) {
		return "cannot copy Wren Gallery: " + error.message();
	}
	std::string config;
	if (!game::readFile(data + "/game.json", config)) {
		return "cannot read " + data + "/game.json";
	}
	const bool patched = appendToList(config, "areas", "\"areas/wren_gallery.json\"") && appendToList(config, "events", "\"events/gallery.ev\"") &&
		insertAfter(config, "\"audio\": [", "\"gallery_door\", ") &&
		insertAfter(config, "\"presentation\": {", "\n\t\t\"pictures\": [\"beacon\", \"farewell\", \"harbor_dawn\", \"storm\"],") &&
		insertAfter(config, "\"music\": {", "\"fade_ms\": 400, ");
	if (!patched || !game::writeFileAtomic(data + "/game.json", config, problem)) {
		return "cannot add Wren Gallery to game.json " + problem;
	}
	for (const auto& [code, name] : {std::make_pair(std::string("en"), std::string("Wren Gallery")), std::make_pair(std::string("ja"), std::string("みそさざいの がろう"))}) {
		std::string text;
		std::string strings;
		std::string lines;
		const std::string table = data + "/text/" + code;
		if (!game::readFile(table + ".json", text) || !game::readFile(table + ".strings", strings) || !game::readFile(from + "/gallery_" + code + ".strings", lines) ||
			!insertAfter(text, "\"text\": {", "\n\t\t\"area.wren_gallery\": \"" + name + "\",") || !game::writeFileAtomic(table + ".json", text, problem) ||
			!game::writeFileAtomic(table + ".strings", strings + lines, problem)) {
			return "cannot add Wren Gallery's " + code + " text " + problem;
		}
	}
	return "";
}

// The actor data with Wren Mill added from FIXTURES/../motion, for data/scripts/motion.txt: its area (a raft that carries
// the party when boarded and a skiff that shuttles by itself) and event, its actors in game.json (a cat wandering along x
// that often stands, a mill hand on patrol and a water wheel on a pixel path with a footprint), `cell_pixels` 16 and its
// text. A copy of its own, so the other scripts play without it.
inline std::string makeMotion(const std::string& game, const std::string& core, const std::string& fixtures, const std::string& out) {
	std::string problem = make(game, core, fixtures, out);
	if (!problem.empty()) {
		return problem;
	}
	namespace fs = std::filesystem;
	std::error_code error;
	const std::string data = out + "/game/data";
	const std::string from = fixtures + "/../motion";
	fs::copy_file(from + "/wren_mill.json", data + "/areas/wren_mill.json", error);
	if (!error) {
		fs::copy_file(from + "/mill.ev", data + "/events/mill.ev", error);
	}
	if (error) {
		return "cannot copy Wren Mill: " + error.message();
	}
	std::string config;
	if (!game::readFile(data + "/game.json", config)) {
		return "cannot read " + data + "/game.json";
	}
	const bool patched = insertAfter(config, "{", "\n\t\"cell_pixels\": 16,") && appendToList(config, "areas", "\"areas/wren_mill.json\"") &&
		appendToList(config, "events", "\"events/mill.ev\"") && insertAfter(config, "\"nameless_actors\": [", "\"mill_cat\", \"mill_hand\", \"mill_wheel\", ") &&
		insertAfter(config, "\"actors\": [",
			"{\"id\": \"mill_cat\", \"sprite\": {\"look\": \"cat\"}, \"area\": \"wren_mill\", \"at\": [3, 1], \"solid\": false, "
			"\"wander\": {\"radius\": 2, \"speed_percent\": 100, \"pause_ms\": [200, 200], \"stand_percent\": 50, \"axis\": \"x\"}}, "
			"{\"id\": \"mill_hand\", \"area\": \"wren_mill\", \"at\": [4, 4], \"facing\": \"north\", "
			"\"patrol\": {\"speed_percent\": 100, \"steps\": [{\"walk\": \"north\", \"count\": 2, \"face\": \"east\", \"pause_ms\": 400}, {\"to\": [4, 4], \"face\": \"north\"}]}}, "
			"{\"id\": \"mill_wheel\", \"sprite\": {\"look\": \"wheel\"}, \"area\": \"wren_mill\", \"at\": [11, 4], "
			"\"path\": {\"offsets\": [[0, 0], [4, -2], [8, 0], [4, 2]], \"ticks\": 2, \"footprint\": [1, 1]}}, ");
	if (!patched || !game::writeFileAtomic(data + "/game.json", config, problem)) {
		return "cannot add Wren Mill to game.json " + problem;
	}
	for (const auto& [code, name] : {std::make_pair(std::string("en"), std::string("Wren Mill")), std::make_pair(std::string("ja"), std::string("みそさざいの こなひき"))}) {
		std::string text;
		std::string strings;
		std::string lines;
		const std::string table = data + "/text/" + code;
		if (!game::readFile(table + ".json", text) || !game::readFile(table + ".strings", strings) || !game::readFile(from + "/mill_" + code + ".strings", lines) ||
			!insertAfter(text, "\"text\": {", "\n\t\t\"area.wren_mill\": \"" + name + "\",") || !game::writeFileAtomic(table + ".json", text, problem) ||
			!game::writeFileAtomic(table + ".strings", strings + lines, problem)) {
			return "cannot add Wren Mill's " + code + " text " + problem;
		}
	}
	return "";
}

}
