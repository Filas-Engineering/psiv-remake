#include "fixtures.h"
#include "rules/formulas.h"
#include "test.h"

#include <algorithm>

using namespace rules;
using namespace fixtures;

namespace {

std::optional<Event> enemyAction(const std::vector<Event>& events, int enemy) {
	return firstBy(events, EventType::ActionStarted, enemy);
}

AiOverride when(AiConditionKind kind, const char* ability, AiTargetRule rule = AiTargetRule::Default, const char* status = "") {
	return {kind, ability, {rule, status}};
}

// Eight slots of the basic attack, then the given overrides.
void biteOnly(Database& db, const char* enemy, std::vector<AiOverride> overrides = {}) {
	EnemyDef& def = db.enemies[enemy];
	def.slots.assign(static_cast<std::size_t>(kAiSlots), def.basicAttack);
	def.overrides = std::move(overrides);
}

void sturdy(Battle& battle, int index) {
	battle.combatant(index).stats.maxHp = 5000;
	battle.combatant(index).hp = 5000;
}

void fell(Battle& battle, int index) {
	battle.combatant(index).hp = 0;
	battle.combatant(index).statuses = {{kDefeatedStatus, 0}};
}

}

// The draw comes right after the initiative rolls, one per enemy in index order: party members not defeated, in
// party order, weighted count, count - 1, ... 1.
TEST(enemyTargetsAreDrawnByPartyOrderWhenTheOrderIsResolved) {
	Database db = data();
	biteOnly(db, "gel_mite");
	for (std::uint64_t seed = 1; seed <= 200; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite", "gel_mite", "gel_mite"}, seed);
		for (int i = 0; i < kFirstEnemy; ++i) {
			sturdy(battle, i);
		}
		const int down = static_cast<int>(seed % 3);
		for (int i = 0; i < down; ++i) {
			fell(battle, kScout + i);
		}
		partyDefends(battle);
		std::vector<int> living;
		for (int i = 0; i < kFirstEnemy; ++i) {
			if (battle.combatant(i).alive()) {
				living.push_back(i);
			}
		}
		Rng rng(seed);
		const int acting = static_cast<int>(living.size()) + 3;
		int highest = 0;
		for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
			if (battle.combatant(i).alive()) {
				highest = std::max(highest, battle.combatant(i).effective(Stat::Agility));
			}
		}
		for (int k = 0; k < acting; ++k) {
			rng.range(0, formulas::initiativeSpread(highest));
		}
		const int count = static_cast<int>(living.size());
		const std::vector<Event> events = battle.resolveRound();
		for (int enemy = kFirstEnemy; enemy < kFirstEnemy + 3; ++enemy) {
			int roll = rng.range(0, count * (count + 1) / 2 - 1);
			int expected = living.back();
			for (int k = 0; k < count; ++k) {
				roll -= count - k;
				if (roll < 0) {
					expected = living[static_cast<std::size_t>(k)];
					break;
				}
			}
			const std::optional<Event> action = enemyAction(events, enemy);
			CHECK(action && action->target == expected);
		}
	}
}

// Defeated members drop out, so the weights follow position among those standing: 2:1, 3:2:1, 5:4:3:2:1.
TEST(enemyTargetWeightsFollowPartyOrder) {
	Database db = data();
	biteOnly(db, "gel_mite");
	const std::vector<std::vector<int>> parties = {{kMystic, kEngineer}, {kMystic, kMedic, kEngineer}, {kFighter, kMystic, kScout, kMedic, kEngineer}};
	for (const std::vector<int>& standing : parties) {
		std::vector<int> hits(kFirstEnemy, 0);
		const int trials = 6000;
		for (int seed = 1; seed <= trials; ++seed) {
			Battle battle = makeBattle(db, {"gel_mite"}, static_cast<std::uint64_t>(seed) * 31 + standing.size());
			for (int i = 0; i < kFirstEnemy; ++i) {
				if (std::find(standing.begin(), standing.end(), i) == standing.end()) {
					fell(battle, i);
				} else {
					sturdy(battle, i);
				}
			}
			partyDefends(battle);
			const std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
			if (action && action->target >= 0 && action->target < kFirstEnemy) {
				++hits[static_cast<std::size_t>(action->target)];
			}
		}
		const int count = static_cast<int>(standing.size());
		for (int k = 0; k < count; ++k) {
			const double share = 100.0 * hits[static_cast<std::size_t>(standing[static_cast<std::size_t>(k)])] / trials;
			const double expected = 100.0 * (count - k) / (count * (count + 1) / 2);
			// Within 2.5 points: over four standard deviations at 6000 trials.
			if (share < expected - 2.5 || share > expected + 2.5) {
				::test::fail(__FILE__, __LINE__, std::to_string(count) + " standing: position " + std::to_string(k) + " drew " + std::to_string(share) + "%, expected " + std::to_string(expected) + "%");
			}
		}
	}
}

