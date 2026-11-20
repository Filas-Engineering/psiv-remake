#include "support.h"

#include "rules/formulas.h"

#include <algorithm>
#include <cstdlib>

using namespace rules;

namespace {

constexpr int kAttempts = 10000;

struct Configuration {
	std::vector<std::string> enemies;
	int level;
	// Enemies, from the first, put to sleep before the attempt.
	int asleep;
	// The setup's escape rating; -1 leaves it to the highest enemy agility at battle start.
	int rating;
};

int highestAgility(const Battle& battle, bool party) {
	int highest = 0;
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		if (battle.isParty(i) == party && battle.combatant(i).alive()) {
			highest = std::max(highest, battle.combatant(i).effective(Stat::Agility));
		}
	}
	return highest;
}

}

ACC_CHECK(acc_enc_04, "ACC-ENC-04", "ENC-030, ENC-031") {
	const Database& db = support::data(t);
	const std::vector<Configuration> configurations = {{{"gel_mite", "gel_mite"}, 1, 0, -1}, {{"rust_hound", "bog_wisp"}, 1, 0, -1}, {{"gel_mite"}, 12, 0, -1},
		{{"brass_warden", "bog_wisp"}, 1, 0, -1}, {{"brass_warden", "bog_wisp"}, 1, 1, -1}, {{"rust_hound", "bog_wisp"}, 1, 2, -1},
		{{"rust_hound", "bog_wisp"}, 1, 0, 0}, {{"gel_mite", "gel_mite"}, 1, 0, 30}};
	std::string summary;
	for (const Configuration& c : configurations) {
		int successes = 0;
		int mismatches = 0;
		double chance = 0;
		// Battle seeds come from a seeded stream, as BAT-134 draws them from the encounter stream.
		Rng seeds(static_cast<std::uint64_t>(c.level) * 1000 + c.enemies.size() * 10 + c.asleep + static_cast<std::uint64_t>(c.rating + 1) * 100000);
		for (int attempt = 0; attempt < kAttempts; ++attempt) {
			const std::uint64_t seed = seeds.next();
			BattleSetup s = support::setup(db, c.enemies, seed, c.level);
			s.flags.escapeAllowed = true;
			s.escapeRating = c.rating;
			Battle battle = support::create(db, s);
			for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
				support::sturdy(battle, i);
			}
			for (int i = 0; i < c.asleep; ++i) {
				battle.combatant(battle.partyCount() + i).statuses.push_back({"sleep", 3});
			}
			const bool enemyCanAct = c.asleep < static_cast<int>(c.enemies.size());
			const int party = highestAgility(battle, true);
			const int enemy = c.rating >= 0 ? c.rating : highestAgility(battle, false);
			t.expect(battle.setup().escapeRating == enemy, "the battle's escape rating is not the setup's, or without one the highest enemy agility at battle start");
			// Rolls of 0 to 63 that score above the floor.
			const int lowestRoll = std::max(0, formulas::kEscapeFloor / 2 + 1 - (party - enemy));
			chance = enemyCanAct ? 100.0 * std::max(0, formulas::kChanceRollMax + 1 - lowestRoll) / (formulas::kChanceRollMax + 1) : 100.0;
			battle.setCommand(0, Command::escape());
			support::othersDefend(battle);
			// The escape is the first draw after initiative, and only when an enemy can act.
			Rng rng(seed);
			support::expectedOrder(battle, rng);
			const bool predicted = !enemyCanAct || formulas::chanceScore(rng.range(0, formulas::kChanceRollMax), party, enemy) > formulas::kEscapeFloor;
			const std::vector<Event> events = battle.resolveRound();
			bool escaped = false;
			bool afterEscape = false;
			for (const Event& e : events) {
				if (escaped && (e.type == EventType::ActionStarted || e.type == EventType::ActionCancelled)) {
					afterEscape = true;
				}
				escaped = escaped || (e.type == EventType::EscapeAttempted && e.succeeded);
			}
			mismatches += escaped == predicted ? 0 : 1;
			if (escaped) {
				++successes;
				t.expect(battle.outcome() == Outcome::Escaped, "a successful escape did not end the battle as Escaped");
				t.expect(!afterEscape, "actions still executed after a successful escape");
				const Rewards rewards = battle.rewards();
				t.expect(rewards.experience == 0 && rewards.currency == 0, "a successful escape granted rewards");
			} else {
				t.expect(battle.outcome() == Outcome::Ongoing, "a failed escape did not let the round continue");
			}
		}
		const std::string name = std::to_string(c.enemies.size()) + " enemies (" + std::to_string(c.asleep) + " asleep), level " + std::to_string(c.level);
		t.expect(mismatches == 0, name + ": " + std::to_string(mismatches) + " attempts differ from the replayed escape roll");
		const double rate = 100.0 * successes / kAttempts;
		t.expect(std::abs(rate - chance) <= 1.0, name + ": escape rate " + std::to_string(rate) + "%, rule gives " + std::to_string(chance) + "%");
		summary += (summary.empty() ? "" : ", ") + std::to_string(static_cast<int>(chance + 0.5)) + "%";
	}

	// Disabled escape never succeeds: the command is refused, and a macro step falls back. An escape rating of 240 or
	// more disables it the same way.
	for (int rating : {-1, 240}) {
		BattleSetup s = support::setup(db, {}, 1);
		useEncounter(s, db, rating < 0 ? "warden" : "training");
		if (rating >= 0) {
			s.escapeRating = rating;
		}
		Battle battle = support::create(db, s);
		t.expect(!battle.escapeAllowed(), rating < 0 ? "the boss encounter does not disable escape" : "an escape rating of 240 does not disable escape");
		t.expect(!battle.setCommand(0, Command::escape()).empty(), "an escape command was accepted in a battle that disables escape");
		Macro run;
		run.name = "acc_run";
		run.steps = {{"fighter", CommandKind::Escape, "", {}}};
		const std::vector<MacroStepResult> steps = battle.applyMacro(run);
		t.expect(steps[0].issue == MacroIssue::EscapeNotAllowed && battle.command(0).kind != CommandKind::Escape, "a macro escape step ran in a battle that disables escape");
		for (int round = 0; round < 20 && battle.outcome() == Outcome::Ongoing; ++round) {
			battle.setCommand(0, Command::escape());
			for (const Event& e : battle.resolveRound()) {
				t.expect(e.type != EventType::EscapeAttempted, "an escape was attempted in a battle that disables escape");
			}
		}
	}

	t.note(std::to_string(kAttempts) + " seeded attempts in each of " + std::to_string(configurations.size()) + " configurations (two with a set escape rating) match the replayed escape roll against the escape rating, and the rate is within 1 point of the rule (" + summary +
		"); with every enemy asleep escape always succeeds; success ends the battle as Escaped, drops later actions and grants no rewards; escape disabled by the encounter or by a rating of 240 is refused");
}
