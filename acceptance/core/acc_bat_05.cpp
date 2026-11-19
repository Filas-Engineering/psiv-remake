#include "support.h"

using namespace rules;

namespace {

std::string text(const std::vector<int>& order) {
	std::string out;
	for (int actor : order) {
		out += (out.empty() ? "" : ",") + std::to_string(actor);
	}
	return out;
}

}

ACC_CHECK(acc_bat_05, "ACC-BAT-05", "BAT-030, BAT-031") {
	const Database& db = support::data(t);
	const std::vector<std::string> enemies = {"gel_mite", "rust_hound", "bog_wisp"};
	const int rounds = 100000;
	int ties = 0;
	for (int seed = 1; seed <= rounds; ++seed) {
		Battle battle = support::create(db, support::setup(db, enemies, static_cast<std::uint64_t>(seed)));
		support::othersDefend(battle);
		int roundTies = 0;
		const std::vector<int> expected = support::expectedFirstRoundOrder(battle, static_cast<std::uint64_t>(seed), &roundTies);
		battle.resolveRound();
		t.expect(battle.lastTurnOrder() == expected, "seed " + std::to_string(seed) + ": order " + text(battle.lastTurnOrder()) + ", expected " + text(expected));
		ties += roundTies;
	}
	t.expect(ties > 0, "no initiative ties occurred, so the tie rule was not exercised");

	// An agility change during a round does not reorder that round; it applies from the next one.
	int reorderChecks = 0;
	for (std::uint64_t seed = 1; seed <= 2000; ++seed) {
		Battle battle = support::create(db, support::setup(db, enemies, seed));
		battle.setCommand(2, Command::ability("overclock", 2));
		support::othersDefend(battle);
		Rng replay(seed);
		const std::vector<int> before = support::expectedOrder(battle, replay);
		const std::vector<Event> events = battle.resolveRound();
		t.expect(support::actingOrder(events) == before, "seed " + std::to_string(seed) + ": the round's actions did not follow the order fixed before the agility change");
		t.expect(battle.combatant(2).effective(Stat::Agility) > battle.combatant(2).stats.agility, "setup: overclock did not raise agility");

		support::othersDefend(battle);
		const std::vector<int> next = support::expectedOrder(battle, replay);
		battle.resolveRound();
		t.expect(battle.lastTurnOrder() == next, "seed " + std::to_string(seed) + ": the next round did not use the changed agility");
		++reorderChecks;
	}

	t.note(std::to_string(rounds) + " seeded rounds match agility plus the core initiative roll exactly (" + std::to_string(ties) +
		" ties, resolved party first then lower index); " + std::to_string(reorderChecks) + " rounds show a mid-round agility change applies next round only");
}