TEST(anEnemyRedrawsWhenItsTargetFallsFirst) {
	Database db = data();
	biteOnly(db, "gel_mite");
	db.enemies["gel_mite"].stats.strength = 400;
	db.enemies["gel_mite"].stats.dexterity = 400;
	int redraws = 0;
	for (std::uint64_t seed = 1; seed <= 300; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite", "gel_mite"}, seed);
		for (int i = 0; i < kFirstEnemy; ++i) {
			battle.combatant(i).hp = 1;
		}
		partyDefends(battle);
		const std::vector<Event> events = battle.resolveRound();
		std::vector<bool> down(kFirstEnemy, false);
		for (const Event& e : events) {
			if (e.type == EventType::Retargeted && e.actor >= kFirstEnemy) {
				CHECK(down[static_cast<std::size_t>(e.previousTarget)]);
				++redraws;
			}
			if (e.type == EventType::ActionStarted && e.actor >= kFirstEnemy) {
				CHECK(!down[static_cast<std::size_t>(e.target)]);
			}
			if (e.type == EventType::CombatantDefeated && e.target < kFirstEnemy) {
				down[static_cast<std::size_t>(e.target)] = true;
			}
		}
	}
	CHECK(redraws > 20);
}

// Slot 0 holds the only venom spit. After any enemy draws it, the next draw, by any enemy, cannot; after a bite slot
// it comes up one time in seven.
TEST(aiSlotsNeverRepeatTheLastIndexDrawn) {
	Database db = data();
	EnemyDef& hound = db.enemies["rust_hound"];
	hound.slots = {"venom_spit", "bite", "bite", "bite", "bite", "bite", "bite", "bite"};
	hound.overrides.clear();
	hound.stats.maxTp = 999;
	int firstSpits = 0;
	int afterSpit = 0;
	int afterBite = 0;
	int spitAfterBite = 0;
	const int battles = 1500;
	for (std::uint64_t seed = 1; seed <= static_cast<std::uint64_t>(battles); ++seed) {
		Battle battle = makeBattle(db, {"rust_hound", "rust_hound"}, seed);
		for (int i = 0; i < kFirstEnemy; ++i) {
			sturdy(battle, i);
		}
		sturdy(battle, kFirstEnemy);
		sturdy(battle, kFirstEnemy + 1);
		std::string previous;
		for (int round = 0; round < 4; ++round) {
			partyDefends(battle);
			for (const Event& e : battle.resolveRound()) {
				if (e.type != EventType::ActionStarted || e.actor < kFirstEnemy) {
					continue;
				}
				if (previous.empty()) {
					firstSpits += e.ability == "venom_spit" ? 1 : 0;
				} else if (previous == "venom_spit") {
					++afterSpit;
					CHECK(e.ability != "venom_spit");
				} else {
					++afterBite;
					spitAfterBite += e.ability == "venom_spit" ? 1 : 0;
				}
				previous = e.ability;
			}
		}
	}
	CHECK(afterSpit > 500);
	const double rate = 100.0 * spitAfterBite / afterBite;
	// 1 in 7 is 14.29%; 1 in 8 would be 12.5%.
	CHECK(rate > 13.4 && rate < 15.2);
	const double first = 100.0 * firstSpits / battles;
	CHECK(first > 10.0 && first < 15.0);
}

TEST(theFirstOverrideThatHoldsReplacesTheSlot) {
	Database db = data();
	biteOnly(db, "rust_hound", {when(AiConditionKind::SelfHpHalf, "venom_spit")});
	Battle battle = makeBattle(db, {"rust_hound"}, 1);
	const int half = battle.combatant(kFirstEnemy).stats.maxHp / 2;
	battle.combatant(kFirstEnemy).hp = half + 1;
	partyDefends(battle);
	std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->command == CommandKind::Attack && action->ability == "bite");

	battle.combatant(kFirstEnemy).hp = half;
	partyDefends(battle);
	action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "venom_spit");

	// Earlier overrides win.
	db.enemies["rust_hound"].overrides = {when(AiConditionKind::LastStanding, "bite"), when(AiConditionKind::SelfHpHalf, "venom_spit")};
	Battle lone = makeBattle(db, {"rust_hound"}, 1);
	lone.combatant(kFirstEnemy).hp = 1;
	partyDefends(lone);
	action = enemyAction(lone.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "bite");
}

