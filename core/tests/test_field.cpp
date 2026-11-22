#include "fixtures.h"
#include "rules/field.h"
#include "rules/formulas.h"
#include "rules/progression.h"
#include "test.h"

using namespace rules;
using namespace fixtures;

namespace {

std::vector<PartyMember> party() {
	std::vector<PartyMember> members;
	for (const std::string& id : data().lineup) {
		members.push_back(makePartyMember(data(), data().characters.at(id), 1));
	}
	return members;
}

}

TEST(itemsWorkOutsideBattleThroughTheBattleEffects) {
	std::vector<PartyMember> members = party();
	Inventory inventory = data().startingInventory;
	members[kFighter].hp = 10;
	FieldUse use = useItemOutsideBattle(data(), members, inventory, "tonic", kFighter);
	CHECK(use.rejection.empty());
	CHECK_EQ(members[kFighter].hp, 40);
	CHECK_EQ(inventory.at("tonic"), 2);
	const std::vector<Event> heals = ofType(use.events, EventType::Healed);
	CHECK(heals.size() == 1 && heals[0].target == kFighter && heals[0].amount == 30 && heals[0].hp == 40 && heals[0].round == 0);
	CHECK_EQ(ofType(use.events, EventType::ResourceSpent).size(), std::size_t(1));

	// Changing nothing keeps the item, as in battle.
	use = useItemOutsideBattle(data(), members, inventory, "tonic", kMystic);
	CHECK(use.rejection.empty() && !ofType(use.events, EventType::ItemHadNoEffect).empty());
	CHECK_EQ(inventory.at("tonic"), 2);

	members[kScout].hp = 0;
	use = useItemOutsideBattle(data(), members, inventory, "second_wind", kScout);
	CHECK_EQ(members[kScout].hp, formulas::reviveHp(memberStats(data(), members[kScout]).maxHp, 30));
	CHECK_EQ(inventory.count("second_wind"), std::size_t(0));
	CHECK_EQ(useItemOutsideBattle(data(), members, inventory, "second_wind", kScout).rejection.key, std::string("field.no_stock"));

	members[kMedic].statuses = {{"poison", 3}, {"gear_lock", 0}};
	useItemOutsideBattle(data(), members, inventory, "clear_gel", kMedic);
	useItemOutsideBattle(data(), members, inventory, "loosen_oil", kMedic);
	CHECK(members[kMedic].statuses.empty());

	CHECK_EQ(useItemOutsideBattle(data(), members, inventory, "wake_chime", -1).rejection.key, std::string("field.not_usable_here"));
	CHECK_EQ(useItemOutsideBattle(data(), members, inventory, "tonic", -1).rejection.key, std::string("field.no_target"));
}

TEST(techniquesWorkOutsideBattleAndCostTheirPoints) {
	std::vector<PartyMember> members = party();
	members[kFighter].hp = 10;
	const int tp = members[kMedic].tp;
	FieldUse use = useAbilityOutsideBattle(data(), members, kMedic, "mend", kFighter);
	CHECK(use.rejection.empty());
	CHECK_EQ(members[kFighter].hp, 10 + formulas::healAmount(28, memberStats(data(), members[kMedic]).mental, formulas::kSpreadMean));
	CHECK_EQ(members[kMedic].tp, tp - 3);
	CHECK_EQ(ofType(use.events, EventType::ResourceSpent).size(), std::size_t(1));

	members[kScout].hp = 0;
	CHECK(useAbilityOutsideBattle(data(), members, kMedic, "rekindle", kScout).rejection.empty());
	CHECK(members[kScout].hp > 0);

	CHECK_EQ(useAbilityOutsideBattle(data(), members, kMystic, "spark", kFighter).rejection.key, std::string("field.not_usable_here"));
	CHECK_EQ(useAbilityOutsideBattle(data(), members, kMystic, "mend", kFighter).rejection.key, std::string("field.not_known"));
	members[kMedic].tp = 1;
	CHECK_EQ(useAbilityOutsideBattle(data(), members, kMedic, "mend", kFighter).rejection.key, std::string("field.not_enough_tp"));
	members[kMedic].hp = 0;
	CHECK_EQ(useAbilityOutsideBattle(data(), members, kMedic, "mend", kFighter).rejection.key, std::string("field.user_down"));
}

TEST(travelItemsAndTechniquesOnlyTakeTheirCost) {
	std::vector<PartyMember> members = party();
	Inventory inventory{{"homing_shell", 2}};
	FieldUse use = useItemOutsideBattle(data(), members, inventory, "homing_shell", -1);
	CHECK(use.rejection.empty());
	CHECK_EQ(inventory.at("homing_shell"), 1);
	CHECK_EQ(ofType(use.events, EventType::ResourceSpent).size(), std::size_t(1));
	CHECK(ofType(use.events, EventType::ItemHadNoEffect).empty());

	members[kMystic] = makePartyMember(data(), data().characters.at("mystic"), 9);
	const int tp = members[kMystic].tp;
	use = useAbilityOutsideBattle(data(), members, kMystic, "homeward", -1);
	CHECK(use.rejection.empty());
	CHECK_EQ(members[kMystic].tp, tp - 8);
	CHECK_EQ(useAbilityOutsideBattle(data(), members, kScout, "outpath", -1).rejection.key, std::string("field.not_known"));
}

