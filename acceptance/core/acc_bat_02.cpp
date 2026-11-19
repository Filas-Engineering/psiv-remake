#include "support.h"

#include "rules/names.h"

#include <algorithm>

using namespace rules;

namespace {

// Where each event type sits in BAT-010's phases: 0 before the first round, 1 round start, 2 command
// selection, 3 order, 4 execution, 5 round end. BattleEnded can follow any action, so it has no phase.
int phaseOf(const Event& e, bool roundStarted) {
	if (!roundStarted) {
		return 0;
	}
	switch (e.type) {
	case EventType::RoundStarted:
		return 1;
	case EventType::CommandsLocked:
		return 2;
	case EventType::OrderResolved:
		return 3;
	case EventType::StatusRemoved:
		return e.statusEnd == StatusEnd::Duration ? 5 : 4;
	case EventType::StatusTicked:
	case EventType::StatModifierExpired:
	case EventType::RoundEnded:
		return 5;
	case EventType::BattleEnded:
		return -1;
	default:
		return 4;
	}
}

// Empty when the round's events follow BAT-010's phases, otherwise what is out of place.
std::string phaseProblem(const std::vector<Event>& events) {
	int last = 0;
	bool roundStarted = false;
	for (const Event& e : events) {
		roundStarted = roundStarted || e.type == EventType::RoundStarted;
		const int phase = phaseOf(e, roundStarted);
		if (phase < 0) {
			continue;
		}
		if (phase < last) {
			return std::string(toString(e.type)) + " (phase " + std::to_string(phase) + ") after phase " + std::to_string(last);
		}
		last = phase;
	}
	for (EventType required : {EventType::RoundStarted, EventType::CommandsLocked, EventType::OrderResolved}) {
		if (support::countType(events, required) != 1) {
			return std::string("the round has ") + std::to_string(support::countType(events, required)) + " " + toString(required) + " events";
		}
	}
	return "";
}

bool cannotAct(const Database& db, const Combatant& c) {
	for (const ActiveStatus& s : c.statuses) {
		const StatusDef* def = db.status(s.id);
		if (def && def->action == StatusAction::CannotAct) {
			return true;
		}
	}
	return false;
}

void defeat(Battle& battle, int index) {
	battle.combatant(index).hp = 0;
	battle.combatant(index).statuses = {{kDefeatedStatus, 0}};
}

}

