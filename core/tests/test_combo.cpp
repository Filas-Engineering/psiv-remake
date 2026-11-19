#include "fixtures.h"
#include "rules/formulas.h"
#include "rules/progression.h"
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
		CHECK_EQ(combos[0].combo, std::string("storm_spark"));
	}
	const std::vector<Event> discovered = ofType(events, EventType::ComboDiscovered);
	CHECK(discovered.size() == 1 && discovered[0].combo == "storm_spark");
	CHECK(!firstBy(events, EventType::ActionStarted, kMystic).has_value());
	CHECK(!firstBy(events, EventType::ActionStarted, kScout).has_value());
	CHECK_EQ(battle.combatant(kMystic).tp, mysticTp - 3);
	CHECK_EQ(battle.combatant(kScout).tp, scoutTp - 3);
	CHECK(log.isDiscovered("storm_spark"));

	const std::vector<Event> again = sparkThenGust(battle);
	CHECK(!ofType(again, EventType::ComboTriggered).empty());
	CHECK(ofType(again, EventType::ComboDiscovered).empty());
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
	const std::optional<Event> cancelled = firstBy(events, EventType::ActionCancelled, kScout);
	CHECK(cancelled && cancelled->cancel == CancelReason::ActorUnable && cancelled->status == "sleep");
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

TEST(macroComboFiresWhenNoEnemyActsBetweenItsSteps) {
	Database db = data();
	db.enemies["brass_warden"].stats.agility = 1;
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		Battle battle = makeBattle(db, {"brass_warden"}, seed);
		battle.combatant(kFighter).hp = 20;
		battle.applyMacro(*db.macro("regroup"));
		battle.setCommand(kFirstEnemy, Command::defend());
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<Event> combos = ofType(events, EventType::ComboTriggered);
		CHECK(!combos.empty() && combos[0].ability == "mending_chorus");
		CHECK_EQ(ofType(events, EventType::Healed).size(), std::size_t(6));
	}
}

TEST(anEnemyTurnBetweenMacroStepsBreaksTheCombo) {
	int fired = 0;
	int broken = 0;
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		Battle battle = makeBattle(data(), {"brass_warden"}, seed);
		battle.combatant(kFighter).hp = 20;
		battle.applyMacro(*data().macro("regroup"));
		battle.setCommand(kFirstEnemy, Command::defend());
		const std::vector<Event> events = battle.resolveRound();
		if (!ofType(events, EventType::ComboTriggered).empty()) {
			++fired;
			continue;
		}
		++broken;
		const std::vector<int>& order = battle.lastTurnOrder();
		const auto enemy = std::find(order.begin(), order.end(), kFirstEnemy);
		CHECK(enemy != order.begin() && enemy != order.end() && enemy + 1 != order.end());
	}
	CHECK(fired > 0);
	CHECK(broken > 0);
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

namespace {

// The first party member, not already taken, who knows the ability at the given level.
int knower(const Battle& battle, const std::string& ability, const std::vector<int>& taken) {
	for (int i = 0; i < battle.partyCount(); ++i) {
		if (battle.combatant(i).knows(ability) && std::find(taken.begin(), taken.end(), i) == taken.end()) {
			return i;
		}
	}
	return -1;
}

}

