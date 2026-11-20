#include "fixtures.h"
#include "rules/formulas.h"
#include "rules/progression.h"
#include "test.h"

#include <limits>
#include <set>

using namespace rules;
using namespace fixtures;

TEST(poisonTicksEachRoundThenExpires) {
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	addStatus(battle, kFighter, "poison", 4);
	const int maxHp = battle.combatant(kFighter).stats.maxHp;
	const int tick = formulas::statusTick(maxHp, 8);
	for (int round = 1; round <= 5; ++round) {
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<Event> ticks = ofType(events, EventType::StatusTicked);
		const std::vector<Event> expired = ofType(events, EventType::StatusRemoved);
		if (round <= 4) {
			CHECK_EQ(ticks.size(), std::size_t(1));
			CHECK(!ticks.empty() && ticks[0].target == kFighter && ticks[0].amount == tick);
		} else {
			CHECK(ticks.empty());
		}
		CHECK_EQ(expired.size(), std::size_t(round == 4 ? 1 : 0));
	}
	CHECK_EQ(battle.combatant(kFighter).hp, maxHp - 4 * tick);
	CHECK(!battle.combatant(kFighter).hasStatus("poison"));
}

TEST(poisonCanDefeat) {
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	battle.combatant(kFighter).hp = 2;
	addStatus(battle, kFighter, "poison", 4);
	addStatus(battle, kFighter, "silence", 3);
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK_EQ(ofType(events, EventType::StatusTicked).at(0).amount, 2);
	CHECK_EQ(ofType(events, EventType::CombatantDefeated).at(0).target, kFighter);
	const Combatant& fighter = battle.combatant(kFighter);
	CHECK_EQ(fighter.statuses.size(), std::size_t(1));
	CHECK(fighter.hasStatus(kDefeatedStatus));
}

TEST(sleepSkipsTurnsUntilItWearsOff) {
	Database db = data();
	db.statuses["sleep"].recoverChance = 0;
	Battle battle = makeBattle(db, {"brass_warden"}, 2);
	addStatus(battle, kFighter, "sleep", 3);
	for (int round = 1; round <= 4; ++round) {
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::optional<Event> prevented = firstBy(events, EventType::ActionCancelled, kFighter);
		const std::optional<Event> acted = firstBy(events, EventType::ActionStarted, kFighter);
		if (round <= 3) {
			CHECK(prevented && prevented->status == "sleep");
			CHECK(!acted);
		} else {
			CHECK(!prevented);
			CHECK(acted.has_value());
		}
	}
}

// No shipped status wakes on damage, so the test gives sleep the rule.
TEST(damageWakesASleeper) {
	Database db = data();
	db.statuses["sleep"].wakesOnDamage = true;
	db.statuses["sleep"].recoverChance = 0;
	db.characters["fighter"].base.dexterity = 500;
	Battle battle = makeBattle(db, {"brass_warden"}, 4);
	addStatus(battle, kFirstEnemy, "sleep", 3);
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	othersDefend(battle);
	bool woke = false;
	for (const Event& e : ofType(battle.resolveRound(), EventType::StatusRemoved)) {
		woke = woke || (e.target == kFirstEnemy && e.statusEnd == StatusEnd::WokeOnDamage);
	}
	CHECK(woke);
	CHECK(!battle.combatant(kFirstEnemy).hasStatus("sleep"));
}