namespace {

// The fighter with no mental, a strong arm and a blade with an attack bonus, who knows a field skill that cures, stands
// the fallen at 0 and heals with power 0 every living ally, attacking with `attackStat`.
Database rallyData(std::optional<Stat> attackStat) {
	Database db = data();
	db.characters["fighter"].base.mental = 0;
	db.characters["fighter"].base.strength = 20;
	db.equipment["copper_blade"].attackBonus = 6;
	AbilityDef rally;
	rally.id = "field_rally";
	rally.name = "Field Rally";
	rally.kind = AbilityKind::Skill;
	rally.uses = 3;
	rally.targeting = Targeting::AllAllies;
	rally.field = true;
	rally.attackStat = attackStat;
	for (const char* status : {"poison", "sleep", "paralysis"}) {
		Effect cure;
		cure.kind = EffectKind::CureStatus;
		cure.status = status;
		rally.effects.push_back(cure);
	}
	Effect revive;
	revive.kind = EffectKind::Revive;
	revive.zeroHp = true;
	Effect heal;
	heal.kind = EffectKind::Heal;
	rally.effects.push_back(revive);
	rally.effects.push_back(heal);
	for (Effect& effect : rally.effects) {
		effect.kinds = {CreatureKind::Living};
	}
	db.abilities[rally.id] = rally;
	db.characters["fighter"].abilities.push_back(rally.id);
	return db;
}

// The scout fallen, every other ally at 1 hit point.
void wound(std::vector<PartyMember>& members) {
	for (PartyMember& member : members) {
		member.hp = 1;
	}
	members[kScout].hp = 0;
}

// The rolled heals, by target, when the fighter uses the rally in a battle where everyone else defends; with `mental`, the
// fighter's mental is that and the rally attacks with it.
std::map<int, int> battleHeals(Database db, std::optional<int> mental) {
	if (mental) {
		db.abilities["field_rally"].attackStat.reset();
	}
	BattleSetup setup = makeSetup(db, {"gel_mite"}, 5);
	wound(setup.party);
	Battle battle = makeBattle(db, setup);
	if (mental) {
		battle.combatant(kFighter).stats.mental = *mental;
	}
	CHECK(battle.setCommand(kFighter, Command::ability("field_rally", -1)).empty());
	othersDefend(battle);
	std::map<int, int> heals;
	for (const Event& e : ofType(battle.resolveRound(), EventType::Healed)) {
		heals[e.target] = e.rolled;
	}
	return heals;
}

}

// A field ability heals as the same ability does in battle for the same user and targets: with its `attack_stat` in place
// of mental. The battle draws a spread for each heal and the field heals at the spread's mean, so a twin battle whose
// rally heals with a mental of 128 reads each draw back.
TEST(fieldAbilitiesAttackWithTheirAttackStatAsInBattle) {
	const std::vector<std::optional<Stat>> attackStats = {Stat::Strength, Stat::Attack, std::nullopt};
	for (const std::optional<Stat>& attackStat : attackStats) {
		const Database db = rallyData(attackStat);
		std::vector<PartyMember> members;
		for (const std::string& id : db.lineup) {
			members.push_back(makePartyMember(db, db.characters.at(id), 1));
		}
		wound(members);
		const int stat = memberCombatant(db, members[kFighter]).effective(attackStat.value_or(Stat::Mental));
		CHECK(stat > 0 || !attackStat);
		const std::map<int, int> inBattle = battleHeals(db, std::nullopt);
		const std::map<int, int> draws = battleHeals(db, 128);
		CHECK_EQ(inBattle.size(), members.size());
		CHECK_EQ(draws.size(), members.size());

		const FieldUse use = useAbilityOutsideBattle(db, members, kFighter, "field_rally", -1);
		CHECK(use.rejection.empty());
		const std::vector<Event> heals = ofType(use.events, EventType::Healed);
		CHECK_EQ(heals.size(), members.size());
		for (const Event& e : heals) {
			const int spread = draws.at(e.target) - 64;
			CHECK_EQ(inBattle.at(e.target), formulas::healAmount(0, stat, spread));
			CHECK_EQ(e.rolled, formulas::healAmount(0, stat, formulas::kSpreadMean));
		}
		CHECK_EQ(members[kScout].hp, std::max(1, formulas::healAmount(0, stat, formulas::kSpreadMean)));
	}
}
