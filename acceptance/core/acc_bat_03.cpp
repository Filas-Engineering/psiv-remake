#include "support.h"

#include "rules/auto_policy.h"
#include "rules/names.h"

using namespace rules;

namespace {

// What each enemy chose this round: its ability (or basic attack) and target, as the stream shows it.
std::string enemyChoices(const std::vector<Event>& events, int partyCount) {
	std::string out;
	for (const Event& e : events) {
		if ((e.type == EventType::ActionStarted || e.type == EventType::ActionFailed) && e.actor >= partyCount) {
			out += std::to_string(e.actor) + ":" + toString(e.command) + ":" + e.ability + ">" + std::to_string(e.target) + " ";
		}
	}
	return out;
}

Battle prepared(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed) {
	Battle battle = support::create(db, support::setup(db, enemies, seed, 8));
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		support::sturdy(battle, i);
	}
	return battle;
}

}

ACC_CHECK(acc_bat_03, "ACC-BAT-03", "BAT-011") {
	const Database& db = support::data(t);
	const std::vector<std::vector<std::string>> groups = {{"rust_hound", "bog_wisp", "gel_mite"}, {"brass_warden", "bog_wisp"}, {"bog_wisp", "bog_wisp", "rust_hound"}};
	int compared = 0;
	for (const std::vector<std::string>& enemies : groups) {
		for (std::uint64_t seed = 1; seed <= 300; ++seed) {
			// Party command set A: everyone defends.
			Battle a = prepared(db, enemies, seed);
			for (int i = 0; i < a.partyCount(); ++i) {
				a.setCommand(i, Command::defend());
			}
			const std::string choicesA = enemyChoices(a.resolveRound(), a.partyCount());

			// Party command set B: everyone acts, mostly against the enemies.
			Battle b = prepared(db, enemies, seed);
			for (int i = 0; i < b.partyCount(); ++i) {
				b.setCommand(i, autoCommand(b, i));
			}
			const std::string choicesB = enemyChoices(b.resolveRound(), b.partyCount());

			t.expect(choicesA == choicesB, "seed " + std::to_string(seed) + ": enemy commands changed with the party's commands: [" + choicesA + "] vs [" + choicesB + "]");
			++compared;
		}
	}
	t.note(std::to_string(compared) + " rounds across 3 enemy groups: enemy commands identical under two different party command sets");
}
