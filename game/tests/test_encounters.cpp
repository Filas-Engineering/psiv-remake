#include "test.h"

#include "game/encounters.h"
#include "game/runner.h"

#include <sstream>

using namespace game;

namespace {

// A walled room with one zone: walls split the room so line of sight can be blocked.
const char* kRoom = R"({
	"id": "room",
	"map": [
		"##########",
		"#........#",
		"#....#...#",
		"#....#...#",
		"#........#",
		"##########"
	],
	"markers": {"door": [1, 1]},
	"zones": [{"id": "z", "from": [1, 1], "to": [8, 4], "table": [{"encounter": "training"}],
		"visible": {"spawns": [[7, 2]], "cap": 1, "detection_radius": 4, "leash_radius": 1, "chase_ms": 1000}}]
})";

Area room() {
	rules::Result<Area> a = parseArea("room.json", kRoom);
	CHECK(a.ok());
	return *a.value;
}

VisibleEnemy enemyAt(Cell at, Facing facing, EnemyState state) {
	VisibleEnemy e;
	e.id = 1;
	e.zone = "z";
	e.encounter = "training";
	e.spawn = e.position = at;
	e.facing = facing;
	e.state = state;
	return e;
}

std::optional<EncounterStart> tickUntilContact(World& w, const Area& a, int ticks) {
	for (int i = 0; i < ticks; ++i) {
		if (std::optional<EncounterStart> c = tickVisibleEnemies(w, a, 50, 250)) {
			return c;
		}
	}
	return std::nullopt;
}

std::string play(std::uint64_t seed, std::optional<EncounterMode> mode, World* out) {
	SessionOptions options;
	options.seed = seed;
	options.encounterMode = mode;
	Session s(test::data(), options);
	std::ostringstream transcript;
	RunnerOptions r;
	r.autoBattle = true;
	Runner runner(s, transcript, r);
	runner.runScript("new\nwalk guide_front\nface north\ntalk\nchoose 0\nwalk 13 4\nwalk 18 4\nwalk 1 4\nwalk 18 2\n");
	CHECK_EQ(runner.errors(), 0);
	if (out) {
		*out = s.world();
	}
	return transcript.str();
}

}

TEST(rearArcIsTheBack90Degrees) {
	CHECK(inRearArc({5, 5}, Facing::East, {4, 5}));
	CHECK(inRearArc({5, 5}, Facing::East, {3, 6}));
	CHECK(inRearArc({5, 5}, Facing::East, {4, 6}));
	CHECK(!inRearArc({5, 5}, Facing::East, {4, 7}));
	CHECK(!inRearArc({5, 5}, Facing::East, {6, 5}));
	CHECK(!inRearArc({5, 5}, Facing::East, {5, 4}));
	CHECK(inRearArc({5, 5}, Facing::North, {5, 6}));
	CHECK(!inRearArc({5, 5}, Facing::North, {5, 5}));
}

TEST(leaderTouchingAnEnemyFromBehindIsPartyFirst) {
	World w;
	w.position = {3, 4};
	w.facing = Facing::East;
	w.enemies.push_back(enemyAt({4, 4}, Facing::East, EnemyState::Idle));
	std::optional<EncounterStart> c = leaderBump(w, {4, 4});
	CHECK(c && c->opening == rules::Opening::PartyFirst);

	w.enemies[0].facing = Facing::West;
	c = leaderBump(w, {4, 4});
	CHECK(c && c->opening == rules::Opening::Normal);

	w.enemies[0].facing = Facing::East;
	w.enemies[0].state = EnemyState::Chasing;
	c = leaderBump(w, {4, 4});
	CHECK(c && c->opening == rules::Opening::Normal);

	w.graceMs = 100;
	CHECK(!leaderBump(w, {4, 4}));
}

TEST(chasingEnemyCatchingTheLeaderFromBehindIsEnemyFirst) {
	const Area a = room();
	World w;
	w.position = {2, 4};
	w.facing = Facing::West;
	w.enemies.push_back(enemyAt({4, 4}, Facing::West, EnemyState::Chasing));
	std::optional<EncounterStart> c = tickUntilContact(w, a, 20);
	CHECK(c && c->opening == rules::Opening::EnemyFirst);
	CHECK(w.enemies[0].position == Cell({3, 4}));

	w.facing = Facing::East;
	w.enemies[0] = enemyAt({4, 4}, Facing::West, EnemyState::Chasing);
	c = tickUntilContact(w, a, 20);
	CHECK(c && c->opening == rules::Opening::Normal);
}

