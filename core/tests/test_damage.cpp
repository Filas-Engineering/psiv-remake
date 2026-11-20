#include "fixtures.h"
#include "rules/formulas.h"
#include "test.h"

#include <algorithm>

using namespace rules;
using namespace fixtures;

TEST(damageMath) {
	CHECK_EQ(formulas::kSpreadMin, 8);
	CHECK_EQ(formulas::kSpreadMax, 120);
	CHECK_EQ(formulas::damage(14, 8, 0, 100, formulas::kSpreadMean), 20);
	CHECK_EQ(formulas::damage(14, 8, 5, 100, formulas::kSpreadMax), 42);
	CHECK_EQ(formulas::damage(14, 8, 0, 100, formulas::kSpreadMin), 7);
	// Heavy armor still lets one point through, and nothing deals more than 999.
	CHECK_EQ(formulas::damage(10, 50, 0, 100, formulas::kSpreadMean), 1);
	CHECK_EQ(formulas::damage(500, 0, 400, 200, formulas::kSpreadMax), 999);
	CHECK_EQ(formulas::criticalBonus(14), 3);
	CHECK_EQ(formulas::criticalBonus(3), 0);
	CHECK_EQ(formulas::chanceScore(10, 14, 20), 8);
	CHECK_EQ(formulas::chanceScore(0, 5, 30), -50);
	CHECK_EQ(formulas::chanceScore(formulas::kChanceRollMax, 10, 8), 130);
}

TEST(affinityAndHealingMath) {
	// Affinity scales the attack before defense comes off.
	CHECK_EQ(formulas::damage(26, 4, 13, 100, formulas::kSpreadMean), 74);
	CHECK_EQ(formulas::damage(26, 4, 13, 150, formulas::kSpreadMean), 113);
	CHECK_EQ(formulas::damage(26, 4, 13, 50, formulas::kSpreadMean), 35);
	// Defend's quarter.
	CHECK_EQ(formulas::damage(26, 4, 13, 25, formulas::kSpreadMean), 15);
	CHECK_EQ(formulas::damage(26, 4, 13, 0, formulas::kSpreadMean), 0);
	CHECK_EQ(formulas::damage(26, 4, 13, -50, formulas::kSpreadMean), 0);
	CHECK_EQ(formulas::healAmount(28, 12, formulas::kSpreadMean), 40);
	CHECK_EQ(formulas::healAmount(28, 12, formulas::kSpreadMin), 34);
	CHECK_EQ(formulas::healAmount(28, 12, formulas::kSpreadMax), 45);
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
		const int targetMental = battle.combatant(kFirstEnemy).effective(Stat::Mental);
		bool sawWeak = false;
		bool sawImmune = false;
		bool sawResisted = false;
		bool sawGaleWeak = false;
		for (const Event& e : ofType(events, EventType::DamageDealt)) {
			if (e.element == Element::Shock && e.target == kFirstEnemy) {
				sawWeak = e.effectiveness == Effectiveness::Weak;
				CHECK(e.amount >= formulas::damage(mental, targetMental, 13, 150, formulas::kSpreadMin));
				CHECK(e.amount <= formulas::damage(mental, targetMental, 13, 150, formulas::kSpreadMax));
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

// With attack stats this high the quartered and the full damage ranges do not overlap, so the bounds tell them apart.
TEST(defendQuartersOnlyPhysicalDamageWithoutAWeaponElement) {
	Database db = data();
	db.combos.clear();
	db.characters["fighter"].base.strength = 200;
	db.characters["fighter"].base.dexterity = 500;
	db.characters["mystic"].base.strength = 200;
	db.characters["mystic"].base.dexterity = 500;
	db.characters["scout"].base.mental = 200;
	CHECK(formulas::damage(200, 3, 0, 100, formulas::kSpreadMin) > formulas::damage(200, 3, formulas::criticalBonus(200), 25, formulas::kSpreadMax));
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		for (bool defending : {true, false}) {
			Battle battle = makeBattle(db, {"gel_mite"}, seed);
			battle.combatant(kFirstEnemy).hp = 5000;
			battle.setCommand(kFighter, Command::attack(kFirstEnemy));
			battle.setCommand(kMystic, Command::attack(kFirstEnemy));
			battle.setCommand(kScout, Command::ability("gust", kFirstEnemy));
			battle.setCommand(kFirstEnemy, defending ? Command::defend() : Command::attack(kEngineer));
			othersDefend(battle);
			const std::vector<Event> events = battle.resolveRound();
			const Combatant& target = battle.combatant(kFirstEnemy);
			int checked = 0;
			for (const Event& e : ofType(events, EventType::DamageDealt)) {
				if (e.target != kFirstEnemy) {
					continue;
				}
				++checked;
				const int strength = battle.combatant(e.actor).stats.strength;
				const bool critical = std::any_of(events.begin(), events.end(), [&](const Event& c) {
					return c.type == EventType::Critical && c.actor == e.actor;
				});
				const int bonus = critical ? formulas::criticalBonus(strength) : 0;
				int affinity = defending ? 25 : 100;
				int attack = strength;
				int defense = target.effective(Stat::Defense);
				if (e.actor == kMystic) {
					CHECK(e.element == Element::Heat);
					affinity = target.affinity(Element::Heat);
				} else if (e.actor == kScout) {
					CHECK(e.element == Element::Gale);
					affinity = target.affinity(Element::Gale);
					attack = battle.combatant(kScout).effective(Stat::Mental);
					defense = target.effective(Stat::Mental);
				}
				const int power = e.actor == kScout ? db.ability("gust")->effects[0].power : bonus;
				CHECK(e.amount >= formulas::damage(attack, defense, power, affinity, formulas::kSpreadMin));
				CHECK(e.amount <= formulas::damage(attack, defense, power, affinity, formulas::kSpreadMax));
			}
			CHECK_EQ(checked, 3);
		}
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
