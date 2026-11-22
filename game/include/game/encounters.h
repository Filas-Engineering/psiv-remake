#pragma once

#include "game/area.h"
#include "game/world.h"

#include "rules/character.h"
#include "rules/database.h"
#include "rules/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace game {

// A battle the field wants to start.
struct EncounterStart {
	std::string encounter;
	rules::Opening opening = rules::Opening::Normal;
	// The visible enemy that made contact, or 0 for a random battle.
	int enemyId = 0;
};

// Whether `point` lies inside the rear 90-degree arc of a body at `body` facing `facing` (ENC-020, ENC-021).
bool inRearArc(Cell body, Facing facing, Cell point);

// Picks a group from the zone's table by weight, from the encounter stream (ENC-004).
std::string drawEncounter(rules::Rng& stream, const Zone& zone);

// Clears the area's visible enemies and spawns each visible zone up to its cap (ENC-010, ENC-014).
void spawnVisibleEnemies(World& world, const Area& area);

// The leader tried to step into `target`. A visible enemy standing there makes contact (ENC-011).
std::optional<EncounterStart> leaderBump(World& world, Cell target);

// Advances every visible enemy by one tick: wander, detect, chase, leash. Returns a contact when a chasing
// enemy reaches the leader. `leaderStepMs` is the leader's walking time per cell, which enemy speeds scale. With
// `leaderUnseen` (the party rides a vehicle) no enemy notices or chases the leader, as during the grace period. With
// `leaderStepUs` above 0 (game.json `step_timing` `exact`) the cell's cost is that time to the hundredth of a millisecond
// instead of `leaderStepMs`. No enemy steps onto a cell of `blocked` (wandering actors).
std::optional<EncounterStart> tickVisibleEnemies(World& world, const Area& area, int tickMs, int leaderStepMs, bool leaderUnseen = false,
	std::int64_t leaderStepUs = 0, const std::vector<Cell>& blocked = {});

// The leader finished a step onto `cell` in random mode. Returns a battle when the counter passes the
// threshold (ENC-040), with its opening rolled from the party's highest agility against the encounter's awareness
// (ENC-022). Does nothing during the grace period or outside a zone (ENC-042).
std::optional<EncounterStart> randomStep(World& world, const Area& area, Cell cell, const rules::Database& db, const std::vector<rules::PartyMember>& party);

// The leader finished a step aboard a vehicle onto `cell` (ENC-007): in either encounter mode the random counter of the
// zone runs, and a battle draws its group from the vehicle's own table. The opening rolls the vehicle's agility, since
// it alone fights; without `rollOpening` it is normal and nothing is drawn for it.
std::optional<EncounterStart> vehicleStep(World& world, const Area& area, Cell cell, const std::vector<TableEntry>& table, const rules::Database& db, int agility,
	bool rollOpening = true);

// Draws a new random threshold for the zone and clears the counter.
void resetRandomCounter(World& world, const Zone& zone);

}
