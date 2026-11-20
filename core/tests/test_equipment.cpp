#include "fixtures.h"
#include "rules/formulas.h"
#include "rules/progression.h"
#include "test.h"

#include <algorithm>

using namespace rules;
using namespace fixtures;

namespace {

bool knows(const std::vector<std::string>& abilities, const std::string& id) {
	return std::find(abilities.begin(), abilities.end(), id) != abilities.end();
}

}

TEST(equipmentAddsStatsAndAbilities) {
	const Database& db = data();
	const PartyMember fighter = makePartyMember(db, db.characters.at("fighter"), 1);
	const Stats stats = memberStats(db, fighter);
	CHECK_EQ(stats.strength, 14 + 4);
	CHECK_EQ(stats.defense, 8 + 2 + 3);
	CHECK_EQ(stats.maxHp, 60 + 10);
	CHECK_EQ(stats.critical, 4 + 4);
	CHECK_EQ(fighter.hp, 70);

	PartyMember medic = makePartyMember(db, db.characters.at("medic"), 1);
	CHECK(knows(knownAbilities(db, medic), "glint"));
	EquipmentBag bag;
	unequip(medic, db, bag, EquipSlot::Accessory);
	CHECK(!knows(knownAbilities(db, medic), "glint"));
	CHECK_EQ(bag.at("spark_ring"), 1);
}

TEST(equipRestrictionsArePerCharacter) {
	const Database& db = data();
	PartyMember mystic = makePartyMember(db, db.characters.at("mystic"), 1);
	EquipmentBag bag = db.startingEquipmentBag;
	const Message refused = equip(mystic, db, bag, "copper_blade");
	CHECK_EQ(refused.key, std::string("equip.wrong_category"));
	CHECK_EQ(refused.params, (std::vector<std::string>{"mystic", "copper_blade"}));
	CHECK_EQ(mystic.equipment.at(EquipSlot::Weapon), std::string("ember_rod"));
	CHECK_EQ(bag.at("copper_blade"), 1);
	CHECK(!canEquip(db, "medic", "rivet_plate").empty());
	CHECK(canEquip(db, "engineer", "rivet_plate").empty());
	CHECK_EQ(equip(mystic, db, bag, "no_such_thing").key, std::string("equip.unknown_equipment"));

	// Taking off armor clamps hit points to the new maximum.
	PartyMember fighter = makePartyMember(db, db.characters.at("fighter"), 1);
	unequip(fighter, db, bag, EquipSlot::Body);
	CHECK_EQ(fighter.hp, 60);
}

TEST(grantedSkillUsesDoNotRefillBySwapping) {
	Database db = data();
	db.equipment["spark_ring"].abilities = {"flare_burst"};
	PartyMember medic = makePartyMember(db, db.characters.at("medic"), 1);
	CHECK_EQ(medic.skillUses.at("flare_burst"), 1);

	BattleSetup setup = makeSetup(db, {"brass_warden"}, 1);
	setup.party[kMedic] = medic;
	Battle battle = makeBattle(db, setup);
	CHECK(battle.setCommand(kMedic, Command::ability("flare_burst", -1)).empty());
	othersDefend(battle);
	CHECK(firstBy(battle.resolveRound(), EventType::ActionStarted, kMedic).has_value());
	medic = battle.partyState()[kMedic];
	CHECK_EQ(medic.skillUses.at("flare_burst"), 0);

	EquipmentBag bag;
	unequip(medic, db, bag, EquipSlot::Accessory);
	CHECK(equip(medic, db, bag, "spark_ring").empty());
	CHECK_EQ(medic.skillUses.at("flare_burst"), 0);
	rest(medic, db);
	CHECK_EQ(medic.skillUses.at("flare_burst"), 1);
}

