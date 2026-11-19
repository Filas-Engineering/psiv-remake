#include "support.h"

#include "rules/formulas.h"

#include <cstdlib>

using namespace rules;

namespace {

constexpr int kAttempts = 10000;

struct Configuration {
	std::vector<std::string> enemies;
	int level;
};

int averageAgility(const Battle& battle, bool party) {
	int total = 0;
	int count = 0;
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		if (battle.isParty(i) == party && battle.combatant(i).alive()) {
			total += battle.combatant(i).effective(Stat::Agility);
			++count;
		}
	}
	return total / count;
}

}

ACC_CHECK(acc_enc_04, "ACC-ENC-04", "ENC-030, ENC-031") {
	const Database& db = support::data(t);
	const std::vector<Configuration> configurations = {{{"gel_mite", "gel_mite"}, 1}, {{"rust_hound", "bog_wisp"}, 1}, {{"gel_mite"}, 12}, {{"brass_warden", "bog_wisp"}, 1}};
	std::string summary;
	for (const Configuration& c : configurations) {
		int successes = 0;
		int expected = 0;
		// Battle seeds come from a seeded stream, as BAT-134 draws them from the encounter stream.
		Rng seeds(static_cast<std::uint64_t>(c.level) * 1000 + c.enemies.size());
		for (int attempt = 0; attempt < kAttempts; ++attempt) {
			BattleSetup s = support::setup(db, c.enemies, seeds.next(), c.level);
			s.flags.escapeAllowed = true;
			Battle battle = support::create(db, s);
			for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
				support::sturdy(battle, i);
			}
			expected = formulas::escapeChance(averageAgility(battle, true), averageAgility(battle, false), db.battleRules.escape);
			battle.setCommand(0, Command::escape());
			support::othersDefend(battle);
			const std::vector<Event> events = battle.resolveRound();
			bool escaped = false;
			bool afterEscape = false;
			for (const Event& e : events) {
				if (escaped && (e.type == EventType::ActionStarted || e.type == EventType::ActionFailed || e.type == EventType::ActionPrevented)) {
					afterEscape = true;
				}
				escaped = escaped || (e.type == EventType::EscapeAttempted && e.succeeded);
			}
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
		const double rate = 100.0 * successes / kAttempts;
		t.expect(std::abs(rate - expected) <= 1.0, std::to_string(c.enemies.size()) + " enemies, level " + std::to_string(c.level) + ": escape rate " + std::to_string(rate) + "%, rule gives " + std::to_string(expected) + "%");
		summary += (summary.empty() ? "" : ", ") + std::to_string(expected) + "%";
	}

	// Disabled escape never succeeds: the command is refused, and a macro step falls back.
	{
		BattleSetup s = support::setup(db, {}, 1);
		useEncounter(s, db, "warden");
		Battle battle = support::create(db, s);
		t.expect(!battle.escapeAllowed(), "the boss encounter does not disable escape");
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

	t.note(std::to_string(kAttempts) + " seeded attempts in each of 4 configurations match the core escape rule within 1 point (" + summary +
		"); success ends the battle as Escaped, drops later actions and grants no rewards; disabled escape is refused");
}
