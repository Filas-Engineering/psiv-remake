#include "game/encounters.h"

#include "rules/formulas.h"
#include "rules/progression.h"
#include "rules/setup.h"

#include <algorithm>
#include <cstdlib>

namespace game {

namespace {

int distance2(Cell a, Cell b) {
	const int dx = a.x - b.x;
	const int dy = a.y - b.y;
	return dx * dx + dy * dy;
}

int manhattan(Cell a, Cell b) {
	return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

bool lineOfSight(const Area& area, Cell from, Cell to) {
	int x = from.x;
	int y = from.y;
	const int dx = std::abs(to.x - x);
	const int dy = -std::abs(to.y - y);
	const int sx = x < to.x ? 1 : -1;
	const int sy = y < to.y ? 1 : -1;
	int err = dx + dy;
	while (x != to.x || y != to.y) {
		const int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y += sy;
		}
		if ((x != to.x || y != to.y) && area.rows[y][x] == '#') {
			return false;
		}
	}
	return true;
}

bool occupied(const World& world, Cell cell, int exceptId) {
	for (const VisibleEnemy& e : world.enemies) {
		if (e.id != exceptId && e.position == cell) {
			return true;
		}
	}
	return false;
}

bool enemyCanStand(const World& world, const Area& area, Cell cell, int id) {
	return area.walkable(cell) && !area.exitAt(cell) && cell != world.position && !occupied(world, cell, id);
}

const Facing kFacings[] = {Facing::North, Facing::East, Facing::South, Facing::West};

// The direction that brings `from` closest to `goal`, preferring the larger axis. Returns false when every
// improving cell is blocked.
bool stepToward(Cell from, Cell goal, Facing& out) {
	const int dx = goal.x - from.x;
	const int dy = goal.y - from.y;
	if (dx == 0 && dy == 0) {
		return false;
	}
	if (std::abs(dx) >= std::abs(dy)) {
		out = dx > 0 ? Facing::East : Facing::West;
	} else {
		out = dy > 0 ? Facing::South : Facing::North;
	}
	return true;
}

std::vector<Facing> towardOrder(Cell from, Cell goal) {
	std::vector<Facing> order(std::begin(kFacings), std::end(kFacings));
	std::stable_sort(order.begin(), order.end(), [&](Facing a, Facing b) {
		return manhattan(stepFrom(from, a), goal) < manhattan(stepFrom(from, b), goal);
	});
	Facing first;
	if (stepToward(from, goal, first)) {
		auto it = std::find(order.begin(), order.end(), first);
		std::rotate(order.begin(), it, it + 1);
	}
	return order;
}

}

bool inRearArc(Cell body, Facing facing, Cell point) {
	const Cell f = facingVector(facing);
	const int dx = point.x - body.x;
	const int dy = point.y - body.y;
	const int dot = -(dx * f.x + dy * f.y);
	const int cross = std::abs(dx * f.y - dy * f.x);
	return dot > 0 && cross <= dot;
}

std::string drawEncounter(rules::Rng& stream, const Zone& zone) {
	int total = 0;
	for (const TableEntry& t : zone.table) {
		total += t.weight;
	}
	int roll = stream.range(1, total);
	for (const TableEntry& t : zone.table) {
		roll -= t.weight;
		if (roll <= 0) {
			return t.encounter;
		}
	}
	return zone.table.back().encounter;
}

void spawnVisibleEnemies(World& world, const Area& area) {
	world.enemies.clear();
	for (const Zone& zone : area.zones) {
		std::vector<Cell> free = zone.visible.spawns;
		for (int n = 0; n < zone.visible.cap && !free.empty(); ++n) {
			const int pick = world.encounterStream.range(0, static_cast<int>(free.size()) - 1);
			const Cell at = free[pick];
			free.erase(free.begin() + pick);
			if (at == world.position) {
				continue;
			}
			VisibleEnemy e;
			e.id = world.nextEnemyId++;
			e.zone = zone.id;
			e.encounter = drawEncounter(world.encounterStream, zone);
			e.spawn = at;
			e.position = at;
			e.facing = kFacings[world.encounterStream.range(0, 3)];
			world.enemies.push_back(e);
		}
	}
}

std::optional<EncounterStart> leaderBump(World& world, Cell target) {
	if (world.graceMs > 0) {
		return std::nullopt;
	}
	for (const VisibleEnemy& e : world.enemies) {
		if (e.position == target) {
			EncounterStart start;
			start.encounter = e.encounter;
			start.enemyId = e.id;
			start.opening = rules::openingFromContact(inRearArc(e.position, e.facing, world.position), e.state == EnemyState::Chasing,
				inRearArc(world.position, world.facing, e.position));
			return start;
		}
	}
	return std::nullopt;
}

std::optional<EncounterStart> tickVisibleEnemies(World& world, const Area& area, int tickMs, int leaderStepMs) {
	const int cellCost = leaderStepMs * 100;
	for (VisibleEnemy& e : world.enemies) {
		const Zone* zone = area.zone(e.zone);
		if (!zone) {
			continue;
		}
		const VisibleRules& r = zone->visible;
		e.stateMs += tickMs;
		if (world.graceMs > 0 && e.state == EnemyState::Chasing) {
			e.state = EnemyState::Returning;
			e.stateMs = 0;
		}
		if (e.state == EnemyState::Idle && world.graceMs == 0 && distance2(e.position, world.position) <= r.detectionRadius * r.detectionRadius &&
			lineOfSight(area, e.position, world.position)) {
			e.state = EnemyState::Chasing;
			e.stateMs = 0;
			e.moveProgress = 0;
		}
		if (e.state == EnemyState::Chasing && e.stateMs >= r.chaseMs) {
			e.state = EnemyState::Returning;
			e.stateMs = 0;
		}
		const int speed = e.state == EnemyState::Chasing ? r.chaseSpeedPercent : r.wanderSpeedPercent;
		e.moveProgress += tickMs * speed;
		if (e.moveProgress < cellCost) {
			continue;
		}
		e.moveProgress -= cellCost;

		if (e.state == EnemyState::Chasing) {
			for (Facing f : towardOrder(e.position, world.position)) {
				const Cell next = stepFrom(e.position, f);
				if (next == world.position) {
					e.facing = f;
					EncounterStart start;
					start.encounter = e.encounter;
					start.enemyId = e.id;
							start.opening = rules::openingFromContact(inRearArc(e.position, e.facing, world.position), true,
						inRearArc(world.position, world.facing, e.position));
					return start;
				}
				if (manhattan(next, world.position) < manhattan(e.position, world.position) && enemyCanStand(world, area, next, e.id)) {
					e.facing = f;
					e.position = next;
					break;
				}
			}
		} else if (e.state == EnemyState::Returning) {
			if (distance2(e.position, e.spawn) <= r.leashRadius * r.leashRadius) {
				e.state = EnemyState::Idle;
				e.stateMs = 0;
				continue;
			}
			for (Facing f : towardOrder(e.position, e.spawn)) {
				const Cell next = stepFrom(e.position, f);
				if (manhattan(next, e.spawn) < manhattan(e.position, e.spawn) && enemyCanStand(world, area, next, e.id)) {
					e.facing = f;
					e.position = next;
					break;
				}
			}
		} else if (world.encounterStream.range(1, 100) <= r.wanderPercent) {
			const Facing f = kFacings[world.encounterStream.range(0, 3)];
			const Cell next = stepFrom(e.position, f);
			e.facing = f;
			if (distance2(next, e.spawn) <= r.leashRadius * r.leashRadius && enemyCanStand(world, area, next, e.id)) {
				e.position = next;
			}
		}
	}
	return std::nullopt;
}

void resetRandomCounter(World& world, const Zone& zone) {
	world.randomCounter = 0;
	world.randomThreshold = world.encounterStream.range(zone.random.minSteps, zone.random.maxSteps) * zone.random.stepDistance;
}

std::optional<EncounterStart> randomStep(World& world, const Area& area, Cell cell, const rules::Database& db, const std::vector<rules::PartyMember>& party) {
	if (world.graceMs > 0 || area.kind == AreaKind::Town) {
		return std::nullopt;
	}
	const Zone* zone = area.zoneAt(cell);
	if (!zone) {
		return std::nullopt;
	}
	if (world.randomThreshold <= 0) {
		resetRandomCounter(world, *zone);
	}
	world.randomCounter += zone->random.stepDistance;
	if (world.randomCounter < world.randomThreshold) {
		return std::nullopt;
	}
	EncounterStart start;
	start.encounter = drawEncounter(world.encounterStream, *zone);
	int partyAgility = 0;
	for (const rules::PartyMember& member : party) {
		if (member.hp > 0) {
			partyAgility = std::max(partyAgility, rules::memberStats(db, member).agility);
		}
	}
	start.opening = rules::openingFromRoll(world.encounterStream.range(0, rules::formulas::kChanceRollMax), partyAgility, db.encounter(start.encounter)->awareness);
	resetRandomCounter(world, *zone);
	return start;
}

}
