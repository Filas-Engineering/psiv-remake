#include "support.h"

#include "rules/names.h"

using namespace rules;

namespace {

// Sets every combatant's agility so the two given actors act first, in this order, and everyone else after.
// The largest roll is under half the highest agility, so 400 always beats 200 and 200 always beats 0.
void actInOrder(Battle& battle, const std::vector<int>& actors) {
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).stats.agility = 0;
	}
	battle.combatant(actors[0]).stats.agility = 400;
	battle.combatant(actors[1]).stats.agility = 200;
}

const Event* find(const std::vector<Event>& events, EventType type, int actor) {
	for (const Event& e : events) {
		if (e.type == type && e.actor == actor) {
			return &e;
		}
	}
	return nullptr;
}

std::ptrdiff_t position(const std::vector<Event>& events, const Event* e) {
	return e ? e - events.data() : -1;
}

// Empty when the stream has a Retargeted event for `actor` from `from` to `to`, right before its ActionStarted.
std::string retargetProblem(const std::vector<Event>& events, int actor, int from, int to) {
	const Event* moved = find(events, EventType::Retargeted, actor);
	const Event* started = find(events, EventType::ActionStarted, actor);
	if (!moved) {
		return "no Retargeted event";
	}
	if (moved->previousTarget != from || moved->target != to) {
		return "Retargeted says " + std::to_string(moved->previousTarget) + " to " + std::to_string(moved->target) + ", expected " + std::to_string(from) + " to " + std::to_string(to);
	}
	if (!started || started->target != to || position(events, started) < position(events, moved)) {
		return "the action did not start on the new target after the Retargeted event";
	}
	return "";
}

std::string cancelProblem(const std::vector<Event>& events, int actor, CancelReason reason, const std::string& status = "") {
	const Event* cancelled = find(events, EventType::ActionCancelled, actor);
	if (!cancelled) {
		return "no ActionCancelled event";
	}
	if (cancelled->cancel != reason || (!status.empty() && cancelled->status != status)) {
		return std::string("ActionCancelled gives ") + toString(cancelled->cancel) + " " + cancelled->status + ", expected " + toString(reason) + " " + status;
	}
	if (find(events, EventType::ActionStarted, actor)) {
		return "the action also started";
	}
	return "";
}

}

