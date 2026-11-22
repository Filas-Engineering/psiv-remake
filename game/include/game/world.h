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

// How a hidden actor left: a plain `hide` (None), or the standard animation that hid it.
enum class ActorVanish { None, Faded, Dissolved, BlinkedOut };

const char* toString(ActorVanish vanish);
bool parseActorVanish(const std::string& text, ActorVanish& out);

// A script's `remove`: gone until the party next enters an area, or for good.
enum class ActorRemoval { None, UntilReload, ForGood };

const char* toString(ActorRemoval removal);
bool parseActorRemoval(const std::string& text, ActorRemoval& out);

// Where an event script left one of the game's actors (game.json `actors`). Scripts place actors in the area the
// party is in; an actor keeps its place when the party leaves.
struct ActorPlace {
	// Empty while the actor has no cell: shown or hidden before any move, with no talk point in the party's area.
	std::string area;
	Cell cell;
	Facing facing = Facing::South;
	bool visible = true;
	ActorVanish vanish = ActorVanish::None;
	// The one-shot animation playing and its milliseconds left and in all; empty once it has played.
	std::string animation = {};
	int animationMs = 0;
	int animationTotalMs = 0;
	ActorRemoval removed = ActorRemoval::None;
	// A script's `look`: an enemy's field figure or a host's look in place of game.json's; both empty for game.json's.
	std::string lookEnemy = {};
	std::string look = {};
	// Its wander or patrol (game.json `wander`, `patrol`): hundredths of a cell travelled toward the next step, and the pause
	// left after one.
	int wanderProgress = 0;
	int wanderPauseMs = 0;
	// Its patrol: the entry it is on and the cells of that entry's walk already taken.
	int patrolStep = 0;
	int patrolWalked = 0;
	// Its path (game.json `path`): field ticks into the loop.
	int pathTick = 0;
};

// Where a moving platform of the party's area is: microseconds along its run from `from` (0) to `to` (cells times its
// step time), the way it goes next (1 toward `to`, -1 toward `from`), the rest left at an end, and whether the party
// boarded it at the end it stands at (a triggered platform leaves only then).
struct PlatformPlace {
	std::int64_t positionUs = 0;
	int heading = 1;
	int pauseMs = 0;
	bool boarded = false;
	bool operator==(const PlatformPlace& o) const { return positionUs == o.positionUs && heading == o.heading && pauseMs == o.pauseMs && boarded == o.boarded; }
	bool operator!=(const PlatformPlace& o) const { return !(*this == o); }
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

// Who was in the lineup and in the reserve, each in order, when a script's `party save` took it.
struct PartySnapshot {
	std::vector<std::string> lineup;
	std::vector<std::string> reserve;
	bool operator==(const PartySnapshot& other) const { return lineup == other.lineup && reserve == other.reserve; }
	bool operator!=(const PartySnapshot& other) const { return !(*this == other); }
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
	// Party snapshots scripts saved, by name.
	std::map<std::string, PartySnapshot> partySnapshots;
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
	// Steps since the party entered the area, counted only up to one past the longest grace of its zones, the first step
	// that may roll.
	int areaSteps = 0;
	// Flag hooks with `once` that have fired, as "<area>/<hook>".
	std::set<std::string> onceHooks;
	// The track a script set for the area the party is in, until it leaves; empty plays the area's own.
	std::string areaMusic;
	// Story pictures a script's `picture show` put up and nothing has taken down, bottom first (world chunk `picture` lines).
	std::vector<std::string> pictures;
	// Where a script's `camera to` put the scene camera (`held`, on `cell`) or that `camera reset` sends it back to the
	// leader (not held), with the milliseconds of the move left and in all (world chunk line `scene_camera`, written while
	// held or moving). Entering an area resets it.
	struct SceneCamera {
		bool held = false;
		Cell cell;
		int ms = 0;
		int totalMs = 0;
		bool operator==(const SceneCamera& o) const { return held == o.held && cell == o.cell && ms == o.ms && totalMs == o.totalMs; }
		bool operator!=(const SceneCamera& o) const { return !(*this == o); }
	};
	SceneCamera sceneCamera;
	// The moving platforms of the party's area by id, set on entering it (world chunk `platform` lines, written only for an
	// area with platforms), and the one the party rides, empty for none (`riding_platform`).
	std::map<std::string, PlatformPlace> platforms;
	std::string ridingPlatform;
	// Steps taken since entering an area with `field_status`, which its poison and paralysis rules count (world chunk line
	// `field_steps`, written when above 0).
	int fieldSteps = 0;
	// The leader's earlier cells in the area, newest first, where the followers walk (Session::followers); empty puts them
	// all on the leader's cell (world chunk line `trail`, written when not empty).
	std::vector<Cell> trail;
	// Under machine_step_heal_fallen `heal`: hit points fallen machine members gained by steps while down, by character id,
	// added when something stands them up (party chunk `machine_bank` lines, written only while above 0).
	std::map<std::string, int> machineBank;
	// The area the party was in before this one, for area names that depend on it (world chunk line `came_from`, written
	// only while the current area has `name` variants).
	std::string cameFrom;
	// Under game.json's `step_timing` `exact` (R108), the time carried toward the leader's next step and the belt's next
	// carry in microseconds, and the update time not yet a whole tick in milliseconds; the session keeps them while it
	// runs and puts them here for a save (world chunk lines `step_carry`, `conveyor_carry`, `tick_carry`, written only
	// when not 0), so a load goes on with the same remainder.
	std::int64_t stepCarryUs = 0;
	std::int64_t conveyorCarryUs = 0;
	int tickCarryMs = 0;
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
