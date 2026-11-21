#include "fixtures.h"
#include "rules/formulas.h"
#include "rules/progression.h"
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

// A stat change sets the battle value to base plus or minus the user's stat: mental for a technique, strength for a
// skill, the power for an item. The latest change replaces the earlier one, and it lasts until the battle ends.
TEST(statChangesMoveFromBaseByTheUsersStat) {
	Database db = data();
	ItemDef draught;
	draught.id = "draught";
	draught.targeting = Targeting::OneAlly;
	draught.effects = {{EffectKind::ModifyStat, 7, Element::None, "", 0, Stat::Strength, true}};
	db.items["draught"] = draught;
	Battle battle = makeBattle(db, {"brass_warden"}, 3, Inventory{{"draught", 1}});
	const int strength = battle.combatant(kMystic).stats.strength;
	const int agility = battle.combatant(kScout).stats.agility;
	battle.setCommand(kFighter, Command::ability("rally", -1));
	othersDefend(battle);
	CHECK_EQ(ofType(battle.resolveRound(), EventType::StatModified).size(), std::size_t(5));
	const int fighterMental = battle.combatant(kFighter).effective(Stat::Mental);
	CHECK_EQ(battle.combatant(kMystic).effective(Stat::Strength), strength + fighterMental);
	// A skill moves the stat by the user's strength, the rally included.
	battle.setCommand(kScout, Command::ability("overclock", kScout));
	othersDefend(battle);
	battle.resolveRound();
	CHECK_EQ(battle.combatant(kScout).effective(Stat::Agility), agility + battle.combatant(kScout).effective(Stat::Strength));

	for (int round = 0; round < 6; ++round) {
		othersDefend(battle);
		battle.resolveRound();
	}
	CHECK_EQ(battle.combatant(kMystic).effective(Stat::Strength), strength + fighterMental);

	// The item's power replaces the rally's amount rather than adding to it.
	battle.setCommand(kMedic, Command::item("draught", kMystic));
	othersDefend(battle);
	battle.resolveRound();
	CHECK_EQ(battle.combatant(kMystic).effective(Stat::Strength), strength + 7);
}

TEST(loweringFloorsAtOneForAgilityAndDexterityAndZeroOtherwise) {
	Database db = data();
	db.characters["scout"].base.mental = 500;
	AbilityDef& weaken = db.abilities["weaken"];
	weaken.effects = {{EffectKind::ModifyStat, 0, Element::None, "", 0, Stat::Defense, false}, {EffectKind::ModifyStat, 0, Element::None, "", 0, Stat::Agility, false}};
	Battle battle = makeBattle(db, {"brass_warden"}, 3);
	battle.combatant(kFirstEnemy).hp = 3000;
	battle.setCommand(kScout, Command::ability("weaken", kFirstEnemy));
	othersDefend(battle);
	battle.resolveRound();
	CHECK_EQ(battle.combatant(kFirstEnemy).effective(Stat::Defense), 0);
	CHECK_EQ(battle.combatant(kFirstEnemy).effective(Stat::Agility), 1);
}