ACC_CHECK(acc_bat_04, "ACC-BAT-04", "BAT-015, BAT-071") {
	Database db = support::data(t);
	const int first = 5;

	// Target falls before the action: one enemy moves to the next living enemy by formation position.
	{
		Battle battle = support::create(db, support::setup(db, {"gel_mite", "gel_mite", "rust_hound"}, 21));
		battle.combatant(first + 1).hp = 1;
		support::sturdy(battle, first + 2);
		actInOrder(battle, {1, 2});
		battle.setCommand(1, Command::ability("spark", first + 1));
		battle.setCommand(2, Command::ability("weaken", first + 1));
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::string problem = retargetProblem(events, 2, first + 1, first + 2);
		t.expect(problem.empty(), "a single-enemy action whose target fell did not move to the next living enemy by formation position: " + problem);
	}

	// Wrap-around: the fallen target was the last in formation, so the action moves to the first living enemy.
	{
		Battle battle = support::create(db, support::setup(db, {"rust_hound", "gel_mite"}, 22));
		support::sturdy(battle, first);
		battle.combatant(first + 1).hp = 1;
		actInOrder(battle, {1, 2});
		battle.setCommand(1, Command::ability("spark", first + 1));
		battle.setCommand(2, Command::ability("weaken", first + 1));
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::string problem = retargetProblem(events, 2, first + 1, first);
		t.expect(problem.empty(), "retarget past the last formation position did not wrap to the first living enemy: " + problem);
	}

	// One ally whose target fell moves to the actor.
	{
		Battle battle = support::create(db, support::setup(db, {"brass_warden"}, 23));
		support::sturdy(battle, first);
		battle.combatant(first).stats.dexterity = 5000;
		battle.combatant(0).hp = 1;
		battle.combatant(3).hp = 10;
		actInOrder(battle, {first, 3});
		battle.setCommand(first, Command::attack(0));
		battle.setCommand(3, Command::ability("mend", 0));
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		t.expect(!battle.combatant(0).alive(), "setup: the healer's target did not fall first");
		const std::string problem = retargetProblem(events, 3, 0, 3);
		t.expect(problem.empty(), "a one-ally heal whose target fell did not move to the actor: " + problem);
	}

	// One fallen ally that recovered before the action: the revive is cancelled with a reason.
	{
		Battle battle = support::create(db, support::setup(db, {"brass_warden"}, 24));
		support::sturdy(battle, first);
		battle.combatant(4).hp = 0;
		battle.combatant(4).statuses = {{kDefeatedStatus, 0}};
		battle.combatant(3).tp = 99;
		actInOrder(battle, {0, 3});
		battle.setCommand(0, Command::item("second_wind", 4));
		battle.setCommand(3, Command::ability("rekindle", 4));
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::string problem = cancelProblem(events, 3, CancelReason::NoTarget);
		t.expect(problem.empty(), "a revive aimed at an ally who recovered first was not cancelled with a no-target reason: " + problem);
		t.expect(!find(events, EventType::ResourceSpent, 3), "the cancelled revive still spent its cost");
	}

	// Cost drained before the action: two members use the last item in stock.
	{
		BattleSetup s = support::setup(db, {"brass_warden"}, 25);
		s.inventory = {{"tonic", 1}};
		Battle battle = support::create(db, s);
		support::sturdy(battle, first);
		battle.combatant(4).hp = 5;
		actInOrder(battle, {0, 1});
		t.expect(battle.setCommand(0, Command::item("tonic", 4)).empty(), "setup: first tonic command rejected");
		t.expect(battle.setCommand(1, Command::item("tonic", 4)).empty(), "selection refused a second use of the last item instead of validating at execution");
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::string problem = cancelProblem(events, 1, CancelReason::NoItem);
		t.expect(problem.empty(), "using an item the party ran out of earlier in the round was not cancelled with a cost reason: " + problem);
	}

	// Technique points are checked at execution, not at selection.
	{
		Battle battle = support::create(db, support::setup(db, {"brass_warden"}, 26));
		support::sturdy(battle, first);
		t.expect(battle.setCommand(1, Command::ability("frostbite", -1)).empty(), "setup: technique command rejected");
		battle.combatant(1).tp = 2;
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::string problem = cancelProblem(events, 1, CancelReason::NotEnoughTp);
		t.expect(problem.empty(), "a technique that can no longer be paid was not cancelled with a cost reason: " + problem);
		t.expect(battle.combatant(1).tp == 2, "the cancelled technique still spent technique points");
	}

	// Actor disabled before the action: an earlier action puts it to sleep.
	{
		db.abilities["acc_doze"] = {"acc_doze", "Doze Dart", AbilityKind::Attack, 0, 0, Targeting::OneEnemy, {}};
		Effect doze;
		doze.kind = EffectKind::ApplyStatus;
		doze.status = "sleep";
		doze.chance = 100;
		db.abilities["acc_doze"].effects = {doze};
		db.enemies["rust_hound"].statusResist.clear();
		Battle battle = support::create(db, support::setup(db, {"rust_hound"}, 27));
		battle.combatant(0).basicAttack = "acc_doze";
		support::sturdy(battle, first);
		actInOrder(battle, {0, first});
		battle.setCommand(0, Command::attack(first));
		battle.setCommand(first, Command::attack(1));
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::string problem = cancelProblem(events, first, CancelReason::ActorUnable, "sleep");
		t.expect(problem.empty(), "an actor put to sleep earlier in the round was not cancelled as unable to act: " + problem);
	}

	// Actor defeated before its action: cancelled as unable to act, BAT-115's "actor unable".
	{
		Battle battle = support::create(db, support::setup(db, {"brass_warden"}, 28));
		support::sturdy(battle, first);
		battle.combatant(first).stats.dexterity = 5000;
		battle.combatant(1).hp = 1;
		actInOrder(battle, {first, 1});
		battle.setCommand(first, Command::attack(1));
		battle.setCommand(1, Command::ability("spark", first));
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		t.expect(!battle.combatant(1).alive(), "setup: the actor did not fall first");
		const std::string problem = cancelProblem(events, 1, CancelReason::ActorUnable, kDefeatedStatus);
		t.expect(problem.empty(), "an actor defeated earlier in the round was not cancelled as unable to act: " + problem);
	}

	t.note("Retargeted to the next living enemy (with wrap) and from a fallen ally to the actor, each before ActionStarted; a recovered revive cancelled with no_target; item and technique costs re-checked at execution and cancelled with no_item and not_enough_tp; a sleeping or fallen actor cancelled with actor_unable and the cause");
}
