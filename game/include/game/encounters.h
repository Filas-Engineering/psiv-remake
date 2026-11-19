#pragma once

#include "game/area.h"
#include "game/world.h"

#include "rules/types.h"

#include <optional>
#include <string>

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
// enemy reaches the leader. `leaderStepMs` is the leader's time per cell, which enemy speeds scale.
std::optional<EncounterStart> tickVisibleEnemies(World& world, const Area& area, int tickMs, int leaderStepMs);

// The leader finished a step onto `cell` in random mode. Returns a battle when the counter passes the
// threshold (ENC-040). Does nothing during the grace period or outside a zone (ENC-042).
std::optional<EncounterStart> randomStep(World& world, const Area& area, Cell cell);

// Draws a new random threshold for the zone and clears the counter.
void resetRandomCounter(World& world, const Zone& zone);

}
