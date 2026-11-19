#include "support.h"

#include "rules/names.h"

using namespace rules;

namespace {

// Where each event type sits in BAT-010's phases: 1 round start, 4 execution, 5 round end.
int phaseOf(const Event& e) {
	switch (e.type) {
	case EventType::RoundStarted:
		return 1;
	case EventType::StatusExpired:
		return e.statusEnd == StatusEnd::Duration ? 5 : 4;
	case EventType::StatusTicked:
	case EventType::StatModifierExpired:
	case EventType::RoundEnded:
	case EventType::BattleEnded:
		return 5;
	default:
		return 4;
	}
}

}

ACC_CHECK(acc_bat_02, "ACC-BAT-02", "BAT-010, BAT-013") {
	const Database& db = support::data(t);

	// A round that exercises every phase: an action that poisons, a timed stat change, and a short sleep.
	{
		Battle battle = support::create(db, support::setup(db, {"rust_hound", "gel_mite"}, 11));
		support::sturdy(battle, 5);
		support::sturdy(battle, 6);
		battle.combatant(6).statuses.push_back({"poison", 3});
		battle.combatant(5).statuses.push_back({"sleep", 1});
		battle.setCommand(2, Command::ability("weaken", 5));
		battle.setCommand(0, Command::ability("rally", -1));
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		int last = 0;
		bool ordered = !events.empty() && events.front().type == EventType::RoundStarted && events.back().type == EventType::RoundEnded;
		for (const Event& e : events) {
			const int phase = phaseOf(e);
			ordered = ordered && phase >= last;
			last = phase;
		}
		t.expect(ordered, "round events are not in BAT-010 phase order: " + serializeEvents(events));
		t.expect(support::countType(events, EventType::StatusTicked) == 1, "the poisoned enemy did not tick at round end");
		bool expired = false;
		for (const Event& e : events) {
			expired = expired || (e.type == EventType::StatusExpired && e.status == "sleep" && e.statusEnd == StatusEnd::Duration);
		}
		t.expect(expired, "a one-round sleep did not count down and expire in phase 5");
		const std::vector<int> acted = support::actingOrder(events);
		t.expect(acted == battle.lastTurnOrder(), "actions did not execute in the order resolved in phase 3");
	}

	// End conditions after every action: the first action defeats the last enemy, so nothing else acts.
	{
		Battle battle = support::create(db, support::setup(db, {"gel_mite"}, 3));
		battle.combatant(5).hp = 1;
		for (int i = 0; i < battle.partyCount(); ++i) {
			battle.combatant(i).stats.agility = 1;
		}
		battle.combatant(2).stats.agility = 400;
		battle.setCommand(2, Command::ability("gust", 5));
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		t.expect(battle.outcome() == Outcome::Victory, "defeating the only enemy did not end the battle in Victory");
		t.expect(support::countType(events, EventType::ActionStarted) == 1, "actions kept executing after the battle was decided");
		t.expect(support::countType(events, EventType::BattleEnded) == 1 && events.back().type == EventType::BattleEnded, "the round did not close with exactly one BattleEnded");
		t.expect(battle.resolveRound().empty(), "a finished battle resolved another round");
	}

	// And after phase 5: a poison tick defeats the last enemy.
	{
		Battle battle = support::create(db, support::setup(db, {"gel_mite"}, 5));
		battle.combatant(5).hp = 1;
		battle.combatant(5).statusResist.clear();
		battle.combatant(5).statuses.push_back({"poison", 3});
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		t.expect(battle.outcome() == Outcome::Victory, "a round-end tick that defeats the last enemy did not end the battle");
		t.expect(support::countType(events, EventType::BattleEnded) == 1, "no BattleEnded after the decisive tick");
	}

	// Defeat: every party member down.
	{
		Battle battle = support::create(db, support::setup(db, {"brass_warden"}, 9));
		for (int i = 1; i < battle.partyCount(); ++i) {
			battle.combatant(i).hp = 0;
			battle.combatant(i).statuses = {{kDefeatedStatus, 0}};
		}
		battle.combatant(0).hp = 1;
		battle.combatant(5).stats.dexterity = 400;
		battle.setCommand(0, Command::defend());
		battle.setCommand(5, Command::attack(0));
		battle.resolveRound();
		t.expect(battle.outcome() == Outcome::Defeat, std::string("the last party member falling gave ") + toString(battle.outcome()) + ", not Defeat");
	}

	// Exactly one result per battle over many played battles.
	ComboLog log;
	int decided = 0;
	for (std::uint64_t seed = 1; seed <= 200; ++seed) {
		const support::PlayedBattle played = support::playRandomBattle(db, seed, log);
		const int ended = support::countType(played.events, EventType::BattleEnded);
		t.expect(ended <= 1, "battle " + std::to_string(seed) + " reported " + std::to_string(ended) + " results");
		decided += ended;
	}

	t.gap("phases 2 and 3 are not visible in the event stream: core emits no CommandsLocked or OrderResolved event (BAT-010 via the BAT-115 set)");
	t.gap("no Scripted end result: Outcome has only Victory, Defeat and Escaped, and no setup field sets an end condition (BAT-013)");
	t.note("phase order 1-4-5 holds, actions run in the resolved order, end conditions are checked after each action and after round end, " +
		std::to_string(decided) + " of 200 random battles ended with exactly one result");
}