// Lowering a foe rolls like a status against the effect's threshold; raising never rolls.
TEST(loweringAFoeRollsLikeAStatus) {
	const Database& db = data();
	int landed = 0;
	int expected = 0;
	const int trials = 3000;
	for (int seed = 1; seed <= trials; ++seed) {
		Battle battle = makeBattle(db, {"brass_warden"}, static_cast<std::uint64_t>(seed));
		battle.combatant(kFirstEnemy).hp = 3000;
		battle.setCommand(kScout, Command::ability("weaken", kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		landed += ofType(events, EventType::StatModified).size() == 1 ? 1 : 0;
		for (const Event& e : ofType(events, EventType::StatusResisted)) {
			CHECK(e.stat == Stat::Defense && e.message == "battle.stat_resisted" && e.status.empty());
		}
	}
	const int mental = makeBattle(db, {"brass_warden"}, 1).combatant(kScout).effective(Stat::Mental);
	const int targetMental = db.enemies.at("brass_warden").stats.mental;
	for (int roll = 0; roll <= formulas::kChanceRollMax; ++roll) {
		expected += formulas::statusScore(roll, mental, targetMental, 0) > db.abilities.at("weaken").effects[0].threshold ? 1 : 0;
	}
	const double rate = 100.0 * landed / trials;
	const double chance = 100.0 * expected / (formulas::kChanceRollMax + 1);
	CHECK(rate > chance - 3 && rate < chance + 3);
	CHECK(chance > 5 && chance < 95);
}

// rules.json `affinity_divisor`: the affinity step divides by it, rounding down once, before defense comes off.
TEST(theAffinityDivisorScalesDamageAndDefend) {
	CHECK_EQ(formulas::damage(26, 4, 13, 200, formulas::kSpreadMean, 200), formulas::damage(26, 4, 13, 100, formulas::kSpreadMean));
	CHECK_EQ(formulas::damage(26, 4, 13, 100, formulas::kSpreadMean, 200), 35);
	// 78 * 50 / 200 rounds down once to 19, then the defense comes off.
	CHECK_EQ(formulas::damage(26, 4, 13, 50, formulas::kSpreadMean, 200), 15);
	Database db = data();
	db.battleRules.affinityDivisor = 200;
	db.characters["fighter"].base.strength = 200;
	db.characters["fighter"].base.dexterity = 500;
	for (bool defending : {false, true}) {
		Battle battle = makeBattle(db, {"gel_mite"}, 3);
		battle.combatant(kFirstEnemy).hp = 5000;
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		battle.setCommand(kFirstEnemy, defending ? Command::defend() : Command::attack(kEngineer));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::optional<Event> hit = firstBy(events, EventType::DamageDealt, kFighter);
		CHECK(hit.has_value());
		const bool critical = std::any_of(events.begin(), events.end(), [](const Event& c) { return c.type == EventType::Critical; });
		const int strength = battle.combatant(kFighter).stats.strength;
		const int bonus = critical ? formulas::criticalBonus(strength) : 0;
		const int defense = battle.combatant(kFirstEnemy).effective(Stat::Defense);
		const int affinity = defending ? 50 : 200;
		CHECK(hit && hit->amount >= formulas::damage(strength, defense, bonus, affinity, formulas::kSpreadMin, 200));
		CHECK(hit && hit->amount <= formulas::damage(strength, defense, bonus, affinity, formulas::kSpreadMax, 200));
	}
}

// rules.json `magic_defense`: elemental damage subtracts magic defense in place of mental; armor adds to it.
TEST(magicDefenseStandsInForMentalWhenTheRulesSaySo) {
	Database db = data();
	PartyMember fighter = makePartyMember(db, db.characters.at("fighter"), 1);
	EquipmentBag bag = {{"ward_mantle", 1}};
	CHECK(equip(fighter, db, bag, "ward_mantle").empty());
	CHECK_EQ(memberStats(db, fighter).magicDefense, 8);
	db.characters["scout"].base.mental = 200;
	for (bool magicDefense : {false, true}) {
		db.battleRules.magicDefense = magicDefense;
		Battle battle = makeBattle(db, {"gloam_reaper"}, 5);
		battle.combatant(kFirstEnemy).hp = 5000;
		battle.combatant(kFirstEnemy).stats.mental = 150;
		battle.setCommand(kScout, Command::ability("gust", kFirstEnemy));
		battle.setCommand(kFirstEnemy, Command::defend());
		othersDefend(battle);
		const std::optional<Event> hit = firstBy(battle.resolveRound(), EventType::DamageDealt, kScout);
		const Combatant& reaper = battle.combatant(kFirstEnemy);
		const int guard = magicDefense ? reaper.stats.magicDefense : reaper.effective(Stat::Mental);
		const int attack = battle.combatant(kScout).effective(Stat::Mental);
		const int power = db.ability("gust")->effects[0].power;
		const int affinity = reaper.affinity(Element::Gale);
		CHECK(hit && hit->amount >= formulas::damage(attack, guard, power, affinity, formulas::kSpreadMin));
		CHECK(hit && hit->amount <= formulas::damage(attack, guard, power, affinity, formulas::kSpreadMax));
	}
}
