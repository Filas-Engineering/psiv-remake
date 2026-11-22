#pragma once

#include "game/world.h"

#include "rules/result.h"
#include "rules/types.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace game {

// A vessel is the inside of a ship between regions (F2): no battles, no vehicles, no returns to a town.
enum class AreaKind { Town, Field, Dungeon, Vessel };
// What a shop sells, for its label (F7): `shop.kind.<kind>` in the text tables.
enum class ShopKind { General, Weapon, Armor, Tool, Gift };
enum class EncounterMode { Visible, Random };
// Touch: the leader walks into an actor (`actor`), the actor wanders into the leader, or the leader interacts with it.
enum class HookTrigger { Interact, Step, Load, Flag, Touch };

const char* toString(AreaKind kind);
const char* toString(ShopKind kind);
bool parseShopKind(const std::string& text, ShopKind& out);

// Map rows: '.' floor, '#' wall, '~' water, ':' soft ground, '%' a barrier wall a vehicle can break (F3), '=' a counter
// (R109: never walked, used across). Only floor is walked on foot.
inline constexpr const char* kMapTiles = ".#~:%=";
const char* toString(EncounterMode mode);
bool parseEncounterMode(const std::string& text, EncounterMode& out);

struct Exit {
	Cell at;
	std::string toArea;
	std::string toMarker;
	// A pit (F8): a one-way drop, announced as a fall.
	bool fall = false;
	// Line in the area file, for problem reports; 0 when not read from a file.
	int line = 0;
	// While these flags do not hold the exit is closed and the cell is a wall.
	std::string requireFlag = {};
	std::string blockFlag = {};
	// The map character the cell is drawn as while the exit is closed (a door): '#' by default, 0 for the tile the map
	// has there. It blocks walking whatever it is.
	char closedTile = '#';
	// A sound cue (a presentation.audio id) a host plays as the party leaves by this exit; empty for none.
	std::string sound = {};
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
	// Flag hooks: fires once per game, which the save keeps, without a block flag.
	bool once = false;
	// Step hooks: fires on entering any cell of this map character, in place of `at`; 0 for none.
	char tile = 0;
	// Interact hooks: the cell is solid only while the hook's flags hold (and, for an actor's talk point, while the actor
	// is shown), so a hook switched off stops blocking the way.
	bool solidWhileActive = false;
	// Interact hooks: the cell the hook's actor stands on, is drawn on and is scripted from, apart from the cell the party
	// uses it from (a keeper behind a counter). That cell blocks walking while the actor is shown there.
	std::optional<Cell> actorAt = std::nullopt;
	// Interact hooks: used from the cell beyond a counter, a cell straight ahead that is not floor and holds nothing.
	bool across = false;
	// Interact hooks: further cells the object covers besides `at` (a long table); each is solid and answers as the hook.
	std::vector<Cell> cells = {};
	// Touch hooks: the actor of game.json whose touch fires it, wherever it stands.
	std::string actor = {};

	bool covers(Cell cell) const;
};

// Cells that carry the party one cell in `direction` every `stepMs` while it stands on them and the flags hold.
struct Conveyor {
	std::vector<Cell> cells;
	Facing direction = Facing::South;
	int stepMs = 250;
	// The same in microseconds: `step_ms` may have a fraction (stepMs is it rounded), which game.json's `step_timing`
	// `exact` keeps.
	std::int64_t stepUs = 250000;
	std::string requireFlag;
	std::string blockFlag;
	int line = 0;
};

// An item or equipment id a shop sells while its flags hold; prices come from the game's price table.
struct StockEntry {
	std::string id;
	std::string requireFlag;
	std::string blockFlag;
};

// A shop or rest point without a cell is reached only from a script. A shop whose flags do not hold is closed (F10).
struct Shop {
	std::string id;
	std::optional<Cell> at;
	ShopKind kind = ShopKind::General;
	std::vector<StockEntry> stock;
	int sellPercent = 50;
	std::string requireFlag = {};
	std::string blockFlag = {};
	int line = 0;
	// The actor of game.json who keeps it, for a host to turn toward or animate; empty for none.
	std::string keeper = {};
	// A text key prefix for the keeper's lines (R113): a host shows PREFIX.welcome, .confirm, .thanks and
	// .cannot_afford where the tables have them, else its own default lines. Empty for the defaults.
	std::string voice = {};
};

