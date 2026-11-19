#pragma once

#include "game/area.h"
#include "game/world.h"

#include "events/glossary.h"
#include "events/program.h"
#include "rules/database.h"
#include "rules/result.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace game {

struct Boss {
	std::string encounter;
	// The story flag the boss's event sets when it falls; pacing keys M5 by it.
	std::string flag;
};

// game.json: everything about the adventure that is not an area, a script or battle data.
struct GameConfig {
	std::string title;
	std::string coreData;
	std::vector<std::string> areaFiles;
	std::vector<std::string> eventFiles;
	std::string glossaryFile;
	// Display names for script speaker ids.
	std::map<std::string, std::string> speakers;

	std::string startArea;
	std::string startMarker;
	Facing startFacing = Facing::South;
	std::vector<std::string> startLineup;
	int startLevel = 1;
	int startMoney = 0;
	rules::Inventory startItems;
	// Level a character has when an event first adds it.
	std::map<std::string, int> joinLevels;

	// Buy prices for items and equipment. Anything not listed cannot be bought or sold.
	std::map<std::string, int> prices;
	// SAV-036 is to measure; these are the proposed defaults.
	int itemCap = 99;
	int moneyCap = 999999;

	int tickMs = 50;
	int graceMs = 3000;
	// Battle time per resolved round, for play time and pacing; headless battles have no clock.
	int roundMs = 4000;
	int autosaveSlots = 3;
	int manualSlots = 10;
	std::vector<Boss> bosses;
};

struct GameData {
	GameConfig config;
	rules::Database db;
	events::Program program;
	events::Glossary glossary;
	std::map<std::string, Area> areas;
	// Battle data hash mixed with the script fingerprint and the area texts.
	std::uint64_t contentHash = 0;

	const Area* area(const std::string& id) const;
	const Boss* boss(const std::string& encounter) const;
	int price(const std::string& id) const;
};

// Loads game.json from `directory`, then the battle data, areas, scripts and glossary it names, and checks
// every cross-reference (exits, hooks, encounters, warps, shop stock).
rules::Result<GameData> loadGameData(const std::string& directory);

}
