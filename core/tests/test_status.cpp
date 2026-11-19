#include "fixtures.h"
#include "rules/formulas.h"
#include "test.h"

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
		const std::vector<Event> expired = ofType(events, EventType::StatusExpired);
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
	Battle battle = makeBattle(data(), {"brass_warden"}, 2);
	addStatus(battle, kFighter, "sleep", 3);
	for (int round = 1; round <= 4; ++round) {
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::optional<Event> prevented = firstBy(events, EventType::ActionPrevented, kFighter);
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

TEST(damageWakesASleeper) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	Battle battle = makeBattle(db, {"brass_warden"}, 4);
	addStatus(battle, kFirstEnemy, "sleep", 3);
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	othersDefend(battle);
	bool woke = false;
	for (const Event& e : ofType(battle.resolveRound(), EventType::StatusExpired)) {
		woke = woke || (e.target == kFirstEnemy && e.statusEnd == StatusEnd::WokeOnDamage);
	}
	CHECK(woke);
	CHECK(!battle.combatant(kFirstEnemy).hasStatus("sleep"));
}

TEST(paralysisCanBeShakenOff) {
	Database db = data();
	db.statuses["paralysis"].recoverChance = 100;
	Battle battle = makeBattle(db, {"brass_warden"}, 1);
	addStatus(battle, kFighter, "paralysis", 3);
	battle.setCommand(kFighter, Command::defend());
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> expired = ofType(events, EventType::StatusExpired);
	CHECK(!expired.empty() && expired[0].statusEnd == StatusEnd::Recovered && expired[0].target == kFighter);
	CHECK(firstBy(events, EventType::ActionStarted, kFighter).has_value());

	db.statuses["paralysis"].recoverChance = 0;
	Battle stuck = makeBattle(db, {"brass_warden"}, 1);
	addStatus(stuck, kFighter, "paralysis", 3);
	othersDefend(stuck);
	CHECK(firstBy(stuck.resolveRound(), EventType::ActionPrevented, kFighter).has_value());
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
	const std::optional<Event> failed = firstBy(events, EventType::ActionFailed, kMystic);
	CHECK(failed && failed->fail == FailReason::Silenced);
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
	CHECK(ofType(events, EventType::StatusCured).size() >= 2);
}
