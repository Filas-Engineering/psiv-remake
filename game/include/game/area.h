#pragma once

#include "game/world.h"

#include "rules/result.h"
#include "rules/types.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace game {

enum class AreaKind { Town, Field, Dungeon };
enum class EncounterMode { Visible, Random };
enum class HookTrigger { Interact, Step, Load, Flag };

const char* toString(AreaKind kind);
const char* toString(EncounterMode mode);
bool parseEncounterMode(const std::string& text, EncounterMode& out);

struct Exit {
	Cell at;
	std::string toArea;
	std::string toMarker;
	// Line in the area file, for problem reports; 0 when not read from a file.
	int line = 0;
};

// A field hook (FLD-023): runs one event script when it fires and its flag conditions hold. A Flag hook fires
// whenever control is in the field and its conditions hold, so it needs a block flag its event sets.
struct Hook {
	std::string id;
	Cell at;
	HookTrigger trigger = HookTrigger::Interact;
	std::string event;
	std::string requireFlag;
	std::string blockFlag;
	int line = 0;
};

// A shop or rest point without a cell is reached only from a script.
struct Shop {
	std::string id;
	std::optional<Cell> at;
	// Item or equipment ids; prices come from the game's price table.
	std::vector<std::string> stock;
	int sellPercent = 50;
	int line = 0;
};

struct RestPoint {
	std::string id;
	std::optional<Cell> at;
	int cost = 0;
	int line = 0;
};

struct Chest {
	std::string id;
	Cell at;
	// Set when opened; never cleared (FLD-032).
	std::string flag;
	rules::Inventory items;
	std::map<std::string, int> equipment;
	int money = 0;
	int line = 0;
};

struct TableEntry {
	std::string encounter;
	int weight = 1;
};

struct VisibleRules {
	std::vector<Cell> spawns;
	int cap = 1;
	int detectionRadius = 4;
	int leashRadius = 3;
	// Percent of the leader's walking speed (ENC-013).
	int chaseSpeedPercent = 110;
	int chaseMs = 6000;
	int wanderSpeedPercent = 50;
	// Chance per wander step that the enemy moves at all.
	int wanderPercent = 50;
};

struct RandomRules {
	// Steps between battles, drawn uniformly (ENC-040; the original's distribution is ENC-005).
	int minSteps = 12;
	int maxSteps = 24;
	// Distance units one cell adds to the counter (ENC-041).
	int stepDistance = 1;
};

struct Zone {
	std::string id;
	Cell from;
	Cell to;
	std::vector<TableEntry> table;
	VisibleRules visible;
	RandomRules random;
	int line = 0;

	bool contains(Cell cell) const;
};

struct Area {
	// The display name is the text key area.<id>.
	std::string id;
	AreaKind kind = AreaKind::Field;
	EncounterMode mode = EncounterMode::Visible;
	// Time to walk one cell.
	int stepMs = 250;
	std::vector<std::string> rows;
	std::map<std::string, Cell> markers;
	std::vector<Exit> exits;
	std::vector<Hook> hooks;
	std::vector<Shop> shops;
	std::vector<RestPoint> rests;
	std::vector<Chest> chests;
	std::vector<Zone> zones;
	// The file game.json names for the area, and the line of each marker, for problem reports.
	std::string file;
	std::map<std::string, int> markerLines;

	int width() const;
	int height() const;
	bool inside(Cell cell) const;
	// Floor that no interactable stands on.
	bool walkable(Cell cell) const;
	const Zone* zoneAt(Cell cell) const;
	const Zone* zone(const std::string& id) const;
	const Exit* exitAt(Cell cell) const;
	const Hook* hookAt(Cell cell, HookTrigger trigger) const;
	const Shop* shopAt(Cell cell) const;
	const RestPoint* restAt(Cell cell) const;
	const Chest* chestAt(Cell cell) const;
	const Shop* shop(const std::string& id) const;
	const RestPoint* rest(const std::string& id) const;
};

// Errors are "file:line: message", one per line.
rules::Result<Area> parseArea(const std::string& file, const std::string& text);

}
