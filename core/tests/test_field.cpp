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
	CHECK_EQ(inventory.at("second_wind"), 0);
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
	CHECK_EQ(members[kFighter].hp, 10 + formulas::healAmount(28, memberStats(data(), members[kMedic]).mental));
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
