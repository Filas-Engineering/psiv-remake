#include "fixtures.h"
#include "rules/formulas.h"
#include "rules/progression.h"
#include "test.h"

#include <limits>
#include <set>

using namespace rules;
using namespace fixtures;

// Poison does nothing in battle and does not wear off; it only lasts beyond it.
TEST(poisonHasNoEffectInBattle) {
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	addStatus(battle, kFighter, "poison", 0);
	const int hp = battle.combatant(kFighter).hp;
	for (int round = 1; round <= 5; ++round) {
		othersDefend(battle);
		CHECK(ofType(battle.resolveRound(), EventType::StatusRemoved).empty());
	}
	CHECK_EQ(battle.combatant(kFighter).hp, hp);
	CHECK(battle.combatant(kFighter).hasStatus("poison"));
	CHECK(battle.partyState()[kFighter].statuses.size() == 1);
}

// The fall clears every status except one that survives it, which stays through a revive.
TEST(defeatKeepsOnlyStatusesThatSurviveIt) {
	Database db = data();
	db.enemies["gel_mite"].stats.strength = 400;
	db.enemies["gel_mite"].stats.dexterity = 400;
	Battle battle = makeBattle(db, {"gel_mite"}, 1);
	battle.combatant(kFighter).hp = 2;
	addStatus(battle, kFighter, "poison", 0);
	addStatus(battle, kFighter, "silence", 0);
	battle.setCommand(kFirstEnemy, Command::attack(kFighter));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK_EQ(ofType(events, EventType::CombatantDefeated).at(0).target, kFighter);
	const std::vector<Event> removed = ofType(events, EventType::StatusRemoved);
	CHECK(removed.size() == 1 && removed[0].status == "poison" && removed[0].statusEnd == StatusEnd::Defeated);
	CHECK(battle.combatant(kFighter).hasStatus("silence") && battle.combatant(kFighter).hasStatus(kDefeatedStatus));

	battle.setCommand(kMedic, Command::ability("rekindle", kFighter));
	othersDefend(battle);
	battle.resolveRound();
	CHECK(battle.combatant(kFighter).alive());
	CHECK(battle.combatant(kFighter).statuses.size() == 1 && battle.combatant(kFighter).hasStatus("silence"));
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
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	// The sleeper still loses this round's turn; the roll comes after every action.
	CHECK(firstBy(events, EventType::ActionCancelled, kFighter).has_value());
	std::vector<EventType> tail;
	for (const Event& e : events) {
		if (e.type == EventType::ActionStarted || e.type == EventType::StatusRemoved || e.type == EventType::RoundEnded) {
			tail.push_back(e.type);
		}
	}
	CHECK(tail.size() >= 2 && tail[tail.size() - 2] == EventType::StatusRemoved && tail.back() == EventType::RoundEnded);
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

// A blocked technique costs its points and fails; skills still work. Without the points it fails for free.
TEST(silenceBlocksTechniquesButNotSkills) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 1);
	addStatus(battle, kMystic, "silence", 0);
	addStatus(battle, kFighter, "silence", 0);
	addStatus(battle, kMedic, "silence", 0);
	battle.combatant(kMedic).tp = 2;
	const int tp = battle.combatant(kMystic).tp;
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
	battle.setCommand(kFighter, Command::ability("focus_shot", kFirstEnemy));
	battle.setCommand(kMedic, Command::ability("mend", kMedic));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> failed = firstBy(events, EventType::ActionCancelled, kMystic);
	const std::optional<Event> spent = firstBy(events, EventType::ResourceSpent, kMystic);
	CHECK(failed && failed->cancel == CancelReason::Silenced);
	CHECK(spent && spent->amount == 3);
	CHECK(!firstBy(events, EventType::ActionStarted, kMystic).has_value());
	CHECK_EQ(battle.combatant(kMystic).tp, tp - 3);
	const std::optional<Event> broke = firstBy(events, EventType::ActionCancelled, kMedic);
	CHECK(broke && broke->cancel == CancelReason::NotEnoughTp);
	CHECK_EQ(battle.combatant(kMedic).tp, 2);
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
		db.statuses["silence"].duration = 3;
		db.statuses["silence"].stacking = StatusStacking::Extend;
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
		db.statuses["silence"].duration = 3;
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

	// Paralysis counts too, alone or with the fallen.
	Battle stiff = makeBattle(data(), {"gel_mite"}, 1);
	for (int i = 0; i < stiff.partyCount(); ++i) {
		addStatus(stiff, i, "paralysis", 0);
	}
	stiff.combatant(kMedic).hp = 0;
	stiff.resolveRound();
	CHECK(stiff.outcome() == Outcome::Defeat);

	// On an enemy it does not: only defeat takes an enemy out.
	Battle held = makeBattle(data(), {"rust_hound"}, 1);
	addStatus(held, kFirstEnemy, "gear_lock", 0);
	partyDefends(held);
	held.resolveRound();
	CHECK(held.outcome() == Outcome::Ongoing);

	// A sleeping party plays on: only the enemies act.
	Database db = data();
	db.statuses["sleep"].recoverChance = 0;
	Battle asleep = makeBattle(db, {"gel_mite"}, 1);
	for (int i = 0; i < asleep.partyCount(); ++i) {
		asleep.combatant(i).stats.maxHp = 999;
		asleep.combatant(i).hp = 999;
		addStatus(asleep, i, "sleep", 0);
	}
	for (int round = 0; round < 3; ++round) {
		const std::vector<Event> events = asleep.resolveRound();
		CHECK(asleep.outcome() == Outcome::Ongoing);
		for (const Event& e : ofType(events, EventType::ActionStarted)) {
			CHECK(!asleep.isParty(e.actor));
		}
		CHECK(!ofType(events, EventType::ActionStarted).empty());
	}
}