TEST(theQuarterOverrideFiresOncePerBattle) {
	Database db = data();
	biteOnly(db, "rust_hound", {when(AiConditionKind::SelfHpQuarterOnce, "venom_spit")});
	db.enemies["rust_hound"].stats.agility = 0;
	Battle battle = makeBattle(db, {"rust_hound", "rust_hound"}, 4);
	for (int i = 0; i < kFirstEnemy; ++i) {
		sturdy(battle, i);
	}
	const int quarter = battle.combatant(kFirstEnemy).stats.maxHp / 4;
	battle.combatant(kFirstEnemy).hp = quarter;
	battle.combatant(kFirstEnemy + 1).hp = quarter;
	int spits = 0;
	for (int round = 0; round < 3; ++round) {
		partyDefends(battle);
		for (const Event& e : ofType(battle.resolveRound(), EventType::ActionStarted)) {
			spits += e.actor >= kFirstEnemy && e.ability == "venom_spit" ? 1 : 0;
		}
	}
	CHECK_EQ(spits, 1);
}

TEST(alliesAndTheLastStandingEnemyAreConditions) {
	Database db = data();
	biteOnly(db, "bog_wisp", {when(AiConditionKind::AnyAllyHpHalf, "patch")});
	Battle battle = makeBattle(db, {"bog_wisp", "gel_mite"}, 1);
	partyDefends(battle);
	std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "bite");
	battle.combatant(kFirstEnemy + 1).hp = battle.combatant(kFirstEnemy + 1).stats.maxHp / 2;
	partyDefends(battle);
	action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "patch" && action->target == kFirstEnemy + 1);

	biteOnly(db, "bog_wisp", {when(AiConditionKind::LastStanding, "lullaby")});
	Battle pair = makeBattle(db, {"bog_wisp", "gel_mite"}, 2);
	partyDefends(pair);
	action = enemyAction(pair.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "bite");
	fell(pair, kFirstEnemy + 1);
	partyDefends(pair);
	action = enemyAction(pair.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "lullaby");
}

// Whatever hit it sets its flag; the override that reads one clears all three.
TEST(hitOverridesReactToWhatLandedAndConsumeTheFlags) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 2000;
	biteOnly(db, "brass_warden", {when(AiConditionKind::HitByTechnique, "steam_vent"), when(AiConditionKind::HitByAttack, "gear_slam")});
	// The warden acts first, so what lands in a round is read in the next one.
	db.enemies["brass_warden"].stats.agility = 900;
	Battle battle = makeBattle(db, {"brass_warden"}, 3);
	sturdy(battle, kFirstEnemy);
	for (int i = 0; i < kFirstEnemy; ++i) {
		sturdy(battle, i);
	}
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
	partyDefends(battle);
	battle.resolveRound();
	const Combatant& warden = battle.combatant(kFirstEnemy);
	CHECK(warden.hitByAttack && warden.hitByAbility && warden.hitByTechnique);

	partyDefends(battle);
	std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "steam_vent");
	CHECK(!warden.hitByAttack && !warden.hitByAbility && !warden.hitByTechnique);

	// Nothing hit it since, so the attack flag is gone with the others.
	partyDefends(battle);
	action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "bite");

	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	partyDefends(battle);
	battle.resolveRound();
	CHECK(warden.hitByAttack && !warden.hitByAbility && !warden.hitByTechnique);
	partyDefends(battle);
	action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "gear_slam");

	biteOnly(db, "brass_warden", {when(AiConditionKind::HitByAbility, "steam_vent")});
	Battle flask = makeBattle(db, {"brass_warden"}, 3);
	sturdy(flask, kFirstEnemy);
	flask.setCommand(kFighter, Command::item("spark_flask", kFirstEnemy));
	partyDefends(flask);
	flask.resolveRound();
	CHECK(flask.combatant(kFirstEnemy).hitByAbility && !flask.combatant(kFirstEnemy).hitByTechnique && !flask.combatant(kFirstEnemy).hitByAttack);
}

TEST(blockedTechniquesAreACondition) {
	Database db = data();
	biteOnly(db, "bog_wisp", {when(AiConditionKind::TechniquesBlocked, "bite")});
	db.enemies["bog_wisp"].slots.assign(static_cast<std::size_t>(kAiSlots), "lullaby");
	Battle battle = makeBattle(db, {"bog_wisp"}, 2);
	addStatus(battle, kFirstEnemy, "silence", 0);
	partyDefends(battle);
	const std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "bite");
}

