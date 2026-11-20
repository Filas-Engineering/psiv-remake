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
enum class JobState { Available, Accepted, Completed };

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
	// Time left in the post-battle grace period (ENC-015, ENC-042).
	int graceMs = 0;
	// Random mode distance counter and the threshold it is racing (ENC-040).
	int randomCounter = 0;
	int randomThreshold = 0;
	rules::Rng encounterStream{0};

	std::map<std::string, JobState> jobs;
	// Encyclopedia entries, kept once unlocked even if their flag is cleared later (TXT-041).
	std::set<std::string> unlocked;

	std::int64_t playTimeMs = 0;
	Settings settings;
	std::vector<UnknownChunk> unknownChunks;

	rules::PartyMember* member(const std::string& characterId);
	const rules::PartyMember* member(const std::string& characterId) const;
	bool flag(const std::string& name) const;
};

// Field-by-field comparison of everything a save holds. On a mismatch, `diff` names the first field that differs.
bool sameWorld(const World& a, const World& b, std::string* diff = nullptr);

}
