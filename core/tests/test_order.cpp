#include "fixtures.h"
#include "test.h"

#include <set>

using namespace rules;
using namespace fixtures;

TEST(sameSeedAndCommandsGiveTheSameBattle) {
	for (std::uint64_t seed : {1u, 7u, 42u}) {
		ComboLog logA;
		ComboLog logB;
		Battle a = makeBattle(data(), {"brass_warden", "bog_wisp"}, seed, &logA);
		Battle b = makeBattle(data(), {"brass_warden", "bog_wisp"}, seed, &logB);
		const std::string runA = serializeEvents(playToEnd(a, 40));
		const std::string runB = serializeEvents(playToEnd(b, 40));
		CHECK(!runA.empty());
		CHECK_EQ(runA, runB);
		CHECK(a.outcome() == b.outcome());
	}
}

TEST(seedChangesTurnOrder) {
	std::set<std::vector<int>> orders;
	for (std::uint64_t seed = 1; seed <= 30; ++seed) {
		Battle battle = makeBattle(data(), {"gel_mite", "rust_hound"}, seed);
		othersDefend(battle);
		battle.resolveRound();
		CHECK_EQ(battle.lastTurnOrder().size(), std::size_t(7));
		orders.insert(battle.lastTurnOrder());
	}
	CHECK(orders.size() > 5);
}

TEST(agilityOutweighsTheRandomFactor) {
	Database db = data();
	db.characters["scout"].base.agility = 300;
	db.enemies["gel_mite"].stats.agility = 1;
	for (std::uint64_t seed = 1; seed <= 30; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite"}, seed);
		othersDefend(battle);
		battle.resolveRound();
		CHECK_EQ(battle.lastTurnOrder().front(), kScout);
	}
}

TEST(defeatedCombatantsGetNoTurn) {
	Battle battle = makeBattle(data(), {"gel_mite", "gel_mite"}, 5);
	battle.combatant(kMedic).hp = 0;
	battle.combatant(kFirstEnemy).hp = 0;
	othersDefend(battle);
	battle.resolveRound();
	for (int actor : battle.lastTurnOrder()) {
		CHECK(actor != kMedic && actor != kFirstEnemy);
	}
	CHECK_EQ(battle.lastTurnOrder().size(), std::size_t(5));
}