ACC_CHECK(acc_bat_02, "ACC-BAT-02", "BAT-010, BAT-013") {
	const Database& db = support::data(t);

	// A round that exercises every phase: an action that weakens, a timed stat change, a poison tick and a short sleep.
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
		const std::string problem = phaseProblem(events);
		t.expect(problem.empty(), "round events are not in BAT-010 phase order: " + problem);
		t.expect(!events.empty() && events.back().type == EventType::RoundEnded, "the round did not close with RoundEnded");
		t.expect(support::countType(events, EventType::StatusTicked) == 1, "the poisoned enemy did not tick at round end");
		bool expired = false;
		for (const Event& e : events) {
			expired = expired || (e.type == EventType::StatusRemoved && e.status == "sleep" && e.statusEnd == StatusEnd::Duration);
		}
		t.expect(expired, "a one-round sleep did not count down and expire in phase 5");
		const std::vector<Event> order = support::ofType(events, EventType::OrderResolved);
		t.expect(order.size() == 1 && order[0].order == battle.lastTurnOrder(), "OrderResolved does not list the order the battle resolved");
		t.expect(support::actingOrder(events) == battle.lastTurnOrder(), "actions did not execute in the order resolved in phase 3");
	}

	// Phase 2 covers every living combatant that can act, across many played battles, and every round's events keep the phase order.
	ComboLog log;
	int decided = 0;
	int rounds = 0;
	int scripted = 0;
	for (std::uint64_t seed = 1; seed <= 200; ++seed) {
		const support::PlayedBattle played = support::playRandomBattle(db, seed, log);
		const int ended = support::countType(played.events, EventType::BattleEnded);
		t.expect(ended == 1 || (ended == 0 && played.end.outcome == Outcome::Ongoing), "battle " + std::to_string(seed) + " reported " + std::to_string(ended) + " results");
		decided += ended;
		scripted += played.end.outcome == Outcome::ScriptedEnd ? 1 : 0;
		std::vector<Event> round;
		for (std::size_t i = 0; i <= played.events.size(); ++i) {
			if (i == played.events.size() || (played.events[i].type == EventType::RoundStarted && !round.empty() && support::countType(round, EventType::RoundStarted) > 0)) {
				const std::string problem = phaseProblem(round);
				t.expect(problem.empty(), "battle " + std::to_string(seed) + " round " + std::to_string(rounds) + ": " + problem);
				round.clear();
				++rounds;
			}
			if (i < played.events.size()) {
				round.push_back(played.events[i]);
			}
		}
	}
	{
		Battle battle = support::create(db, support::setup(db, {"rust_hound", "bog_wisp", "gel_mite"}, 31, 4));
		battle.combatant(6).statuses.push_back({"sleep", 2});
		defeat(battle, 3);
		battle.setCommand(0, Command::defend());
		std::vector<int> canAct;
		for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
			if (battle.combatant(i).alive() && !cannotAct(db, battle.combatant(i))) {
				canAct.push_back(i);
			}
		}
		const std::vector<Event> locked = support::ofType(battle.resolveRound(), EventType::CommandsLocked);
		std::vector<int> listed;
		if (!locked.empty()) {
			for (const RecordedCommand& c : locked[0].commands) {
				if (c.command.kind != CommandKind::None) {
					listed.push_back(c.actor);
				}
			}
		}
		for (int actor : canAct) {
			t.expect(std::find(listed.begin(), listed.end(), actor) != listed.end(), "CommandsLocked has no command for combatant " + std::to_string(actor) + ", which is alive and can act");
		}
		t.expect(std::find(listed.begin(), listed.end(), 3) == listed.end(), "CommandsLocked lists a command for a fallen party member");
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
			defeat(battle, i);
		}
		battle.combatant(0).hp = 1;
		battle.combatant(5).stats.dexterity = 400;
		battle.setCommand(0, Command::defend());
		battle.setCommand(5, Command::attack(0));
		battle.resolveRound();
		t.expect(battle.outcome() == Outcome::Defeat, std::string("the last party member falling gave ") + toString(battle.outcome()) + ", not Defeat");
	}

	// Defeat when no party member can act: a status whose data counts toward Defeat on every member.
	std::string countingStatus;
	for (const auto& [id, status] : db.statuses) {
		if (status.countsTowardDefeat && countingStatus.empty()) {
			countingStatus = id;
		}
	}
	if (countingStatus.empty()) {
		t.gap("no status in core/data counts toward Defeat, so 'no party member can act' was not exercised (BAT-013)");
	} else {
		Battle battle = support::create(db, support::setup(db, {"brass_warden"}, 12));
		support::sturdy(battle, 5);
		for (int i = 0; i < battle.partyCount(); ++i) {
			support::sturdy(battle, i);
			battle.combatant(i).statuses.push_back({countingStatus, 0});
		}
		const std::vector<Event> events = battle.resolveRound();
		t.expect(battle.outcome() == Outcome::Defeat, std::string("a party all held by ") + countingStatus + ", which counts toward Defeat, gave " + toString(battle.outcome()));
		t.expect(support::countType(events, EventType::BattleEnded) == 1, "no single BattleEnded when no party member can act");
	}

	// Scripted end: surviving a number of rounds.
	{
		BattleSetup s = support::setup(db, {"brass_warden"}, 13);
		s.scriptedEnd.kind = ScriptedEndKind::SurviveRounds;
		s.scriptedEnd.rounds = 2;
		Battle battle = support::create(db, s);
		for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
			support::sturdy(battle, i);
		}
		int ends = 0;
		for (int round = 0; round < 5 && battle.outcome() == Outcome::Ongoing; ++round) {
			support::othersDefend(battle);
			ends += support::countType(battle.resolveRound(), EventType::BattleEnded);
		}
		t.expect(battle.outcome() == Outcome::ScriptedEnd && battle.round() == 2, std::string("a survive-2-rounds condition gave ") + toString(battle.outcome()) + " after round " + std::to_string(battle.round()));
		t.expect(ends == 1, "a scripted end reported " + std::to_string(ends) + " results");
	}

	// Scripted end: one enemy falls while others stand. Victory takes precedence when both happen together.
	for (const bool lastEnemy : {false, true}) {
		BattleSetup s = support::setup(db, lastEnemy ? std::vector<std::string>{"gel_mite"} : std::vector<std::string>{"gel_mite", "brass_warden"}, 14);
		s.scriptedEnd.kind = ScriptedEndKind::EnemyDefeated;
		s.scriptedEnd.enemySlot = 0;
		Battle battle = support::create(db, s);
		battle.combatant(5).hp = 1;
		if (!lastEnemy) {
			support::sturdy(battle, 6);
		}
		for (int i = 0; i < battle.partyCount(); ++i) {
			support::sturdy(battle, i);
			battle.combatant(i).stats.dexterity = 400;
		}
		battle.setCommand(0, Command::attack(5));
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const Outcome expected = lastEnemy ? Outcome::Victory : Outcome::ScriptedEnd;
		t.expect(battle.outcome() == expected, std::string(lastEnemy ? "the scripted enemy falling as the last enemy" : "the scripted enemy falling") + " gave " + toString(battle.outcome()) + ", not " + toString(expected));
		t.expect(support::countType(events, EventType::BattleEnded) == 1, "a battle decided by a scripted condition reported other than one result");
	}

	t.note("phase order 0-1-2-3-4-5 holds in " + std::to_string(rounds) + " played rounds; CommandsLocked covers every living combatant that can act; OrderResolved matches the executed order; end conditions are checked after each action and after round end; " +
		std::to_string(decided) + " of 200 random battles ended with exactly one result (" + std::to_string(scripted) + " scripted ends); Defeat, a status that counts toward Defeat, survive-rounds and enemy-defeated scripted ends, and Victory over a simultaneous scripted end all give one result");
}