TEST(weaponElementAppliesTheTargetsAffinity) {
	Database db = data();
	db.characters["mystic"].base.dexterity = 500;
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite"}, seed);
		battle.combatant(kFirstEnemy).hp = 500;
		battle.setCommand(kMystic, Command::attack(kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<Event> damage = ofType(events, EventType::DamageDealt);
		CHECK_EQ(damage.size(), std::size_t(1));
		if (damage.empty()) {
			continue;
		}
		CHECK(damage[0].element == Element::Heat);
		CHECK(damage[0].effectiveness == Effectiveness::Weak);
		const int strength = battle.combatant(kMystic).stats.strength;
		const int bonus = ofType(events, EventType::Critical).empty() ? 0 : formulas::criticalBonus(strength);
		const int low = formulas::damage(strength, 3, bonus, 150, formulas::kSpreadMin);
		const int high = formulas::damage(strength, 3, bonus, 150, formulas::kSpreadMax);
		CHECK(damage[0].amount >= low && damage[0].amount <= high);
	}
}

TEST(weaponAttackAbilityHitsTheTargetsGroup) {
	Battle battle = makeBattle(data(), {"gel_mite", "rust_hound", "gel_mite"}, 2);
	battle.setCommand(kScout, Command::attack(kFirstEnemy + 2));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> started = firstBy(events, EventType::ActionStarted, kScout);
	CHECK(started && started->ability == "scatter_shot" && started->target == -1);
	int hits = 0;
	for (const Event& e : events) {
		if (e.type == EventType::DamageDealt || e.type == EventType::Missed) {
			CHECK(e.target == kFirstEnemy || e.target == kFirstEnemy + 2);
			++hits;
		}
	}
	CHECK_EQ(hits, 2);
}

TEST(equipmentCarriesBetweenBattles) {
	const Database& db = data();
	Battle first = makeBattle(db, {"gel_mite"}, 1);
	othersDefend(first);
	first.resolveRound();
	const std::vector<PartyMember> party = first.partyState();
	CHECK(party[kScout].equipment == first.setup().party[kScout].equipment);
	CHECK_EQ(party[kScout].equipment.at(EquipSlot::Weapon), std::string("volley_sling"));

	BattleSetup next = makeSetup(db, {"rust_hound"}, 2);
	next.party = party;
	Battle second = makeBattle(db, next);
	CHECK_EQ(second.combatant(kScout).basicAttack, std::string("scatter_shot"));
	CHECK_EQ(second.combatant(kFighter).stats.strength, first.combatant(kFighter).stats.strength);
}

TEST(abilitiesAreLearnedByLevel) {
	const Database& db = data();
	CHECK(!knows(knownAbilities(db, makePartyMember(db, db.characters.at("fighter"), 2)), "cleave"));
	CHECK(knows(knownAbilities(db, makePartyMember(db, db.characters.at("fighter"), 3)), "cleave"));

	// Skipping several levels at once still reports every ability passed on the way.
	Database wide = data();
	wide.characters["medic"].learns.push_back({6, "flare_burst"});
	PartyMember medic = makePartyMember(wide, wide.characters.at("medic"), 1);
	const LevelUp levelUp = grantExperience(medic, wide, 100000);
	CHECK_EQ(levelUp.learned, (std::vector<std::string>{"renew", "flare_burst"}));
	CHECK_EQ(medic.skillUses.at("flare_burst"), 1);
	CHECK(grantExperience(medic, wide, 100000).learned.empty());
}

TEST(equipmentMovesBetweenTheBagAndMembers) {
	const Database& db = data();
	PartyMember fighter = makePartyMember(db, db.characters.at("fighter"), 1);
	PartyMember scout = makePartyMember(db, db.characters.at("scout"), 1);
	EquipmentBag bag = {{"copper_blade", 1}};
	CHECK(equip(scout, db, bag, "copper_blade").empty());
	CHECK_EQ(scout.equipment.at(EquipSlot::Weapon), std::string("copper_blade"));
	CHECK(bag.count("copper_blade") == 0 && bag.at("volley_sling") == 1);

	// The one spare blade is worn, so nobody else can put one on.
	PartyMember engineer = makePartyMember(db, db.characters.at("engineer"), 1);
	CHECK_EQ(equip(engineer, db, bag, "copper_blade").key, std::string("equip.not_in_bag"));
	CHECK(engineer.equipment.count(EquipSlot::Weapon) == 0);

	unequip(fighter, db, bag, EquipSlot::Weapon);
	CHECK_EQ(bag.at("copper_blade"), 1);
	CHECK(equip(engineer, db, bag, "copper_blade").empty());
	CHECK(bag.count("copper_blade") == 0);
	unequip(fighter, db, bag, EquipSlot::Weapon);
	CHECK(bag.count("copper_blade") == 0);
}
