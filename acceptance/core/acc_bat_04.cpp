#include "support.h"

#include "rules/names.h"

using namespace rules;

namespace {

// Sets every combatant's agility so the given actors act first, in this order, and everyone else after.
void actInOrder(Battle& battle, const std::vector<int>& actors) {
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).stats.agility = 1;
	}
	int agility = 1000;
	for (int actor : actors) {
		battle.combatant(actor).stats.agility = agility;
		agility -= 200;
	}
}

const Event* find(const std::vector<Event>& events, EventType type, int actor) {
	for (const Event& e : events) {
		if (e.type == type && e.actor == actor) {
			return &e;
		}
	}
	return nullptr;
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
		const Event* moved = find(events, EventType::ActionStarted, 2);
		t.expect(moved && moved->target == first + 2, "a single-enemy action whose target fell did not move to the next living enemy by formation position");
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
		const Event* moved = find(events, EventType::ActionStarted, 2);
		t.expect(moved && moved->target == first, "retarget past the last formation position did not wrap to the first living enemy");
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
		const Event* healed = find(events, EventType::ActionStarted, 3);
		t.expect(!battle.combatant(0).alive(), "setup: the healer's target did not fall first");
		t.expect(healed && healed->target == 3, "a one-ally heal whose target fell did not move to the actor");
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
		const Event* cancelled = find(events, EventType::ActionFailed, 3);
		t.expect(cancelled && cancelled->fail == FailReason::NoTarget, "a revive aimed at an ally who recovered first was not cancelled with a no-target reason");
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
		const Event* cancelled = find(events, EventType::ActionFailed, 1);
		t.expect(cancelled && cancelled->fail == FailReason::NoItem, "using an item the party ran out of earlier in the round was not cancelled with a cost reason");
	}

	// Technique points are checked at execution, not at selection.
	{
		Battle battle = support::create(db, support::setup(db, {"brass_warden"}, 26));
		support::sturdy(battle, first);
		t.expect(battle.setCommand(1, Command::ability("frostbite", -1)).empty(), "setup: technique command rejected");
		battle.combatant(1).tp = 2;
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const Event* cancelled = find(events, EventType::ActionFailed, 1);
		t.expect(cancelled && cancelled->fail == FailReason::NotEnoughTp, "a technique that can no longer be paid was not cancelled with a cost reason");
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
		const Event* prevented = find(events, EventType::ActionPrevented, first);
		t.expect(prevented && prevented->status == "sleep", "an actor put to sleep earlier in the round still acted, or no event gave the reason");
		t.expect(!find(events, EventType::ActionStarted, first), "the disabled actor's action still started");
	}

	// Actor defeated before its action: record what the stream says, since BAT-115 lists "actor unable".
	bool defeatedActorSilent = false;
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
		defeatedActorSilent = !find(events, EventType::ActionStarted, 1) && !find(events, EventType::ActionFailed, 1) && !find(events, EventType::ActionPrevented, 1);
	}

	t.gap("no Retargeted event: a retarget shows only as a different ActionStarted.target (BAT-015 via BAT-071, BAT-115)");
	t.note("retarget to next living enemy (with wrap), ally to actor, recovered revive cancelled, item and TP costs re-checked at execution, sleeping actor prevented with a reason event");
	if (defeatedActorSilent) {
		t.gap("an actor defeated earlier in the round is dropped with no cancel event; BAT-115 lists 'actor unable' as an ActionCancelled reason");
	}
}
