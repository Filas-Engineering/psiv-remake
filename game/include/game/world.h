#pragma once

#include "rules/character.h"
#include "rules/combo.h"
#include "rules/macro.h"
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

// SplitMix64, kept in the game so its state can be saved (ENC-006).
struct EncounterStream {
	std::uint64_t state = 0;

	std::uint64_t next();
	// Inclusive on both ends.
	int range(int low, int high);
};

enum class TextSpeed { Slow, Normal, Fast, Instant };
enum class BattleSpeed { X1, X1_5, X2, X3 };

struct Settings {
	// Saved per game (SAV-005).
	std::string difficulty = "original";
	rules::RewardMultiplier rewardMultiplier = rules::RewardMultiplier::X1;
	// Global profile, not saved per game (SAV-005).
	TextSpeed textSpeed = TextSpeed::Normal;
	BattleSpeed battleSpeed = BattleSpeed::X1;
	bool showShippedNames = false;
};

const char* toString(TextSpeed speed);
const char* toString(BattleSpeed speed);
bool parseTextSpeed(const std::string& text, TextSpeed& out);
bool parseBattleSpeed(const std::string& text, BattleSpeed& out);

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
	rules::Inventory items;
	// Unworn equipment, by id.
	std::map<std::string, int> equipmentBag;
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
	// Time left in the post-battle grace period (ENC-015, ENC-042).
	int graceMs = 0;
	// Random mode distance counter and the threshold it is racing (ENC-040).
	int randomCounter = 0;
	int randomThreshold = 0;
	EncounterStream encounterStream;

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
