#include "fixtures.h"
#include "rules/formulas.h"
#include "test.h"

using namespace rules;
using namespace fixtures;

TEST(physicalDamageMath) {
	CHECK_EQ(formulas::physicalDamage(14, 8, 100, 100), 20);
	CHECK_EQ(formulas::physicalDamage(14, 8, 180, 110), 39);
	CHECK_EQ(formulas::physicalDamage(14, 8, 100, 90), 18);
	// Heavy armor still lets a quarter of strength through.
	CHECK_EQ(formulas::physicalDamage(10, 50, 100, 100), 2);
	CHECK_EQ(formulas::physicalDamage(1, 50, 10, 90), 1);
	CHECK_EQ(formulas::defended(21), 10);
	CHECK_EQ(formulas::hitChance(10, 8, 0), 87);
	CHECK_EQ(formulas::hitChance(500, 8, 0), 100);
	CHECK_EQ(formulas::hitChance(1, 300, 0), 40);
	// Evasion scales whatever chance is left after the clamp.
	CHECK_EQ(formulas::hitChance(10, 8, 10), 78);
	CHECK_EQ(formulas::hitChance(500, 8, 100), 0);
}

TEST(elementalDamageAndAffinityMath) {
	CHECK_EQ(formulas::elementalDamage(26, 14, 4, 100, 100), 38);
	CHECK_EQ(formulas::elementalDamage(26, 14, 4, 150, 100), 57);
	CHECK_EQ(formulas::elementalDamage(26, 14, 4, 50, 100), 19);
	CHECK_EQ(formulas::elementalDamage(26, 14, 4, 0, 100), 0);
	// A resistant, high-mental target still takes half the base power.
	CHECK_EQ(formulas::elementalDamage(20, 1, 100, 100, 100), 10);
	CHECK_EQ(formulas::healAmount(28, 12), 34);
	CHECK_EQ(formulas::reviveHp(42, 50), 21);
	CHECK_EQ(formulas::modifiedStat(14, 30), 18);
	CHECK_EQ(formulas::modifiedStat(10, -300), 5);
}

TEST(battleAppliesWeaknessResistanceAndImmunity) {
	// Without recipes, so spark and gust never merge into a combo when they land back to back.
	Database db = data();
	db.combos.clear();
	for (std::uint64_t seed = 1; seed <= 5; ++seed) {
		Battle battle = makeBattle(db, {"rust_hound", "bog_wisp", "brass_warden"}, seed);
		// Room for every hit, so no damage event is capped by remaining hit points.
		for (int enemy = kFirstEnemy; enemy < kFirstEnemy + 3; ++enemy) {
			battle.combatant(enemy).hp = 500;
		}
		battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
		battle.setCommand(kScout, Command::ability("gust", kFirstEnemy + 1));
		battle.setCommand(kEngineer, Command::ability("flare_burst", -1));
		for (int enemy = kFirstEnemy; enemy < kFirstEnemy + 3; ++enemy) {
			battle.setCommand(enemy, Command::attack(kFighter));
		}
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();

		const int mental = battle.combatant(kMystic).stats.mental;
		bool sawWeak = false;
		bool sawImmune = false;
		bool sawResisted = false;
		bool sawGaleWeak = false;
		for (const Event& e : ofType(events, EventType::DamageDealt)) {
			if (e.element == Element::Shock && e.target == kFirstEnemy) {
				sawWeak = e.effectiveness == Effectiveness::Weak;
				CHECK(e.amount >= formulas::elementalDamage(26, mental, 4, 150, formulas::kVarianceMin));
				CHECK(e.amount <= formulas::elementalDamage(26, mental, 4, 150, formulas::kVarianceMax));
			}
			if (e.element == Element::Gale && e.target == kFirstEnemy + 1) {
				sawGaleWeak = e.effectiveness == Effectiveness::Weak;
			}
			if (e.element == Element::Heat && e.target == kFirstEnemy + 2) {
				sawResisted = e.effectiveness == Effectiveness::Resisted;
			}
		}
		CHECK(sawWeak);
		CHECK(sawGaleWeak);
		CHECK(sawResisted);

		Battle immune = makeBattle(db, {"bog_wisp"}, seed);
		immune.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
		othersDefend(immune);
		for (const Event& e : ofType(immune.resolveRound(), EventType::DamageDealt)) {
			if (e.element == Element::Shock) {
				sawImmune = e.effectiveness == Effectiveness::Immune && e.amount == 0;
			}
		}
		CHECK(sawImmune);
		CHECK_EQ(immune.combatant(kFirstEnemy).hp, immune.combatant(kFirstEnemy).stats.maxHp);
	}
}

TEST(defendingHalvesPhysicalDamage) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite"}, seed);
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> damage = ofType(battle.resolveRound(), EventType::DamageDealt);
		CHECK_EQ(damage.size(), std::size_t(1));
		const int strength = battle.combatant(kFighter).stats.strength;
		const int low = formulas::defended(formulas::physicalDamage(strength, 3, 100, formulas::kVarianceMin));
		const int high = formulas::defended(formulas::physicalDamage(strength, 3, 100, formulas::kVarianceMax));
		CHECK(!damage.empty() && damage[0].amount >= low && damage[0].amount <= high);
	}
}

TEST(statModifiersChangeEffectiveStatsAndExpire) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 3);
	const int base = battle.combatant(kMystic).stats.strength;
	battle.setCommand(kFighter, Command::ability("rally", -1));
	othersDefend(battle);
	const std::vector<Event> first = battle.resolveRound();
	CHECK_EQ(ofType(first, EventType::StatModified).size(), std::size_t(5));
	CHECK_EQ(battle.combatant(kMystic).effective(Stat::Strength), formulas::modifiedStat(base, 30));

	std::vector<Event> later;
	for (int round = 0; round < 2; ++round) {
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		later.insert(later.end(), events.begin(), events.end());
	}
	const std::vector<Event> expired = ofType(later, EventType::StatModifierExpired);
	CHECK(!expired.empty() && expired.front().round == 3);
	CHECK_EQ(battle.combatant(kMystic).effective(Stat::Strength), base);
}
