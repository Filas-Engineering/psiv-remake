#include "support.h"

using namespace rules;

namespace {

constexpr int kFirst = 5;

std::vector<Event> playMacroRound(const Database& db, const Macro& macro, ComboLog& log, std::uint64_t seed, std::int64_t playTime) {
	Battle battle = support::create(db, support::setup(db, {"brass_warden"}, seed, 6), &log);
	support::sturdy(battle, kFirst);
	battle.setPlayTime(playTime);
	battle.applyMacro(macro);
	support::othersDefend(battle);
	return battle.resolveRound();
}

}

ACC_CHECK(acc_bat_12, "ACC-BAT-12", "BAT-064") {
	const Database& db = support::data(t);
	const Macro& opener = *db.macro("opener");
	ComboLog log;

	const std::vector<Event> first = playMacroRound(db, opener, log, 1, 754321);
	const std::vector<Event> fired = support::ofType(first, EventType::ComboTriggered);
	const std::vector<Event> found = support::ofType(first, EventType::ComboDiscovered);
	t.expect(fired.size() == 1, "setup: the opener macro did not trigger exactly one combination");
	t.expect(found.size() == 1 && !fired.empty() && found[0].combo == fired[0].combo, "the first trigger of a combination did not emit one ComboDiscovered with its id");
	t.expect(log.entries().size() == 1, "the first trigger did not add exactly one entry to the combo log");
	if (!fired.empty() && log.entries().size() == 1) {
		const ComboLogEntry& entry = log.entries()[0];
		std::vector<std::string> characters;
		for (int participant : fired[0].participants) {
			characters.push_back(db.lineup[static_cast<std::size_t>(participant)]);
		}
		t.expect(entry.recipe == fired[0].combo, "the combo log entry names " + entry.recipe + ", not the combination that fired");
		t.expect(entry.playTimeMs == 754321, "the combo log entry stores play time " + std::to_string(entry.playTimeMs) + ", not the host's 754321 ms");
		t.expect(entry.characters == characters, "the combo log entry does not list the characters involved in the order they acted");
	}

	const std::vector<Event> second = playMacroRound(db, opener, log, 2, 900000);
	t.expect(support::countType(second, EventType::ComboTriggered) == 1 && support::countType(second, EventType::ComboDiscovered) == 0, "a second trigger emitted ComboDiscovered again");
	t.expect(log.entries().size() == 1 && log.entries()[0].playTimeMs == 754321, "a second trigger added an entry or overwrote the first one");

	// The log is saved: it survives its text form.
	{
		Result<ComboLog> parsed = parseComboLog(serializeComboLog(log));
		t.expect(parsed.ok() && parsed.value->entries().size() == 1 && parsed.value->entries()[0].recipe == log.entries()[0].recipe &&
				parsed.value->entries()[0].playTimeMs == log.entries()[0].playTimeMs && parsed.value->entries()[0].characters == log.entries()[0].characters,
			"the combo log does not survive serializeComboLog and parseComboLog");
	}

	// Auto-battle: the auto macro supplies the rounds with no input; its triggers are logged and discovered too.
	ComboLog autoLog;
	Battle battle = support::create(db, support::setup(db, {"brass_warden", "bog_wisp"}, 5, 6), &autoLog);
	const Macro& tempest = *db.macro("tempest");
	t.expect(battle.startAuto(tempest), "setup: auto-battle did not start");
	int discoveries = 0;
	while (battle.outcome() == Outcome::Ongoing && battle.round() < 30) {
		battle.setPlayTime(1000 + battle.round());
		if (!battle.fillFromAuto()) {
			battle.startAuto(tempest);
			battle.fillFromAuto();
		}
		discoveries += support::countType(battle.resolveRound(), EventType::ComboDiscovered);
	}
	t.expect(discoveries >= 1 && static_cast<int>(autoLog.entries().size()) == discoveries, "combinations triggered by auto-battle were not discovered and logged one entry each");

	t.note("first trigger emits ComboDiscovered and adds one entry with the combination id, the host's play time and the characters in acting order; later triggers do not; the log round-trips through its save text; auto-battle triggers are discovered and logged");
}
