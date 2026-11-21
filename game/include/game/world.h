#pragma once

#include "rules/character.h"
#include "rules/combo.h"
#include "rules/macro.h"
#include "rules/rng.h"
#include "rules/types.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace game {

struct Cell {
	int x = 0;
	int y = 0;

	bool operator==(const Cell& other) const { return x == other.x && y == other.y; }
	bool operator!=(const Cell& other) const { return !(*this == other); }
	bool operator<(const Cell& other) const { return y != other.y ? y < other.y : x < other.x; }
};

enum class Facing { North, East, South, West };

Cell facingVector(Facing facing);
Cell stepFrom(Cell cell, Facing facing);
const char* toString(Facing facing);
bool parseFacing(const std::string& text, Facing& out);

// The settings saved per game (SAV-005). Everything else is in the global profile (profile.h).
struct Settings {
	std::string difficulty = "original";
	rules::RewardMultiplier rewardMultiplier = rules::RewardMultiplier::X1;
};

// A guild job's state only moves forward (UI-012); a job that is not yet available has no entry.
enum class JobState { Available, Accepted, Completed, Missed };

const char* toString(JobState state);
bool parseJobState(const std::string& text, JobState& out);

enum class EnemyState { Idle, Chasing, Returning };

const char* toString(EnemyState state);
bool parseEnemyState(const std::string& text, EnemyState& out);

// One visible enemy in the field. It stands for the group drawn when it spawned (ENC-010).
struct VisibleEnemy {
	int id = 0;
	std::string zone;
	std::string encounter;
	Cell spawn;
	Cell position;
	Facing facing = Facing::South;
	EnemyState state = EnemyState::Idle;
	int stateMs = 0;
	// Hundredths of a cell travelled toward the next cell.
	int moveProgress = 0;
};

// Where an event script left one of the game's actors (game.json `actors`). Scripts place actors in the area the
// party is in; an actor keeps its place when the party leaves.
struct ActorPlace {
	// Empty while the actor has no cell: shown or hidden before any move, with no talk point in the party's area.
	std::string area;
	Cell cell;
	Facing facing = Facing::South;
	bool visible = true;
};

// A vehicle the party has (FLD-060). While the party rides it, `area` and `cell` follow the party.
struct VehiclePlace {
	std::string area;
	Cell cell;
	Facing facing = Facing::South;
	// Where it was last boarded or put by a script. It goes back there when it would be left where the party cannot
	// reach it (FLD-063).
	std::string homeArea;
	Cell home;
	// Its skill uses left from earlier battles (B19); a skill left out is full. Rest fills them all.
	std::map<std::string, int> skillUses = {};
};

// A save chunk this build does not know, kept and written back unchanged (SAV-020).
struct UnknownChunk {
	std::string type;
	int version = 0;
	std::string payload;
};

struct World {
	std::map<std::string, bool> flags;
	std::map<std::string, std::int64_t> vars;

	std::vector<rules::PartyMember> roster;
	// Character ids of the active party, in battle order.
	std::vector<std::string> lineup;
	// Names given at name entry, by character id.
	std::map<std::string, std::string> memberNames;
	rules::Inventory items;
	rules::EquipmentBag equipmentBag;
	int money = 0;

	std::vector<rules::Macro> macros;
	std::string autoMacro;
	rules::ComboLog comboLog;

	std::string area;
	Cell position;
	Facing facing = Facing::South;
	std::set<std::string> visitedAreas;
	bool playerControl = true;

	std::vector<VisibleEnemy> enemies;
	int nextEnemyId = 1;
	// Actors a script moved, turned, showed or hid, by actor id. The others stand at their talk points.
	std::map<std::string, ActorPlace> actors;
	// The vehicles the party has, by vehicle id, and the one it rides (empty for none).
	std::map<std::string, VehiclePlace> vehicles;
	std::string aboard;
	// An actor who walks with the party without joining it (F14); empty for none.
	std::string follower;
	// Where the party stood outside the dungeon it is in, which leaving the dungeon at once returns to (FLD-051); the
	// area is empty when the party is not in a dungeon or came in by a script's warp from another dungeon.
	std::string dungeonExitArea;
	Cell dungeonExitCell;
	// The port the party walked aboard a ship from, while it is in the vessel (F2); empty ashore.
	std::string shipPort;
	// Barrier cells a vehicle broke through (F3), by area: for good, and in the current area until the party leaves it.
	std::map<std::string, std::set<Cell>> brokenCells;
	std::set<Cell> brokenHere;
	// Time left in the post-battle grace period (ENC-015, ENC-042).
	int graceMs = 0;
	// Random mode distance counter and the threshold it is racing (ENC-040).
	int randomCounter = 0;
	int randomThreshold = 0;
	rules::Rng encounterStream{0};

	std::map<std::string, JobState> jobs;
	// Encyclopedia entries, kept once unlocked even if their flag is cleared later (TXT-041).
	std::set<std::string> unlocked;
	// Party talk that already came up by itself (FLD-073), as "<set id>@<area id>": once per area per set, and a set
	// holds for one stretch of the story.
	std::set<std::string> ambientHeard;

	std::int64_t playTimeMs = 0;
	Settings settings;
	// A developer command changed this game (docs/dev-tools.md). Never cleared once set.
	bool devMarked = false;
	std::vector<UnknownChunk> unknownChunks;

	rules::PartyMember* member(const std::string& characterId);
	const rules::PartyMember* member(const std::string& characterId) const;
	bool flag(const std::string& name) const;
};

// Field-by-field comparison of everything a save holds. On a mismatch, `diff` names the first field that differs.
bool sameWorld(const World& a, const World& b, std::string* diff = nullptr);

}
