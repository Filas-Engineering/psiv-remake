#include "fixtures.h"
#include "rules/formulas.h"
#include "test.h"

#include <algorithm>

using namespace rules;
using namespace fixtures;

namespace {

int position(const std::vector<int>& order, int actor) {
	return static_cast<int>(std::find(order.begin(), order.end(), actor) - order.begin());
}

// Mystic always acts first and scout third-fastest; `between` (agility 60) lands between them.
Database orderedData(const char* between) {
	Database db = data();
	for (auto& [id, character] : db.characters) {
		character.base.agility = 1;
	}
	db.enemies["gel_mite"].stats.agility = 1;
	db.characters["mystic"].base.agility = 100;
	db.characters["scout"].base.agility = 20;
	if (std::string(between) == "gel_mite") {
		db.enemies["gel_mite"].stats.agility = 60;
	} else if (*between) {
		db.characters[between].base.agility = 60;
	}
	return db;
}

std::vector<Event> sparkThenGust(Battle& battle) {
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
	battle.setCommand(kScout, Command::ability("gust", kFirstEnemy));
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	battle.setCommand(kFirstEnemy, Command::defend());
	othersDefend(battle);
	return battle.resolveRound();
}

}

TEST(comboFiresForConsecutiveActorsAndIsLogged) {
	const Database db = orderedData("");
	ComboLog log;
	Battle battle = makeBattle(db, {"brass_warden"}, 3, &log);
	const int mysticTp = battle.combatant(kMystic).tp;
	const int scoutTp = battle.combatant(kScout).tp;
	const std::vector<Event> events = sparkThenGust(battle);
	CHECK_EQ(position(battle.lastTurnOrder(), kMystic) + 1, position(battle.lastTurnOrder(), kScout));

	const std::vector<Event> combos = ofType(events, EventType::ComboTriggered);
	CHECK_EQ(combos.size(), std::size_t(1));
	if (!combos.empty()) {
		CHECK_EQ(combos[0].ability, std::string("storm_spark"));
		CHECK_EQ(combos[0].participants, (std::vector<int>{kMystic, kScout}));
		CHECK(combos[0].firstDiscovery);
	}
	CHECK(!firstBy(events, EventType::ActionStarted, kMystic).has_value());
	CHECK(!firstBy(events, EventType::ActionStarted, kScout).has_value());
	CHECK_EQ(battle.combatant(kMystic).tp, mysticTp - 3);
	CHECK_EQ(battle.combatant(kScout).tp, scoutTp - 3);
	CHECK(log.isDiscovered("storm_spark"));

	const std::vector<Event> again = ofType(sparkThenGust(battle), EventType::ComboTriggered);
	CHECK(!again.empty() && !again[0].firstDiscovery);
}

TEST(enemyActionBetweenBreaksTheCombo) {
	const Database db = orderedData("gel_mite");
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		ComboLog log;
		Battle battle = makeBattle(db, {"gel_mite"}, seed, &log);
		battle.combatant(kFirstEnemy).hp = 500;
		const std::vector<Event> events = sparkThenGust(battle);
		const std::vector<int>& order = battle.lastTurnOrder();
		CHECK(position(order, kMystic) < position(order, kFirstEnemy));
		CHECK(position(order, kFirstEnemy) < position(order, kScout));
		CHECK(ofType(events, EventType::ComboTriggered).empty());
		CHECK(firstBy(events, EventType::ActionStarted, kMystic).has_value());
		CHECK(firstBy(events, EventType::ActionStarted, kScout).has_value());
		CHECK(log.discovered().empty());
	}
}

TEST(allyActionBetweenBreaksTheCombo) {
	const Database db = orderedData("fighter");
	Battle battle = makeBattle(db, {"brass_warden"}, 2);
	const std::vector<Event> events = sparkThenGust(battle);
	CHECK_EQ(position(battle.lastTurnOrder(), kFighter), 1);
	CHECK(ofType(events, EventType::ComboTriggered).empty());
}

TEST(sleepingParticipantBreaksTheCombo) {
	const Database db = orderedData("");
	Battle battle = makeBattle(db, {"brass_warden"}, 2);
	addStatus(battle, kScout, "sleep", 3);
	const std::vector<Event> events = sparkThenGust(battle);
	CHECK(ofType(events, EventType::ComboTriggered).empty());
	CHECK(firstBy(events, EventType::ActionPrevented, kScout).has_value());
}

TEST(longestRecipeWinsInsideAMacro) {
	ComboLog log;
	Battle battle = makeBattle(data(), {"brass_warden"}, 9, &log);
	battle.applyMacro(*data().macro("tempest"));
	battle.setCommand(kFirstEnemy, Command::defend());
	const std::vector<Event> combos = ofType(battle.resolveRound(), EventType::ComboTriggered);
	CHECK_EQ(combos.size(), std::size_t(1));
	if (!combos.empty()) {
		CHECK_EQ(combos[0].ability, std::string("tempest_flare"));
		CHECK_EQ(combos[0].participants, (std::vector<int>{kMystic, kScout, kEngineer}));
	}
	CHECK_EQ(battle.combatant(kEngineer).skillUses.at("flare_burst"), 0);
	CHECK(log.isDiscovered("tempest_flare"));
	CHECK(!log.isDiscovered("storm_spark"));
}

TEST(macroOrderMakesSupportCombosReliable) {
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		Battle battle = makeBattle(data(), {"brass_warden"}, seed);
		battle.combatant(kFighter).hp = 20;
		battle.applyMacro(*data().macro("regroup"));
		battle.setCommand(kFirstEnemy, Command::defend());
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<Event> combos = ofType(events, EventType::ComboTriggered);
		CHECK(!combos.empty() && combos[0].ability == "mending_chorus");
		CHECK_EQ(ofType(events, EventType::Healed).size(), std::size_t(6));
	}
}

TEST(comboAveragesEveryParticipantsStats) {
	Database db = orderedData("");
	db.characters["scout"].base.mental = 200;
	for (std::uint64_t seed = 1; seed <= 5; ++seed) {
		Battle battle = makeBattle(db, {"brass_warden"}, seed);
		battle.combatant(kFirstEnemy).hp = 2000;
		const std::vector<Event> events = sparkThenGust(battle);
		CHECK_EQ(ofType(events, EventType::ComboTriggered).size(), std::size_t(1));
		const int mental = (battle.combatant(kMystic).stats.mental + battle.combatant(kScout).stats.mental) / 2;
		const int low = formulas::defended(formulas::elementalDamage(80, mental, 10, 100, formulas::kVarianceMin));
		const int high = formulas::defended(formulas::elementalDamage(80, mental, 10, 100, formulas::kVarianceMax));
		bool found = false;
		for (const Event& e : ofType(events, EventType::DamageDealt)) {
			if (e.target == kFirstEnemy && e.element == Element::Shock) {
				found = true;
				CHECK(e.amount >= low && e.amount <= high);
			}
		}
		CHECK(found);
	}
}
