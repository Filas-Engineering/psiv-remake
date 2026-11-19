#include "support.h"

#include "rules/auto_policy.h"

using namespace rules;

namespace {

constexpr int kFirst = 5;

std::vector<Event> playMacroRound(const Database& db, const Macro& macro, ComboLog& log, std::uint64_t seed) {
	Battle battle = support::create(db, support::setup(db, {"brass_warden"}, seed, 6), &log);
	support::sturdy(battle, kFirst);
	battle.applyMacro(macro);
	support::othersDefend(battle);
	return battle.resolveRound();
}

}

ACC_CHECK(acc_bat_12, "ACC-BAT-12", "BAT-064") {
	const Database& db = support::data(t);
	const Macro& opener = *db.macro("opener");
	ComboLog log;

	const std::vector<Event> first = support::ofType(playMacroRound(db, opener, log, 1), EventType::ComboTriggered);
	t.expect(first.size() == 1 && first[0].firstDiscovery, "the first trigger of a combination was not marked as a discovery");
	t.expect(log.discovered().size() == 1, "the first trigger did not add exactly one entry to the combo log");
	const std::size_t entries = log.discovered().size();

	const std::vector<Event> second = support::ofType(playMacroRound(db, opener, log, 2), EventType::ComboTriggered);
	t.expect(second.size() == 1 && !second[0].firstDiscovery, "a second trigger was marked as a new discovery");
	t.expect(log.discovered().size() == entries, "a second trigger added another combo log entry");

	// Auto-battle: the host plays the auto macro every round with no input; its triggers count too.
	ComboLog autoLog;
	Battle battle = support::create(db, support::setup(db, {"brass_warden", "bog_wisp"}, 5, 6), &autoLog);
	const Macro& tempest = *db.macro("tempest");
	int discoveries = 0;
	while (battle.outcome() == Outcome::Ongoing && battle.round() < 30) {
		battle.applyMacro(tempest);
		for (const Event& e : battle.resolveRound()) {
			discoveries += e.type == EventType::ComboTriggered && e.firstDiscovery ? 1 : 0;
		}
	}
	t.expect(discoveries >= 1 && !autoLog.discovered().empty(), "combinations triggered by the auto macro were not logged");

	t.gap("no ComboDiscovered event: discovery is a firstDiscovery flag on ComboTriggered (BAT-064, BAT-115)");
	t.gap("ComboLog stores only the combination ID, not play time or the characters involved (BAT-064)");
	t.note("first trigger adds one log entry and is marked; later triggers are not; auto-macro triggers are logged");
}
