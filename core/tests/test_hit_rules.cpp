#include "fixtures.h"
#include "rules/formulas.h"
#include "test.h"

#include <limits>

using namespace rules;
using namespace fixtures;

TEST(statusFormulas) {
	CHECK_EQ(formulas::statusScore(10, 14, 20, 0), 16);
	CHECK_EQ(formulas::statusScore(0, 3, 30, 0), -108);
	CHECK_EQ(formulas::statusScore(63, 10, 8, 50), 130);
	CHECK_EQ(formulas::statusScore(63, 10, 8, 75), 65);
	// The factor rounds down: from 76 percent resistance on, the score is 0 whatever the roll.
	CHECK_EQ(formulas::statusScore(63, 10, 8, 76), 0);
	CHECK_EQ(formulas::statusScore(63, 10, 8, 99), 0);
}

TEST(criticalHitsComeFromDataInputs) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	db.characters["fighter"].base.critical = 100;
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite"}, seed);
		battle.combatant(kFirstEnemy).hp = 500;
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::optional<Event> critical = firstBy(events, EventType::Critical, kFighter);
		CHECK(critical && critical->target == kFirstEnemy);
		const std::vector<Event> damage = ofType(events, EventType::DamageDealt);
		const int strength = battle.combatant(kFighter).stats.strength;
		const int low = formulas::damage(strength, 3, formulas::criticalBonus(strength), 25, formulas::kSpreadMin);
		const int high = formulas::damage(strength, 3, formulas::criticalBonus(strength), 25, formulas::kSpreadMax);
		CHECK(!damage.empty() && damage[0].amount >= low && damage[0].amount <= high);
	}

	// The same odds never make a critical of an attack on a group or of a skill.
	db.characters["scout"].base.dexterity = 500;
	db.characters["scout"].base.critical = 100;
	for (std::uint64_t seed = 1; seed <= 30; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite", "gel_mite"}, seed);
		battle.combatant(kFirstEnemy).hp = 500;
		battle.combatant(kFirstEnemy + 1).hp = 500;
		battle.setCommand(kScout, Command::attack(kFirstEnemy));
		battle.setCommand(kFighter, Command::ability("focus_shot", kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		CHECK_EQ(ofType(events, EventType::DamageDealt).size(), std::size_t(3));
		CHECK(ofType(events, EventType::Critical).empty());
	}
}

TEST(evasionTurnsHitsIntoMisses) {
	Database db = data();
	db.enemies["gel_mite"].stats.evasion = 100;
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite"}, seed);
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		CHECK(firstBy(events, EventType::Missed, kFighter).has_value());
		CHECK(ofType(events, EventType::DamageDealt).empty());

		// Only basic attacks roll to hit.
		Battle skill = makeBattle(db, {"gel_mite"}, seed);
		skill.combatant(kFirstEnemy).hp = 500;
		skill.setCommand(kFighter, Command::ability("focus_shot", kFirstEnemy));
		othersDefend(skill);
		const std::vector<Event> landed = skill.resolveRound();
		CHECK(ofType(landed, EventType::Missed).empty());
		CHECK_EQ(ofType(landed, EventType::DamageDealt).size(), std::size_t(1));
	}
}

TEST(statusResistanceLowersTheChance) {
	// Hush (threshold 73) from the mystic (mental 17): the gel mite (mental 3) resists rolls 0 to 4, 5 in 64. The warden
	// (mental 10) resists silence by half, which halves the factor, so it resists rolls 0 to 29, 30 in 64.
	int plain = 0;
	int resisted = 0;
	for (std::uint64_t seed = 1; seed <= 300; ++seed) {
		Battle battle = makeBattle(data(), {"gel_mite"}, seed);
		battle.combatant(kFirstEnemy).hp = 500;
		battle.setCommand(kMystic, Command::ability("hush", kFirstEnemy));
		othersDefend(battle);
		plain += ofType(battle.resolveRound(), EventType::StatusApplied).size() == 1 ? 1 : 0;

		Battle warden = makeBattle(data(), {"brass_warden"}, seed);
		warden.setCommand(kMystic, Command::ability("hush", kFirstEnemy));
		othersDefend(warden);
		resisted += ofType(warden.resolveRound(), EventType::StatusApplied).size() == 1 ? 1 : 0;
	}
	CHECK(plain > 255 && plain < 295);
	CHECK(resisted > 125 && resisted < 195);
}