// Sleep holds agility at 1 and paralysis agility and dexterity, until cured or recovered.
TEST(statusesHoldStatsAtOneWhileTheyLast) {
	Database db = data();
	db.statuses["sleep"].recoverChance = 0;
	Battle battle = makeBattle(db, {"brass_warden"}, 1);
	Combatant& mystic = battle.combatant(kMystic);
	const int agility = mystic.stats.agility;
	const int dexterity = mystic.stats.dexterity;
	addStatus(battle, kMystic, "sleep", 0);
	addStatus(battle, kScout, "paralysis", 0);
	othersDefend(battle);
	battle.resolveRound();
	CHECK_EQ(mystic.effective(Stat::Agility), 1);
	CHECK_EQ(mystic.effective(Stat::Dexterity), dexterity);
	CHECK_EQ(battle.combatant(kScout).effective(Stat::Agility), 1);
	CHECK_EQ(battle.combatant(kScout).effective(Stat::Dexterity), 1);

	battle.setCommand(kFighter, Command::item("wake_chime", -1));
	othersDefend(battle);
	battle.resolveRound();
	CHECK_EQ(mystic.effective(Stat::Agility), agility);
	CHECK_EQ(battle.combatant(kScout).effective(Stat::Dexterity), battle.combatant(kScout).stats.dexterity);
}

TEST(receivingParalysisRemovesSleep) {
	Database db = withInflictingAttack("paralysis");
	Battle battle = makeBattle(db, {"rust_hound"}, 1);
	addStatus(battle, kFirstEnemy, "sleep", 0);
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> removed = ofType(events, EventType::StatusRemoved);
	CHECK(!removed.empty() && removed[0].status == "sleep" && removed[0].statusEnd == StatusEnd::Cured && removed[0].target == kFirstEnemy);
	const std::vector<Event> applied = ofType(events, EventType::StatusApplied);
	CHECK(applied.size() == 1 && applied[0].status == "paralysis");
	CHECK(!battle.combatant(kFirstEnemy).hasStatus("sleep"));
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
	// Running out leaves agility and dexterity at 1 for the rest of the battle, as in the original.
	CHECK_EQ(ofType(events, EventType::StatModified).size(), std::size_t(2));
	for (int round = 0; round < 2; ++round) {
		othersDefend(battle);
		battle.resolveRound();
	}
	CHECK_EQ(battle.combatant(kFirstEnemy).effective(Stat::Agility), 1);
	CHECK_EQ(battle.combatant(kFirstEnemy).effective(Stat::Dexterity), 1);

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