TEST(recoveryIsRolledAtTheEndOfTheRound) {
	Database db = data();
	db.statuses["sleep"].recoverChance = 100;
	Battle battle = makeBattle(db, {"brass_warden"}, 1);
	addStatus(battle, kFighter, "sleep", 0);
	addStatus(battle, kFighter, "poison", 2);
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	// The sleeper still loses this round's turn; the roll comes after the status ticks.
	CHECK(firstBy(events, EventType::ActionCancelled, kFighter).has_value());
	std::vector<EventType> tail;
	for (const Event& e : events) {
		if (e.type == EventType::StatusTicked || e.type == EventType::StatusRemoved || e.type == EventType::RoundEnded) {
			tail.push_back(e.type);
		}
	}
	CHECK_EQ(tail, (std::vector<EventType>{EventType::StatusTicked, EventType::StatusRemoved, EventType::RoundEnded}));
	const std::vector<Event> removed = ofType(events, EventType::StatusRemoved);
	CHECK(removed.size() == 1 && removed[0].status == "sleep" && removed[0].statusEnd == StatusEnd::Recovered && removed[0].target == kFighter);
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	othersDefend(battle);
	CHECK(firstBy(battle.resolveRound(), EventType::ActionStarted, kFighter).has_value());

	db.statuses["sleep"].recoverChance = 0;
	Battle stuck = makeBattle(db, {"brass_warden"}, 1);
	addStatus(stuck, kFighter, "sleep", 0);
	for (int round = 0; round < 5; ++round) {
		othersDefend(stuck);
		CHECK(firstBy(stuck.resolveRound(), EventType::ActionCancelled, kFighter).has_value());
	}
}

TEST(silenceBlocksTechniquesButNotSkills) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 1);
	addStatus(battle, kMystic, "silence", 3);
	addStatus(battle, kFighter, "silence", 3);
	const int tp = battle.combatant(kMystic).tp;
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
	battle.setCommand(kFighter, Command::ability("focus_shot", kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> failed = firstBy(events, EventType::ActionCancelled, kMystic);
	CHECK(failed && failed->cancel == CancelReason::Silenced);
	CHECK_EQ(battle.combatant(kMystic).tp, tp);
	CHECK(firstBy(events, EventType::ActionStarted, kFighter).has_value());
	CHECK_EQ(battle.combatant(kFighter).skillUses.at("focus_shot"), 2);
}

TEST(curesAndItemsRemoveStatuses) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 1);
	addStatus(battle, kScout, "poison", 0);
	addStatus(battle, kMystic, "sleep", 3);
	addStatus(battle, kEngineer, "paralysis", 3);
	battle.setCommand(kMedic, Command::ability("purge", kScout));
	battle.setCommand(kFighter, Command::item("wake_chime", -1));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK(!battle.combatant(kScout).hasStatus("poison"));
	CHECK_EQ(battle.inventory().at("wake_chime"), 0);
	int cured = 0;
	for (const Event& e : ofType(events, EventType::StatusRemoved)) {
		cured += e.statusEnd == StatusEnd::Cured ? 1 : 0;
	}
	CHECK(cured >= 2);
}

namespace {

// Gives the fighter a basic attack that applies the status every time, so a test controls each application.
Database withInflictingAttack(const std::string& status) {
	Database db = data();
	AbilityDef inflict;
	inflict.id = "test_inflict";
	inflict.name = "Test Inflict";
	inflict.kind = AbilityKind::Attack;
	inflict.targeting = Targeting::OneEnemy;
	Effect effect;
	effect.kind = EffectKind::ApplyStatus;
	effect.status = status;
	effect.threshold = std::numeric_limits<int>::min();
	inflict.effects = {effect};
	db.abilities[inflict.id] = inflict;
	db.characters["fighter"].basicAttack = inflict.id;
	db.characters["scout"].basicAttack = inflict.id;
	db.characters["scout"].startingEquipment.clear();
	db.enemies["rust_hound"].statusResist.clear();
	return db;
}

int remaining(const Combatant& c, const std::string& status) {
	for (const ActiveStatus& active : c.statuses) {
		if (active.id == status) {
			return active.remaining;
		}
	}
	return -1;
}

}