TEST(everyShippedRecipeFiresFromAMacro) {
	Database db = data();
	db.enemies["brass_warden"].stats.agility = 1;
	CHECK_EQ(db.combos.size(), std::size_t(15));
	for (const ComboRecipe& recipe : db.combos) {
		ComboLog log;
		BattleSetup setup = makeSetup(db, {"brass_warden"}, 3);
		for (PartyMember& member : setup.party) {
			member = makePartyMember(db, db.characters.at(member.characterId), 6);
		}
		Battle battle = makeBattle(db, setup, &log);
		battle.combatant(kFirstEnemy).hp = 5000;
		Macro macro;
		macro.name = recipe.id;
		std::vector<int> taken;
		for (std::size_t k = 0; k < recipe.abilities.size(); ++k) {
			const int actor = recipe.characters[k].empty() ? knower(battle, recipe.abilities[k], taken) : (recipe.characters[k] == "medic" ? kMedic : kFighter);
			if (actor < 0) {
				::test::fail(__FILE__, __LINE__, recipe.id + ": nobody knows " + recipe.abilities[k]);
			}
			taken.push_back(actor);
			macro.steps.push_back({actor >= 0 ? battle.combatant(actor).defId : "", CommandKind::Ability, recipe.abilities[k], {}});
		}
		// The rest of the party goes after the recipe, and the enemy is too slow to cut in.
		for (int i = 0; i < kFirstEnemy; ++i) {
			if (std::find(taken.begin(), taken.end(), i) == taken.end()) {
				macro.steps.push_back({battle.combatant(i).defId, CommandKind::Defend, "", {}});
			}
		}
		battle.applyMacro(macro);
		othersDefend(battle);
		const std::vector<Event> fired = ofType(battle.resolveRound(), EventType::ComboTriggered);
		if (!(fired.size() == 1 && fired[0].combo == recipe.id && fired[0].ability == recipe.result)) {
			::test::fail(__FILE__, __LINE__, recipe.id + " did not fire from a macro");
		}
		CHECK(log.isDiscovered(recipe.id));
	}
}

TEST(aTiedComponentNeedsItsCharacter) {
	Database db = orderedData("");
	// The scout wears the ring too, so both the scout and the medic know glint.
	db.characters["scout"].startingEquipment = {"spark_ring"};
	for (const char* user : {"scout", "medic"}) {
		const int actor = std::string(user) == "scout" ? kScout : kMedic;
		db.characters[user].base.agility = 60;
		Battle battle = makeBattle(db, {"brass_warden"}, 5);
		db.characters[user].base.agility = 1;
		battle.combatant(kFirstEnemy).hp = 5000;
		battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
		battle.setCommand(actor, Command::ability("glint", kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<int>& order = battle.lastTurnOrder();
		CHECK_EQ(position(order, kMystic) + 1, position(order, actor));
		const std::vector<Event> combos = ofType(events, EventType::ComboTriggered);
		if (actor == kMedic) {
			CHECK(combos.size() == 1 && combos[0].combo == "twin_arc");
		} else {
			CHECK(combos.empty());
		}
	}
}

TEST(anOrderedRecipeNeedsItsOrder) {
	for (bool gustFirst : {true, false}) {
		Database db = orderedData("");
		db.characters["mystic"].base.agility = gustFirst ? 20 : 100;
		db.characters["scout"].base.agility = gustFirst ? 100 : 20;
		Battle battle = makeBattle(db, {"brass_warden"}, 6);
		battle.combatant(kFirstEnemy).hp = 5000;
		battle.setCommand(kMystic, Command::ability("ember", kFirstEnemy));
		battle.setCommand(kScout, Command::ability("gust", kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> combos = ofType(battle.resolveRound(), EventType::ComboTriggered);
		if (gustFirst) {
			CHECK(combos.size() == 1 && combos[0].combo == "cinder_gale");
		} else {
			CHECK(combos.empty());
		}
	}
}

TEST(comboLogKeepsPlayTimeAndCharacters) {
	const Database db = orderedData("");
	ComboLog log;
	Battle battle = makeBattle(db, {"brass_warden"}, 3, &log);
	battle.setPlayTime(4321000);
	sparkThenGust(battle);
	const ComboLogEntry* entry = log.entry("storm_spark");
	CHECK(entry != nullptr);
	if (entry) {
		CHECK_EQ(entry->playTimeMs, std::int64_t(4321000));
		CHECK_EQ(entry->characters, (std::vector<std::string>{"mystic", "scout"}));
	}
	battle.setPlayTime(9000000);
	sparkThenGust(battle);
	CHECK_EQ(log.entries().size(), std::size_t(1));
	CHECK_EQ(log.entry("storm_spark")->playTimeMs, std::int64_t(4321000));

	// The record keeps the play time, so a replay rebuilds the same log.
	ComboLog replayLog;
	std::vector<Event> events;
	CHECK(replayBattle(db, battle.record(), replayLog, events).ok());
	CHECK(replayLog.entry("storm_spark") && replayLog.entry("storm_spark")->playTimeMs == 4321000);
}
