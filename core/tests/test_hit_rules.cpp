#include "fixtures.h"
#include "rules/formulas.h"
#include "test.h"

using namespace rules;
using namespace fixtures;

TEST(statusFormulas) {
	CHECK_EQ(formulas::statusChance(70, 50), 35);
	CHECK_EQ(formulas::statusChance(70, 100), 0);
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
		const int low = formulas::defended(formulas::damage(strength, 3, formulas::criticalBonus(strength), 100, formulas::kSpreadMin));
		const int high = formulas::defended(formulas::damage(strength, 3, formulas::criticalBonus(strength), 100, formulas::kSpreadMax));
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
	// Hush lands 70% of the time; the warden resists silence by half.
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
	CHECK(plain > 180 && plain < 240);
	CHECK(resisted > 75 && resisted < 135);
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

TEST(equipmentResistanceProtectsTheWearer) {
	Database db = data();
	db.enemies["bog_wisp"].ai = {{"lullaby", 1, {}}};
	db.abilities["lullaby"].effects[0].chance = 100;
	Battle battle = makeBattle(db, {"bog_wisp"}, 1);
	partyDefends(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK_EQ(ofType(events, EventType::StatusApplied).size(), std::size_t(4));
	const std::vector<Event> resisted = ofType(events, EventType::StatusResisted);
	CHECK(resisted.size() == 1 && resisted[0].target == kMystic && resisted[0].effectiveness == Effectiveness::Immune);
}