TEST(persistentStatusesCarryOutOfBattleAndBack) {
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	addStatus(battle, kFighter, "poison", 4);
	addStatus(battle, kFighter, "silence", 3);
	othersDefend(battle);
	battle.resolveRound();
	std::vector<PartyMember> party = battle.partyState();
	CHECK_EQ(party[kFighter].statuses.size(), std::size_t(1));
	CHECK(!party[kFighter].statuses.empty() && party[kFighter].statuses[0].id == "poison" && party[kFighter].statuses[0].remaining == 3);

	BattleSetup next = makeSetup(data(), {"rust_hound"}, 2);
	next.party = party;
	Battle second = makeBattle(data(), next);
	CHECK(second.combatant(kFighter).hasStatus("poison"));
	CHECK_EQ(remaining(second.combatant(kFighter), "poison"), 3);

	next.party[kFighter].statuses.push_back({"silence", 2});
	const Result<Battle> refused = Battle::create(data(), next, nullptr);
	CHECK(!refused.ok() && refused.error.find("does not persist outside battle") != std::string::npos);
	next.party[kFighter].statuses.back().id = "smog";
	CHECK(!Battle::create(data(), next, nullptr).ok());

	// A fallen member carries nothing but the fall itself into battle.
	next.party[kFighter].statuses = {{"poison", 2}};
	next.party[kFighter].hp = 0;
	Battle fallen = makeBattle(data(), next);
	CHECK(fallen.combatant(kFighter).statuses.size() == 1 && fallen.combatant(kFighter).hasStatus(kDefeatedStatus));
}

TEST(forcedStatusSuppliesARandomAttackInPhaseTwo) {
	std::set<int> targets;
	for (std::uint64_t seed = 1; seed <= 40; ++seed) {
		Battle battle = makeBattle(data(), {"gel_mite", "gel_mite"}, seed);
		for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
			battle.combatant(i).stats.maxHp = 999;
			battle.combatant(i).hp = 999;
		}
		addStatus(battle, kFighter, "jumble", 2);
		CHECK_EQ(battle.setCommand(kFighter, Command::defend()).key, std::string("command.forced_by_status"));
		Macro macro;
		macro.name = "test";
		macro.steps.push_back({"fighter", CommandKind::Defend, "", {}});
		const std::vector<MacroStepResult> results = battle.applyMacro(macro);
		CHECK(results[0].status == MacroStepStatus::Skipped && results[0].issue == MacroIssue::ForcedByStatus);
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<Event> locked = ofType(events, EventType::CommandsLocked);
		CHECK(!locked.empty() && locked[0].commands[0].actor == kFighter && locked[0].commands[0].command.kind == CommandKind::Attack);
		const std::optional<Event> attack = firstBy(events, EventType::ActionStarted, kFighter);
		CHECK(attack.has_value());
		if (attack && !locked.empty()) {
			CHECK(attack->target != kFighter);
			CHECK_EQ(attack->target, locked[0].commands[0].command.target);
			targets.insert(attack->target);
		}
	}
	// random_any reaches both sides.
	CHECK(targets.count(kFirstEnemy) + targets.count(kFirstEnemy + 1) > 0);
	CHECK(targets.count(kMystic) + targets.count(kScout) + targets.count(kMedic) + targets.count(kEngineer) > 0);
}