TEST(statusImmunityNeverRolls) {
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		Battle battle = makeBattle(data(), {"brass_warden"}, seed);
		battle.setCommand(kEngineer, Command::ability("drowse", kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> resisted = ofType(battle.resolveRound(), EventType::StatusResisted);
		CHECK(resisted.size() == 1 && resisted[0].effectiveness == Effectiveness::Immune && resisted[0].status == "sleep");
		CHECK(!battle.combatant(kFirstEnemy).hasStatus("sleep"));
	}
}

// Drowse on the brass warden, which is immune to sleep, must leave the random stream as if nobody had cast it.
TEST(immuneTargetsTakeNoStatusRoll) {
	auto secondRound = [](std::uint64_t seed, bool cast, int resistance) {
		Battle battle = makeBattle(data(), {"brass_warden"}, seed);
		battle.combatant(kFirstEnemy).statusResist["sleep"] = resistance;
		battle.setCommand(kEngineer, cast ? Command::ability("drowse", kFirstEnemy) : Command::defend());
		othersDefend(battle);
		const std::vector<Event> first = battle.resolveRound();
		const std::vector<Event> resisted = ofType(first, EventType::StatusResisted);
		CHECK_EQ(resisted.size(), std::size_t(cast ? 1 : 0));
		CHECK(battle.combatant(kFirstEnemy).statuses.empty());
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		othersDefend(battle);
		std::vector<int> amounts;
		for (const Event& e : battle.resolveRound()) {
			if (e.type == EventType::DamageDealt || e.type == EventType::Missed) {
				amounts.push_back(e.type == EventType::Missed ? -1 : e.amount);
			}
		}
		return amounts;
	};
	int differed = 0;
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		const std::vector<int> untouched = secondRound(seed, false, 100);
		CHECK_EQ(secondRound(seed, true, 100), untouched);
		// At 99 percent the status is still always resisted, but after a roll.
		differed += secondRound(seed, true, 99) != untouched ? 1 : 0;
	}
	CHECK(differed > 0);
}

// From 76 percent resistance the score is 0, and at 75 it is the roll plus the mental difference, so these outcomes
// do not depend on the roll.
TEST(statusThresholdDecidesTheOutcome) {
	auto applied = [](int resistance, int threshold, int mentalOverMystic) {
		Database db = data();
		db.abilities["hush"].effects[0].threshold = threshold;
		int count = 0;
		for (std::uint64_t seed = 1; seed <= 8; ++seed) {
			Battle battle = makeBattle(db, {"rust_hound"}, seed);
			Combatant& hound = battle.combatant(kFirstEnemy);
			hound.statusResist["silence"] = resistance;
			hound.stats.mental = battle.combatant(kMystic).effective(Stat::Mental) + mentalOverMystic;
			battle.setCommand(kMystic, Command::ability("hush", kFirstEnemy));
			othersDefend(battle);
			count += ofType(battle.resolveRound(), EventType::StatusApplied).size() == 1 ? 1 : 0;
		}
		return count;
	};
	CHECK_EQ(applied(80, 0, 0), 0);
	CHECK_EQ(applied(80, -1, 0), 8);
	// Score from -63 to 0: never above 0, always above -64.
	CHECK_EQ(applied(75, 0, 63), 0);
	CHECK_EQ(applied(75, -64, 63), 8);
	// Score from 1 to 64.
	CHECK_EQ(applied(75, 0, -1), 8);
	CHECK_EQ(applied(75, 64, -1), 0);
}

TEST(equipmentResistanceProtectsTheWearer) {
	Database db = data();
	db.enemies["bog_wisp"].slots.assign(static_cast<std::size_t>(kAiSlots), "lullaby");
	db.enemies["bog_wisp"].overrides.clear();
	db.abilities["lullaby"].effects[0].threshold = std::numeric_limits<int>::min();
	Battle battle = makeBattle(db, {"bog_wisp"}, 1);
	partyDefends(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK_EQ(ofType(events, EventType::StatusApplied).size(), std::size_t(4));
	const std::vector<Event> resisted = ofType(events, EventType::StatusResisted);
	CHECK(resisted.size() == 1 && resisted[0].target == kMystic && resisted[0].effectiveness == Effectiveness::Immune);
}
