#include "support.h"

#include "rules/auto_policy.h"
#include "rules/names.h"

using namespace rules;

namespace {

// What each enemy got in phase 2 (CommandsLocked) and, unless only that is wanted, how it ran, as the stream shows it.
std::string enemyChoices(const std::vector<Event>& events, int partyCount, bool lockedOnly = false) {
	std::string out;
	for (const Event& e : events) {
		if (e.type == EventType::CommandsLocked) {
			for (const RecordedCommand& c : e.commands) {
				if (c.actor >= partyCount) {
					out += "locked " + std::to_string(c.actor) + ":" + toString(c.command.kind) + ":" + c.command.id + ">" + std::to_string(c.command.target) + " ";
				}
			}
		}
		if (!lockedOnly && (e.type == EventType::ActionStarted || e.type == EventType::ActionCancelled) && e.actor >= partyCount) {
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

// The enemy AI picks its action when its turn comes (BAT-091), from the battle's one generator, so what the party's
// actions draw and do before that turn can change it. What the AI must not read is the party's commands: commands
// that draw nothing and touch no enemy leave every enemy choice the same, and whatever the commands, phase 2 locks
// the same enemy commands.
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

			// Party command set B: everyone uses a cure on themselves, which draws nothing and touches no enemy.
			Battle b = prepared(db, enemies, seed);
			for (int i = 0; i < b.partyCount(); ++i) {
				b.setCommand(i, Command::item("clear_gel", i));
			}
			const std::string choicesB = enemyChoices(b.resolveRound(), b.partyCount());
			t.expect(choicesA == choicesB, "seed " + std::to_string(seed) + ": enemy commands changed with the party's commands: [" + choicesA + "] vs [" + choicesB + "]");

			// Party command set C: everyone acts against the enemies. Phase 2 still locks the same enemy commands.
			Battle c = prepared(db, enemies, seed);
			for (int i = 0; i < c.partyCount(); ++i) {
				c.setCommand(i, autoCommand(c, i));
			}
			const std::string lockedC = enemyChoices(c.resolveRound(), c.partyCount(), true);
			t.expect(choicesA.compare(0, lockedC.size(), lockedC) == 0, "seed " + std::to_string(seed) + ": phase 2 locked other enemy commands under other party commands: [" + lockedC + "]");
			++compared;
		}
	}
	t.note(std::to_string(compared) + " rounds across 3 enemy groups: enemy commands, as locked in phase 2 and as executed, identical under two party command sets that draw nothing; the commands locked in phase 2 also identical under a third set that attacks");
}