TEST(stackingRuleDecidesWhatAReapplicationDoes) {
	{
		// Extend: two silences in one round add up.
		Database db = withInflictingAttack("silence");
		Battle battle = makeBattle(db, {"rust_hound"}, 1);
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		battle.setCommand(kScout, Command::attack(kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> applied = ofType(battle.resolveRound(), EventType::StatusApplied);
		CHECK(applied.size() == 2 && applied[0].duration == 3 && applied[1].duration == 6);
		CHECK_EQ(remaining(battle.combatant(kFirstEnemy), "silence"), 5);
	}
	{
		// Refresh: the second application resets the count.
		Database db = withInflictingAttack("silence");
		db.statuses["silence"].stacking = StatusStacking::Refresh;
		Battle battle = makeBattle(db, {"rust_hound"}, 1);
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		battle.setCommand(kScout, Command::attack(kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> applied = ofType(battle.resolveRound(), EventType::StatusApplied);
		CHECK(applied.size() == 2 && applied[1].duration == 3);
		CHECK_EQ(remaining(battle.combatant(kFirstEnemy), "silence"), 2);
	}
	{
		// Keep: the second application changes nothing and says so.
		Database db = withInflictingAttack("jumble");
		Battle battle = makeBattle(db, {"rust_hound"}, 1);
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		battle.setCommand(kScout, Command::attack(kFirstEnemy));
		battle.setCommand(kFirstEnemy, Command::defend());
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		CHECK_EQ(ofType(events, EventType::StatusApplied).size(), std::size_t(1));
		const std::vector<Event> kept = ofType(events, EventType::StatusResisted);
		CHECK(kept.size() == 1 && kept[0].message == "status.jumble.unchanged");
	}
}

TEST(statusesThatCountTowardDefeatDecideTheBattle) {
	Battle locked = makeBattle(data(), {"gel_mite"}, 1);
	for (int i = 0; i < locked.partyCount(); ++i) {
		addStatus(locked, i, "gear_lock", 0);
	}
	const std::vector<Event> events = locked.resolveRound();
	CHECK(locked.outcome() == Outcome::Defeat);
	for (const Event& e : ofType(events, EventType::ActionStarted)) {
		CHECK(!locked.isParty(e.actor));
	}

	// One locked enemy left standing is a victory, and pays its rewards.
	Battle win = makeBattle(data(), {"rust_hound"}, 1);
	addStatus(win, kFirstEnemy, "gear_lock", 0);
	partyDefends(win);
	win.resolveRound();
	CHECK(win.outcome() == Outcome::Victory);
	CHECK_EQ(win.rewards().experience, 10);
	CHECK(win.combatant(kFirstEnemy).alive());

	// A plain cannot-act status does not.
	Battle asleep = makeBattle(data(), {"gel_mite"}, 1);
	for (int i = 0; i < asleep.partyCount(); ++i) {
		addStatus(asleep, i, "sleep", 3);
	}
	asleep.resolveRound();
	CHECK(asleep.outcome() == Outcome::Ongoing);
}

TEST(restCuresByDataAndRevivesByRule) {
	Database db = data();
	PartyMember fighter = makePartyMember(db, db.characters.at("fighter"), 1);
	fighter.statuses = {{"poison", 2}, {"gear_lock", 0}};
	fighter.hp = 5;
	rest(fighter, db);
	CHECK(fighter.statuses.empty());
	CHECK_EQ(fighter.hp, 70);

	db.statuses["gear_lock"].restCures = false;
	fighter.statuses = {{"poison", 2}, {"gear_lock", 0}};
	rest(fighter, db);
	CHECK(fighter.statuses.size() == 1 && fighter.statuses[0].id == "gear_lock");

	db.battleRules.restRevives = false;
	fighter.hp = 0;
	rest(fighter, db);
	CHECK_EQ(fighter.hp, 0);
	CHECK_EQ(fighter.tp, memberStats(db, fighter).maxTp);
}

TEST(enemyDurationReplacesDurationOnEnemiesOnly) {
	Database db = withInflictingAttack("paralysis");
	db.enemies["rust_hound"].basicAttack = "test_inflict";
	db.characters["fighter"].base.agility = 500;
	Battle battle = makeBattle(db, {"rust_hound"}, 1);
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	battle.setCommand(kFirstEnemy, Command::attack(kScout));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	// The fighter acts first, so the paralysis cancels the hound's turn and wears off at the end of the round.
	const std::vector<Event> applied = ofType(events, EventType::StatusApplied);
	CHECK(applied.size() == 1 && applied[0].target == kFirstEnemy && applied[0].duration == 1);
	CHECK(firstBy(events, EventType::ActionCancelled, kFirstEnemy).has_value());
	const std::vector<Event> removed = ofType(events, EventType::StatusRemoved);
	CHECK(removed.size() == 1 && removed[0].target == kFirstEnemy && removed[0].statusEnd == StatusEnd::Duration);

	// On a party member it lasts until cured.
	battle.setCommand(kFirstEnemy, Command::attack(kScout));
	partyDefends(battle);
	const std::vector<Event> second = ofType(battle.resolveRound(), EventType::StatusApplied);
	CHECK(second.size() == 1 && second[0].target == kScout && second[0].duration == 0);
	for (int round = 0; round < 3; ++round) {
		partyDefends(battle);
		battle.resolveRound();
	}
	CHECK_EQ(remaining(battle.combatant(kScout), "paralysis"), 0);
}