struct RestPoint {
	std::string id;
	std::optional<Cell> at;
	int cost = 0;
	int line = 0;
	std::string keeper = {};
	// The price is cost times the lineup's size (R113; Session::restCost).
	bool perMember = false;
	std::string voice = {};
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

// A port of the transport between regions (FLD-051, FLD-052): interacting with it lists the other open ports to travel
// to. Like an interact hook it may stand on any cell and is used from the floor beside it.
struct Port {
	std::string id;
	Cell at;
	// Where travellers arrive, a marker of the same area.
	std::string marker;
	// The port is open, as a place to leave from and to arrive at, once this flag is set; always when empty.
	std::string flag;
	// Closes the port again while set.
	std::string blockFlag = {};
	// A ship's port (F2): the party walks aboard the vessel area at its arrival marker and picks the destination at its
	// helm, among the open ports of the same ship. Empty: the destination menu opens at the port, among the ports
	// without a ship.
	std::string ship = {};
	int line = 0;
};

// A vessel's helm: interacting with it lists the open ports of its ship. It stands on any cell, like a port.
struct Helm {
	std::string id;
	Cell at;
	int line = 0;
};

// Where leaving a dungeon at once (FLD-051) takes the party from the dungeon area that names it, in place of the cell
// the party stood on before going in.
struct DungeonExitTarget {
	std::string area;
	// A marker of `area`; when empty, `at`.
	std::string marker;
	Cell at;
	// Left out, the party keeps the facing it had.
	std::optional<Facing> facing;
	int line = 0;
};

// Map cells that become `tile` while the flag is set (F8, F10): a bridge repaired, a barrier removed, a place sealed.
// Later changes win over earlier ones on the same cell.
struct CellChange {
	std::string flag;
	char tile = '.';
	std::vector<Cell> cells;
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
	// Steps between battles, drawn uniformly (ENC-040; the original's distribution is ENC-005). maxSteps 0 (R115) draws no
	// count: once minSteps steps are taken, each step rolls stepChance, with no step at which a battle must come.
	int minSteps = 12;
	int maxSteps = 24;
	// Distance units one cell adds to the counter (ENC-041).
	int stepDistance = 1;
	// Steps after entering the area before any roll.
	int graceSteps = 0;
	// When nonzero, each step has a 1 in N chance of a battle in place of the counter; aboard a vehicle, the second N
	// when nonzero.
	int stepChance = 0;
	int vehicleStepChance = 0;
	// No roll while the leader stands on or next to an exit.
	bool safeNearExits = false;
	// The grace steps start again after every battle fought in the zone, as after entering the area.
	bool graceAfterBattle = false;
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
	// Time to walk one cell; `step_ms` may have a fraction, kept in microseconds in stepUs (stepMs is it rounded).
	int stepMs = 250;
	std::int64_t stepUs = 250000;
	// False when the area's own `arrive_notice` is false: entering it shows no arrival notice (GameConfig::arriveNotice).
	bool arriveNotice = true;
	// The track for the area outside events and battles, an audio id from game.json; empty when the data names none.
	std::string music;
	// The world or region the area belongs to (FLD-050); a return to a town reaches only towns of the same region.
	std::string region;
	// A town's marker where a return to it lands (FLD-051); a town without one is no destination.
	std::string arrival;
	// A dungeon's way out for the dungeon-exit item and technique; without one, the cell the party entered from.
	std::optional<DungeonExitTarget> dungeonExit;
	std::vector<std::string> rows;
	std::map<std::string, Cell> markers;
	std::vector<Exit> exits;
	std::vector<Hook> hooks;
	std::vector<Shop> shops;
	std::vector<RestPoint> rests;
	std::vector<Chest> chests;
	std::vector<Port> ports;
	std::vector<Zone> zones;
	std::vector<Helm> helms;
	// In a vessel: cells that take the party back ashore, to the port it boarded from.
	std::vector<Cell> gangway;
	// Counter cells (R109): the map's '=' cells and those the area's `counters` lists. Interaction tries the cell behind a
	// faced counter that holds nothing, and runs what stands there as if faced.
	std::set<Cell> counters;
	std::vector<CellChange> changes;
	std::vector<Conveyor> conveyors;
	// Cells of interact hooks that are solid only while active and are not now; the session's view of the area sets them.
	std::set<Cell> passable = {};
	// Closed exits drawn as something other than a wall; the session's view of the area sets them.
	std::set<Cell> closed = {};
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
	// An interact hook covers `at` and its `cells`.
	const Hook* hookAt(Cell cell, HookTrigger trigger) const;
	// A step hook on the cell, or one on its map character.
	const Hook* stepHookAt(Cell cell) const;
	const Shop* shopAt(Cell cell) const;
	const RestPoint* restAt(Cell cell) const;
	const Chest* chestAt(Cell cell) const;
	const Port* portAt(Cell cell) const;
	const Helm* helmAt(Cell cell) const;
	bool gangwayAt(Cell cell) const;
	const Port* port(const std::string& id) const;
	const Shop* shop(const std::string& id) const;
	const RestPoint* rest(const std::string& id) const;
};

// Whether the flags let the party use it now.
bool portOpen(const Port& port, const World& world);
bool exitOpen(const Exit& exit, const World& world);

// Errors are "file:line: message", one per line.
rules::Result<Area> parseArea(const std::string& file, const std::string& text);

}