TEST(anActionTheEnemyCannotPayForBecomesTheBasicAttack) {
	Database db = data();
	db.enemies["rust_hound"].slots.assign(static_cast<std::size_t>(kAiSlots), "venom_spit");
	db.enemies["rust_hound"].overrides.clear();
	Battle battle = makeBattle(db, {"rust_hound"}, 1);
	battle.combatant(kFirstEnemy).tp = 0;
	partyDefends(battle);
	const std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->command == CommandKind::Attack && action->ability == "bite");
}

TEST(aiRevivesAFallenAllyFromASlot) {
	Database db = data();
	db.enemies["brass_warden"].slots.assign(static_cast<std::size_t>(kAiSlots), "rouse");
	db.enemies["brass_warden"].overrides.clear();
	Battle battle = makeBattle(db, {"brass_warden", "gel_mite"}, 1);
	partyDefends(battle);
	// Nobody to revive: the slot finds no target and the warden bites.
	std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "bite");

	fell(battle, kFirstEnemy + 1);
	partyDefends(battle);
	const std::vector<Event> events = battle.resolveRound();
	action = enemyAction(events, kFirstEnemy);
	CHECK(action && action->ability == "rouse" && action->target == kFirstEnemy + 1);
	CHECK(battle.combatant(kFirstEnemy + 1).alive());
}

TEST(overrideTargetRulesPickByHitPoints) {
	Database db = data();
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		biteOnly(db, "rust_hound", {when(AiConditionKind::LastStanding, "bite", AiTargetRule::LowestHp)});
		Battle low = makeBattle(db, {"rust_hound"}, seed);
		low.combatant(kScout).hp = 7;
		partyDefends(low);
		const std::optional<Event> lowest = enemyAction(low.resolveRound(), kFirstEnemy);
		CHECK(lowest && lowest->target == kScout);

		biteOnly(db, "rust_hound", {when(AiConditionKind::LastStanding, "bite", AiTargetRule::HighestHp)});
		Battle high = makeBattle(db, {"rust_hound"}, seed);
		partyDefends(high);
		const std::optional<Event> highest = enemyAction(high.resolveRound(), kFirstEnemy);
		// The engineer starts with the most hit points (75).
		CHECK(highest && highest->target == kEngineer);
	}
}

TEST(overrideStatusRulesFilterAndQualify) {
	Database db = data();
	biteOnly(db, "rust_hound", {when(AiConditionKind::LastStanding, "venom_spit", AiTargetRule::HasStatus, "sleep")});
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		// Nobody asleep: the override has no target, so the slot's bite stands.
		Battle awake = makeBattle(db, {"rust_hound"}, seed);
		partyDefends(awake);
		const std::optional<Event> bite = enemyAction(awake.resolveRound(), kFirstEnemy);
		CHECK(bite && bite->ability == "bite");

		Battle dozing = makeBattle(db, {"rust_hound"}, seed);
		addStatus(dozing, kMedic, "sleep", 0);
		partyDefends(dozing);
		const std::optional<Event> spit = enemyAction(dozing.resolveRound(), kFirstEnemy);
		CHECK(spit && spit->ability == "venom_spit" && spit->target == kMedic);
	}

	biteOnly(db, "rust_hound", {when(AiConditionKind::LastStanding, "bite", AiTargetRule::LacksStatus, "poison")});
	Battle battle = makeBattle(db, {"rust_hound"}, 3);
	for (int i : {kFighter, kMystic, kMedic, kEngineer}) {
		addStatus(battle, i, "poison", 0);
	}
	partyDefends(battle);
	const std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->target == kScout);
}

TEST(lastAttackerRuleAnswersWhoeverHitIt) {
	Database db = data();
	biteOnly(db, "brass_warden", {when(AiConditionKind::LastStanding, "gear_slam", AiTargetRule::LastAttacker)});
	db.characters["scout"].base.dexterity = 500;
	db.enemies["brass_warden"].stats.agility = 1;
	Battle battle = makeBattle(db, {"brass_warden"}, 2);
	battle.combatant(kFirstEnemy).hp = 2000;
	// The scout acts first and hits it, so the warden answers the scout.
	battle.setCommand(kScout, Command::attack(kFirstEnemy));
	partyDefends(battle);
	const std::optional<Event> first = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK_EQ(battle.combatant(kFirstEnemy).lastAttacker, kScout);
	CHECK(first && first->ability == "gear_slam" && first->target == kScout);

	Battle untouched = makeBattle(db, {"brass_warden"}, 2);
	partyDefends(untouched);
	const std::optional<Event> bite = enemyAction(untouched.resolveRound(), kFirstEnemy);
	CHECK(bite && bite->ability == "bite");
}