TEST(detectionNeedsRangeAndLineOfSight) {
	const Area a = room();
	World w;
	w.position = {3, 3};
	w.enemies.push_back(enemyAt({7, 3}, Facing::West, EnemyState::Idle));
	tickVisibleEnemies(w, a, 50, 250);
	CHECK(w.enemies[0].state == EnemyState::Idle);

	w.position = {6, 1};
	tickVisibleEnemies(w, a, 50, 250);
	CHECK(w.enemies[0].state == EnemyState::Chasing);
}

TEST(chaseEndsAfterChaseTimeAndReturnsToLeash) {
	const Area a = room();
	World w;
	w.position = {1, 4};
	w.enemies.push_back(enemyAt({7, 2}, Facing::West, EnemyState::Chasing));
	w.enemies[0].position = {7, 4};
	for (int i = 0; i < 21; ++i) {
		tickVisibleEnemies(w, a, 50, 250);
	}
	CHECK(w.enemies[0].state == EnemyState::Returning);
	w.position = {1, 1};
	w.graceMs = 60000;
	for (int i = 0; i < 200 && w.enemies[0].state != EnemyState::Idle; ++i) {
		tickVisibleEnemies(w, a, 50, 250);
	}
	CHECK(w.enemies[0].state == EnemyState::Idle);
}

TEST(graceStopsChasesAndContact) {
	const Area a = room();
	World w;
	w.position = {2, 4};
	w.graceMs = 3000;
	w.enemies.push_back(enemyAt({3, 4}, Facing::West, EnemyState::Chasing));
	CHECK(!tickUntilContact(w, a, 20));
	CHECK(w.enemies[0].state != EnemyState::Chasing);
}

TEST(visibleEnemiesRespawnOnReentry) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "reed_path", "east_gate", Facing::East);
	s.world().graceMs = 60000;
	s.walk(Facing::East);
	CHECK_EQ(s.world().area, std::string("bell_cove"));
	s.walk(Facing::West);
	CHECK_EQ(s.world().area, std::string("reed_path"));
	CHECK_EQ(s.world().enemies.size(), 4u);
	s.world().enemies.clear();
	s.walk(Facing::East);
	s.walk(Facing::West);
	CHECK_EQ(s.world().enemies.size(), 4u);
}

TEST(encountersAreReproducibleFromTheSeed) {
	World a;
	World b;
	World c;
	const std::string first = play(11, EncounterMode::Visible, &a);
	const std::string second = play(11, EncounterMode::Visible, &b);
	const std::string other = play(12, EncounterMode::Visible, &c);
	std::string diff;
	CHECK(sameWorld(a, b, &diff));
	CHECK_EQ(first, second);
	CHECK(first != other);
	CHECK(first.find("Battle!") != std::string::npos);
}

TEST(randomModeIsReproducibleAndCountsSteps) {
	World a;
	World b;
	const std::string first = play(5, EncounterMode::Random, &a);
	const std::string second = play(5, EncounterMode::Random, &b);
	CHECK_EQ(first, second);
	CHECK(sameWorld(a, b));
	CHECK(a.enemies.empty());
	CHECK(first.find("Battle!") != std::string::npos);
}

TEST(randomCounterWaitsOutGraceAndTowns) {
	const Area& reeds = *test::data().area("reed_path");
	const Area& town = *test::data().area("tidewater");
	World w;
	w.encounterStream.restore(3);
	CHECK(!randomStep(w, town, {5, 5}));
	CHECK_EQ(w.randomCounter, 0);
	w.graceMs = 10;
	CHECK(!randomStep(w, reeds, {3, 3}));
	CHECK_EQ(w.randomCounter, 0);
	w.graceMs = 0;
	int steps = 0;
	std::optional<EncounterStart> start;
	while (!start && steps < 100) {
		start = randomStep(w, reeds, {3, 3});
		++steps;
	}
	CHECK(start.has_value());
	CHECK(steps >= 6 && steps <= 12);
	CHECK_EQ(start->encounter, std::string("training"));
}
