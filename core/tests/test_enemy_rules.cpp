#include "fixtures.h"
#include "rules/auto_policy.h"
#include "rules/field.h"
#include "rules/loader.h"
#include "rules/progression.h"
#include "test.h"

#include <algorithm>

using namespace rules;
using namespace fixtures;

namespace {

constexpr int kCogwright = kEngineer;

// The shipped lineup with the machine member in the engineer's place.
BattleSetup withCogwright(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed) {
	BattleSetup setup = makeSetup(db, enemies, seed);
	setup.party[kCogwright] = makePartyMember(db, *db.character("cogwright"), 1);
	return setup;
}

void sturdy(Battle& battle, int index) {
	battle.combatant(index).stats.maxHp = 5000;
	battle.combatant(index).hp = 5000;
}

void replaceOnce(std::string& text, const std::string& from, const std::string& to) {
	const std::size_t at = text.find(from);
	if (at == std::string::npos) {
		::test::fail(__FILE__, __LINE__, "fixture text not found: " + from);
		return;
	}
	text.replace(at, from.size(), to);
}

void expectError(const std::map<std::string, std::string>& files, const std::string& expected) {
	const Result<Database> loaded = loadDatabaseFromTexts(files);
	if (loaded.ok()) {
		::test::fail(__FILE__, __LINE__, "expected a load error containing: " + expected);
	} else if (loaded.error.find(expected) == std::string::npos) {
		::test::fail(__FILE__, __LINE__, "expected '" + expected + "' in:\n" + loaded.error);
	}
}

}

// P6: healing aimed at the living passes a machine by; its own repair and the salve reach it.
TEST(machineMembersTakeOnlyMachineHealing) {
	const Database& db = data();
	BattleSetup setup = withCogwright(db, {"brass_warden"}, 4);
	setup.inventory = {{"tonic", 1}, {"gear_salve", 1}};
	Battle battle = makeBattle(db, setup);
	sturdy(battle, kFirstEnemy);
	battle.combatant(kCogwright).hp = 20;
	battle.setCommand(kMedic, Command::ability("mend", kCogwright));
	battle.setCommand(kFighter, Command::item("tonic", kCogwright));
	othersDefend(battle);
	std::vector<Event> events = battle.resolveRound();
	CHECK_EQ(battle.combatant(kCogwright).hp, 20);
	CHECK(firstBy(events, EventType::ItemHadNoEffect, kFighter).has_value());
	CHECK_EQ(battle.inventory().at("tonic"), 1);

	battle.setCommand(kCogwright, Command::ability("self_repair", kCogwright));
	othersDefend(battle);
	events = battle.resolveRound();
	CHECK(battle.combatant(kCogwright).hp > 20);
	battle.setCommand(kScout, Command::item("gear_salve", kCogwright));
	othersDefend(battle);
	battle.resolveRound();
	CHECK_EQ(battle.combatant(kCogwright).hp, battle.combatant(kCogwright).stats.maxHp);
	CHECK_EQ(battle.inventory().at("gear_salve"), 0);
}

// R50: auto-battle's revives and heals pass by a fallen or hurt machine the technique cannot reach, for the living
// member beside it, and leave the machine alone when it is the only one down or hurt.
TEST(autoBattleRevivesAndHealsOnlyMembersItsTechniquesReach) {
	const Database& db = data();
	BattleSetup setup = makeSetup(db, {"brass_warden"}, 4);
	setup.party[kFighter] = makePartyMember(db, *db.character("cogwright"), 1);
	Battle battle = makeBattle(db, setup);
	auto fell = [&battle](int index) {
		battle.combatant(index).hp = 0;
		battle.combatant(index).statuses = {{kDefeatedStatus, 0}};
	};
	fell(kFighter);
	fell(kScout);
	Command chosen = autoCommand(battle, kMedic);
	CHECK(chosen.kind == CommandKind::Ability && chosen.id == "rekindle" && chosen.target == kScout);

	Macro macro;
	macro.name = "raise";
	macro.steps.push_back({"medic", CommandKind::Ability, "rekindle", {}});
	battle.applyMacro(macro);
	othersDefend(battle);
	battle.setCommand(kFirstEnemy, Command::defend());
	std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> revived = ofType(events, EventType::Revived);
	CHECK(revived.size() == 1 && revived[0].target == kScout);
	CHECK(!battle.combatant(kFighter).alive());

	chosen = autoCommand(battle, kMedic);
	CHECK(chosen.id != "rekindle" && chosen.id != "mend");
	battle.combatant(kFighter).hp = 1;
	battle.combatant(kFighter).statuses.clear();
	battle.combatant(kScout).hp = battle.combatant(kScout).stats.maxHp * 30 / 100;
	chosen = autoCommand(battle, kMedic);
	CHECK(chosen.kind == CommandKind::Ability && chosen.id == "mend" && chosen.target == kScout);
	battle.combatant(kScout).hp = battle.combatant(kScout).stats.maxHp;
	chosen = autoCommand(battle, kMedic);
	CHECK(chosen.id != "mend");
}

// R50: an enemy's revive or heal that cannot reach a fallen or hurt machine ally finds no target there.
TEST(enemyRevivesAndHealsPassAlliesTheyCannotReach) {
	Database db = data();
	db.abilities["rouse"].effects.front().kinds = {CreatureKind::Living};
	db.enemies["brass_warden"].slots.assign(static_cast<std::size_t>(kAiSlots), "rouse");
	db.enemies["brass_warden"].overrides.clear();
	Battle battle = makeBattle(db, {"brass_warden", "brass_warden", "gel_mite"}, 5);
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).stats.maxHp = battle.combatant(i).hp = 5000;
	}
	auto fell = [&battle](int index) {
		battle.combatant(index).hp = 0;
		battle.combatant(index).statuses = {{kDefeatedStatus, 0}};
	};
	fell(kFirstEnemy + 1);
	fell(kFirstEnemy + 2);
	partyDefends(battle);
	std::vector<Event> events = battle.resolveRound();
	std::optional<Event> action = firstBy(events, EventType::ActionStarted, kFirstEnemy);
	CHECK(action && action->ability == "rouse" && action->target == kFirstEnemy + 2);
	fell(kFirstEnemy + 2);
	battle.combatant(kFirstEnemy + 2).hp = 5000;
	battle.combatant(kFirstEnemy + 2).statuses.clear();
	partyDefends(battle);
	events = battle.resolveRound();
	action = firstBy(events, EventType::ActionStarted, kFirstEnemy);
	CHECK(action && action->ability == "bite");

	db = data();
	db.abilities["patch"].effects.front().kinds = {CreatureKind::Living};
	db.enemies["bog_wisp"].slots.assign(static_cast<std::size_t>(kAiSlots), "bite");
	db.enemies["bog_wisp"].overrides = {};
	AiOverride heal;
	heal.when = AiConditionKind::AnyAllyHpHalf;
	heal.ability = "patch";
	db.enemies["bog_wisp"].overrides.push_back(heal);
	Battle wisp = makeBattle(db, {"bog_wisp", "brass_warden"}, 1);
	wisp.combatant(kFirstEnemy + 1).hp = wisp.combatant(kFirstEnemy + 1).stats.maxHp / 3;
	partyDefends(wisp);
	action = firstBy(wisp.resolveRound(), EventType::ActionStarted, kFirstEnemy);
	CHECK(action && action->ability == "bite");
}

// P6: a fallen machine shares no experience and stands up with the rules' hit points once the battle is won.
TEST(aFallenMachineStandsUpAfterAWonBattle) {
	const Database& db = data();
	Battle battle = makeBattle(db, withCogwright(db, {"gel_mite"}, 2));
	battle.combatant(kCogwright).hp = 0;
	battle.combatant(kCogwright).statuses = {{kDefeatedStatus, 0}};
	battle.combatant(kFirstEnemy).hp = 1;
	const std::vector<Event> events = playToEnd(battle, 10);
	CHECK(battle.outcome() == Outcome::Victory);
	const std::vector<Event> revived = ofType(events, EventType::Revived);
	CHECK(revived.size() == 1 && revived[0].target == kCogwright && revived[0].hp == db.battleRules.machineRecoverHp);
	CHECK_EQ(battle.rewards().memberExperience.at(kCogwright), 0);
	CHECK(battle.rewards().memberExperience.at(kFighter) > 0);
	CHECK_EQ(battle.partyState().at(kCogwright).hp, db.battleRules.machineRecoverHp);
	CHECK_EQ(battle.partyState().at(kFighter).hp, battle.combatant(kFighter).hp);
}

// B6, B8: a spirit takes holy damage as a weakness; others take it plain.
TEST(holyDamageHurtsSpiritsMore) {
	Database db = data();
	db.characters["mystic"].abilities.push_back("hallow");
	Battle battle = makeBattle(db, {"bog_wisp", "gel_mite"}, 3);
	sturdy(battle, kFirstEnemy);
	sturdy(battle, kFirstEnemy + 1);
	battle.setCommand(kMystic, Command::ability("hallow", kFirstEnemy));
	battle.setCommand(kMedic, Command::ability("glint", kFirstEnemy + 1));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK(db.enemies.at("bog_wisp").kind == CreatureKind::Spirit);
	bool weak = false;
	for (const Event& e : ofType(events, EventType::DamageDealt)) {
		weak = weak || (e.target == kFirstEnemy && e.element == Element::Holy && e.effectiveness == Effectiveness::Weak);
	}
	CHECK(weak);
}

// B7: an instant defeat rolls like a status against a vulnerable foe and never lands on anyone else.
TEST(instantDefeatWorksOnlyOnVulnerableFoes) {
	Database db = data();
	db.characters["mystic"].abilities.push_back("unmake");
	int landed = 0;
	int resisted = 0;
	for (std::uint64_t seed = 1; seed <= 40; ++seed) {
		Battle battle = makeBattle(db, {"twin_ooze", "brass_warden"}, seed);
		sturdy(battle, kFirstEnemy + 1);
		battle.setCommand(kMystic, Command::ability("unmake", kFirstEnemy));
		battle.setCommand(kMedic, Command::ability("glint", kFirstEnemy + 1));
		othersDefend(battle);
		std::vector<Event> events = battle.resolveRound();
		for (const Event& e : ofType(events, EventType::Message)) {
			landed += e.message == "battle.instant_defeat" && e.target == kFirstEnemy ? 1 : 0;
		}
		for (const Event& e : ofType(events, EventType::Message)) {
			resisted += e.message == "battle.instant_defeat_resisted" ? 1 : 0;
		}
		battle.combatant(kMystic).tp = battle.combatant(kMystic).stats.maxTp;
		battle.setCommand(kMystic, Command::ability("unmake", kFirstEnemy + 1));
		othersDefend(battle);
		events = battle.resolveRound();
		std::vector<Event> immune;
		for (const Event& e : ofType(events, EventType::Message)) {
			if (e.message.rfind("battle.instant_defeat", 0) == 0) {
				immune.push_back(e);
			}
		}
		CHECK(immune.size() == 1 && immune[0].effectiveness == Effectiveness::Immune && immune[0].message == "battle.instant_defeat_immune");
		CHECK(battle.combatant(kFirstEnemy + 1).alive());
	}
	CHECK(landed > 0);
	CHECK(resisted > 0);
}

// B20: the party's technique points come back in battle; such an effect cannot be used outside battle.
TEST(restoringTechniquePointsWorksInBattleOnly) {
	const Database& db = data();
	Battle battle = makeBattle(db, withCogwright(db, {"brass_warden"}, 5));
	sturdy(battle, kFirstEnemy);
	battle.combatant(kMystic).tp = 2;
	battle.combatant(kMedic).tp = battle.combatant(kMedic).stats.maxTp - 3;
	battle.setCommand(kCogwright, Command::ability("calm_tide", -1));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK_EQ(battle.combatant(kMystic).tp, 12);
	CHECK_EQ(battle.combatant(kMedic).tp, battle.combatant(kMedic).stats.maxTp);
	bool tpHeal = false;
	for (const Event& e : ofType(events, EventType::Healed)) {
		tpHeal = tpHeal || (e.resource == ResourceKind::Tp && e.target == kMystic && e.amount == 10);
	}
	CHECK(tpHeal);

	auto files = dataTexts();
	replaceOnce(files["abilities.json"], "{\"id\": \"calm_tide\", \"name\": \"Calm Tide\", \"kind\": \"skill\", \"uses\": 2,", "{\"id\": \"calm_tide\", \"name\": \"Calm Tide\", \"kind\": \"skill\", \"uses\": 2, \"field\": true,");
	expectError(files, "field use allows only heal");
}

// B10: an enemy's howl strips every raise at once, and a dust does the same to an enemy.
TEST(strippingRemovesEveryStatChange) {
	const Database& db = data();
	BattleSetup setup = makeSetup(db, {"aegis_sentinel"}, 6);
	setup.inventory = {{"null_dust", 1}};
	Battle battle = makeBattle(db, setup);
	battle.combatant(kFighter).setModifier(Stat::Strength, 9);
	battle.combatant(kFighter).setModifier(Stat::Defense, 4);
	battle.combatant(kFirstEnemy).setModifier(Stat::Agility, 7);
	battle.setCommand(kFirstEnemy, Command::ability("dispel_howl", -1));
	battle.setCommand(kScout, Command::item("null_dust", kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK(battle.combatant(kFighter).modifiers.empty());
	CHECK(battle.combatant(kFirstEnemy).modifiers.empty());
	CHECK_EQ(battle.combatant(kFighter).effective(Stat::Strength), battle.combatant(kFighter).base(Stat::Strength));
	int stripped = 0;
	for (const Event& e : ofType(events, EventType::StatModified)) {
		stripped += e.amount == 0 ? 1 : 0;
	}
	CHECK_EQ(stripped, 3);
}

// B11: the shield holds every hit to the cap until the story item takes it off.
TEST(aShieldHoldsDamageToItsCapUntilRemoved) {
	const Database& db = data();
	BattleSetup setup = makeSetup(db, {"aegis_sentinel"}, 7);
	setup.inventory = {{"sunstone", 1}};
	Battle battle = makeBattle(db, setup);
	CHECK(battle.combatant(kFirstEnemy).hasStatus("aegis"));
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
	battle.setCommand(kFirstEnemy, Command::defend());
	othersDefend(battle);
	std::vector<Event> events = battle.resolveRound();
	for (const Event& e : ofType(events, EventType::DamageDealt)) {
		CHECK(e.amount <= 1);
	}
	battle.setCommand(kScout, Command::item("sunstone", kFirstEnemy));
	battle.setCommand(kFirstEnemy, Command::defend());
	othersDefend(battle);
	battle.resolveRound();
	CHECK(!battle.combatant(kFirstEnemy).hasStatus("aegis"));
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
	battle.setCommand(kFirstEnemy, Command::defend());
	othersDefend(battle);
	events = battle.resolveRound();
	const std::vector<Event> hits = ofType(events, EventType::DamageDealt);
	CHECK(!hits.empty() && hits[0].amount > 1);
}

// B12: a fallen enemy is replaced in its place at once, so the battle is never won; escape still ends it.
TEST(fallenEnemiesAreReplacedUntilThePartyRuns) {
	const Database& db = data();
	BattleSetup setup = makeSetup(db, {}, 8);
	useEncounter(setup, db, "endless");
	Battle battle = makeBattle(db, setup);
	battle.combatant(kFirstEnemy).hp = 1;
	battle.combatant(kFirstEnemy + 1).hp = 1;
	battle.setCommand(kEngineer, Command::ability("flare_burst", -1));
	othersDefend(battle);
	std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> joined = ofType(events, EventType::EnemyJoined);
	CHECK_EQ(joined.size(), std::size_t(2));
	if (joined.size() == 2) {
		CHECK(joined[0].ids == std::vector<std::string>{"gel_mite"});
		CHECK_EQ(joined[0].amount, 0);
		CHECK_EQ(joined[1].amount, 1);
		CHECK_EQ(joined[0].target, kFirstEnemy + 2);
	}
	CHECK(battle.outcome() == Outcome::Ongoing);
	CHECK_EQ(battle.combatants().size(), std::size_t(kFirstEnemy + 4));
	for (int round = 0; round < 30 && battle.outcome() == Outcome::Ongoing; ++round) {
		battle.setCommand(kScout, Command::escape());
		othersDefend(battle);
		battle.resolveRound();
	}
	CHECK(battle.outcome() == Outcome::Escaped);
}

// B13: with a free place beside it the summoner calls up kin there instead of attacking.
TEST(aSummonerCallsKinIntoAFreePlaceBesideIt) {
	const Database& db = data();
	Battle battle = makeBattle(db, {"hive_lantern"}, 9);
	for (int i = 0; i < kFirstEnemy; ++i) {
		sturdy(battle, i);
	}
	partyDefends(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> action = firstBy(events, EventType::ActionStarted, kFirstEnemy);
	CHECK(action && action->ability == "call_kin");
	const std::vector<Event> joined = ofType(events, EventType::EnemyJoined);
	CHECK(joined.size() == 1 && joined[0].actor == kFirstEnemy && joined[0].amount == 1 && joined[0].ids == std::vector<std::string>{"gel_mite"});
	CHECK(ofType(events, EventType::DamageDealt).empty());
	// Both places beside position 0 are now the edge and the summoned mite, so the lantern bites.
	partyDefends(battle);
	const std::vector<Event> next = battle.resolveRound();
	const std::optional<Event> bite = firstBy(next, EventType::ActionStarted, kFirstEnemy);
	CHECK(bite && bite->ability == "bite");
}

// B14: two of a kind merge into one stronger enemy, which alone pays out.
TEST(twoEnemiesOfAKindMergeIntoAStrongerOne) {
	const Database& db = data();
	Battle battle = makeBattle(db, {"twin_ooze", "twin_ooze"}, 10);
	for (int i = 0; i < kFirstEnemy; ++i) {
		sturdy(battle, i);
	}
	partyDefends(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> left = ofType(events, EventType::EnemyLeft);
	const std::vector<Event> changed = ofType(events, EventType::EnemyTransformed);
	CHECK_EQ(left.size(), std::size_t(1));
	CHECK(changed.size() == 1 && changed[0].ids == std::vector<std::string>{"grand_ooze"});
	int standing = 0;
	for (int i = kFirstEnemy; i < static_cast<int>(battle.combatants().size()); ++i) {
		standing += battle.combatant(i).alive() ? 1 : 0;
	}
	CHECK_EQ(standing, 1);
	for (int i = kFirstEnemy; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).hp = std::min(battle.combatant(i).hp, 1);
	}
	playToEnd(battle, 10);
	CHECK(battle.outcome() == Outcome::Victory);
	CHECK_EQ(battle.rewards().experience, db.enemies.at("grand_ooze").experience);
}

// B15: an attack on every enemy makes the crab answer with its heavy counter at its next turn.
TEST(aGroupAttackDrawsTheHeavyCounter) {
	const Database& db = data();
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		Battle battle = makeBattle(db, {"mirror_crab"}, seed);
		for (int i = 0; i < kFirstEnemy; ++i) {
			sturdy(battle, i);
		}
		sturdy(battle, kFirstEnemy);
		battle.setCommand(kMystic, Command::ability("frostbite", -1));
		battle.setCommand(kFirstEnemy, Command::defend());
		othersDefend(battle);
		battle.resolveRound();
		CHECK(battle.combatant(kFirstEnemy).hitByGroup);
		partyDefends(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::optional<Event> answer = firstBy(events, EventType::ActionStarted, kFirstEnemy);
		CHECK(answer && answer->ability == "crushing_claw");
		CHECK(!battle.combatant(kFirstEnemy).hitByGroup);
	}
}

// B16: the colossus takes its second form at full hit points in place of falling; the last form pays out.
TEST(aBossTakesItsNextFormInPlaceOfFalling) {
	const Database& db = data();
	BattleSetup setup = makeSetup(db, {}, 11);
	useEncounter(setup, db, "colossus");
	Battle battle = makeBattle(db, setup);
	battle.combatant(kFirstEnemy).hp = 1;
	battle.setCommand(kEngineer, Command::ability("flare_burst", -1));
	battle.setCommand(kFirstEnemy, Command::defend());
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> changed = ofType(events, EventType::EnemyTransformed);
	CHECK(changed.size() == 1 && changed[0].ids == std::vector<std::string>{"tide_colossus_core"});
	CHECK(ofType(events, EventType::CombatantDefeated).empty());
	CHECK_EQ(battle.combatant(kFirstEnemy).defId, std::string("tide_colossus_core"));
	CHECK_EQ(battle.combatant(kFirstEnemy).hp, db.enemies.at("tide_colossus_core").stats.maxHp);
	CHECK(battle.outcome() == Outcome::Ongoing);
	battle.combatant(kFirstEnemy).hp = 1;
	playToEnd(battle, 10);
	CHECK(battle.outcome() == Outcome::Victory);
	CHECK_EQ(battle.rewards().experience, db.enemies.at("tide_colossus_core").experience);
}

// The ambush reaction fires at the enemy's first turn after a Party first opening, and only then.
TEST(theAmbushReactionFiresAtTheFirstTurnAfterAPartyOpening) {
	Database db = data();
	EnemyDef& sentinel = db.enemies["aegis_sentinel"];
	sentinel.slots.assign(static_cast<std::size_t>(kAiSlots), "bite");
	for (Opening opening : {Opening::PartyFirst, Opening::Normal}) {
		BattleSetup setup = makeSetup(db, {"aegis_sentinel"}, 12);
		setup.opening = opening;
		Battle battle = makeBattle(db, setup);
		for (int i = 0; i < kFirstEnemy; ++i) {
			sturdy(battle, i);
		}
		std::vector<std::string> actions;
		for (int round = 0; round < 3; ++round) {
			partyDefends(battle);
			const std::optional<Event> action = firstBy(battle.resolveRound(), EventType::ActionStarted, kFirstEnemy);
			if (action) {
				actions.push_back(action->ability);
			}
		}
		if (opening == Opening::PartyFirst) {
			CHECK(actions.size() == 2 && actions[0] == "hamstring" && actions[1] == "bite");
		} else {
			CHECK(std::count(actions.begin(), actions.end(), std::string("hamstring")) == 0);
		}
	}
}

// B4 as data: the twins' chorus needs the whole group standing; once one falls the other bites.
TEST(anEnemyGroupActsTogetherOnlyWhileItStandsWhole) {
	const Database& db = data();
	Battle battle = makeBattle(db, {"echo_twin", "echo_twin"}, 13);
	for (int i = 0; i < kFirstEnemy; ++i) {
		sturdy(battle, i);
	}
	partyDefends(battle);
	std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> chorus = firstBy(events, EventType::ActionStarted, kFirstEnemy);
	CHECK(chorus && chorus->ability == "twin_chorus");
	battle.combatant(kFirstEnemy + 1).hp = 0;
	battle.combatant(kFirstEnemy + 1).statuses = {{kDefeatedStatus, 0}};
	partyDefends(battle);
	events = battle.resolveRound();
	const std::optional<Event> alone = firstBy(events, EventType::ActionStarted, kFirstEnemy);
	CHECK(alone && alone->ability == "bite");
}

// B6: an override can depend on another kind of enemy being in the battle.
TEST(overridesCanLookForAnotherKindOfEnemy) {
	Database db = data();
	EnemyDef& hound = db.enemies["rust_hound"];
	hound.slots.assign(static_cast<std::size_t>(kAiSlots), "bite");
	AiOverride present;
	present.when = AiConditionKind::AllyPresent;
	present.enemy = "bog_wisp";
	present.ability = "venom_spit";
	hound.overrides = {present};
	for (const bool withWisp : {true, false}) {
		Battle battle = makeBattle(db, withWisp ? std::vector<std::string>{"rust_hound", "bog_wisp"} : std::vector<std::string>{"rust_hound", "gel_mite"}, 14);
		for (int i = 0; i < kFirstEnemy; ++i) {
			sturdy(battle, i);
		}
		battle.setCommand(kFirstEnemy + 1, Command::defend());
		partyDefends(battle);
		const std::optional<Event> action = firstBy(battle.resolveRound(), EventType::ActionStarted, kFirstEnemy);
		CHECK(action && action->ability == (withWisp ? "venom_spit" : "bite"));
	}
}

// B3: an ability of the recipe ability's family fills its place.
TEST(anAbilityOfTheSameFamilyFillsARecipe) {
	Database db = data();
	for (auto& [id, character] : db.characters) {
		character.base.agility = 0;
	}
	for (auto& [id, enemy] : db.enemies) {
		enemy.stats.agility = 0;
	}
	db.characters["mystic"].base.agility = 400;
	db.characters["scout"].base.agility = 200;
	db.characters["mystic"].abilities.push_back("bright_spark");
	Battle battle = makeBattle(db, {"brass_warden"}, 15);
	battle.setCommand(kMystic, Command::ability("bright_spark", kFirstEnemy));
	battle.setCommand(kScout, Command::ability("gust", kFirstEnemy));
	battle.setCommand(kFirstEnemy, Command::defend());
	othersDefend(battle);
	const std::vector<Event> combos = ofType(battle.resolveRound(), EventType::ComboTriggered);
	CHECK(combos.size() == 1 && combos[0].combo == "storm_spark");
}

// B2: an enemy that fell earlier in the round no longer stands between two parts.
TEST(aFallenEnemyNoLongerSeparatesComboParts) {
	Database db = data();
	db.characters["engineer"].base.agility = 400;
	int checked = 0;
	for (std::uint64_t seed = 1; seed <= 3000 && checked < 3; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite", "brass_warden"}, seed);
		battle.combatant(kFirstEnemy).hp = 1;
		sturdy(battle, kFirstEnemy + 1);
		battle.setCommand(kEngineer, Command::ability("flare_burst", -1));
		battle.setCommand(kScout, Command::ability("gust", kFirstEnemy + 1));
		battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy + 1));
		battle.setCommand(kFirstEnemy, Command::defend());
		battle.setCommand(kFirstEnemy + 1, Command::defend());
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<int>& order = battle.lastTurnOrder();
		const auto scout = std::find(order.begin(), order.end(), kScout);
		if (order.front() != kEngineer || scout + 2 >= order.end() || *(scout + 1) != kFirstEnemy || *(scout + 2) != kMystic) {
			continue;
		}
		++checked;
		const std::vector<Event> combos = ofType(events, EventType::ComboTriggered);
		CHECK(combos.size() == 1 && combos[0].participants == (std::vector<int>{kScout, kMystic}));
	}
	CHECK_EQ(checked, 3);
}

// B5: a one-ally raise in a macro goes to the first member in line, as the original's pick does.
TEST(aMacroRaiseOnOneAllyGoesToTheFirstMemberInLine) {
	Database db = data();
	AbilityDef bolster;
	bolster.id = "bolster";
	bolster.name = "Bolster";
	bolster.kind = AbilityKind::Technique;
	bolster.cost = 1;
	bolster.targeting = Targeting::OneAlly;
	Effect raise;
	raise.kind = EffectKind::ModifyStat;
	raise.stat = Stat::Defense;
	raise.raises = true;
	bolster.effects = {raise};
	db.abilities["bolster"] = bolster;
	db.characters["medic"].abilities.push_back("bolster");
	Battle battle = makeBattle(db, {"brass_warden"}, 16);
	battle.setCommand(kFirstEnemy, Command::defend());
	Macro macro;
	macro.name = "guard";
	MacroStep step;
	step.member = "medic";
	step.kind = CommandKind::Ability;
	step.id = "bolster";
	step.target.kind = MacroTargetKind::Member;
	step.target.member = "engineer";
	macro.steps = {step};
	battle.applyMacro(macro);
	othersDefend(battle);
	const std::optional<Event> cast = firstBy(battle.resolveRound(), EventType::ActionStarted, kMedic);
	CHECK(cast && cast->target == kFighter);
}

// P12: a worn piece casts its ability through the Item command, with nothing paid and nothing used up.
TEST(aWornPieceCastsItsAbilityWithoutBeingUsedUp) {
	const Database& db = data();
	BattleSetup setup = makeSetup(db, {"brass_warden"}, 17);
	setup.party[kMystic].equipment[EquipSlot::Weapon] = "storm_rod";
	Battle battle = makeBattle(db, setup);
	sturdy(battle, kFirstEnemy);
	const int tp = battle.combatant(kMystic).tp;
	CHECK(battle.setCommand(kMystic, Command::item("storm_rod", kFirstEnemy)).empty());
	CHECK(!battle.setCommand(kScout, Command::item("storm_rod", kFirstEnemy)).empty());
	battle.setCommand(kFirstEnemy, Command::defend());
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> used = firstBy(events, EventType::ActionStarted, kMystic);
	CHECK(used && used->command == CommandKind::Item && used->ability == "storm_rod");
	CHECK(!firstBy(events, EventType::ResourceSpent, kMystic).has_value());
	CHECK_EQ(battle.combatant(kMystic).tp, tp);
	bool gale = false;
	for (const Event& e : ofType(events, EventType::DamageDealt)) {
		gale = gale || (e.actor == kMystic && e.element == Element::Gale);
	}
	CHECK(gale);
}

// P11: a thrown weapon's attack reaches every enemy.
TEST(aThrownWeaponHitsEveryEnemy) {
	const Database& db = data();
	BattleSetup setup = makeSetup(db, {"gel_mite", "rust_hound", "bog_wisp"}, 18);
	setup.party[kScout].equipment[EquipSlot::Weapon] = "arc_disc";
	Battle battle = makeBattle(db, setup);
	for (int i = kFirstEnemy; i < kFirstEnemy + 3; ++i) {
		sturdy(battle, i);
	}
	battle.setCommand(kScout, Command::attack(kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> attack = firstBy(events, EventType::ActionStarted, kScout);
	CHECK(attack && attack->ability == "disc_throw" && attack->target == -1);
	int reached = 0;
	for (const Event& e : events) {
		reached += (e.type == EventType::Hit || e.type == EventType::Missed) && e.actor == kScout ? 1 : 0;
	}
	CHECK_EQ(reached, 3);
}

// B19: the vehicle's skill uses carry over from the setup and come back out for the next battle.
TEST(vehicleSkillUsesCarryBetweenBattles) {
	const Database& db = data();
	BattleSetup setup = makeSetup(db, {"brass_warden"}, 19);
	setup.flags.vehicleBattle = true;
	setup.vehicle = "reed_skimmer";
	setup.vehicleSkillUses = {{"hull_vent", 0}};
	Battle battle = makeBattle(db, setup);
	const int vehicle = static_cast<int>(setup.party.size());
	sturdy(battle, vehicle + 1);
	battle.setCommand(vehicle, Command::ability("hull_vent", vehicle + 1));
	battle.setCommand(vehicle + 1, Command::defend());
	std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> refused = firstBy(events, EventType::ActionCancelled, vehicle);
	CHECK(refused && refused->cancel == CancelReason::NoUsesLeft);
	battle.setCommand(vehicle, Command::ability("hull_patch", vehicle));
	battle.setCommand(vehicle + 1, Command::defend());
	battle.resolveRound();
	const std::map<std::string, int> after = battle.vehicleSkillUses();
	CHECK_EQ(after.at("hull_vent"), 0);
	CHECK_EQ(after.at("hull_patch"), db.abilities.at("hull_patch").uses - 1);
	CHECK(makeBattle(db, makeSetup(db, {"gel_mite"}, 1)).vehicleSkillUses().empty());
}

// P8: a found scroll teaches its ability for good; a second one teaches nothing and stays in the bag.
TEST(aScrollTeachesItsAbilityForGood) {
	const Database& db = data();
	std::vector<PartyMember> party = {makePartyMember(db, *db.character("fighter"), 1)};
	Inventory inventory = {{"hallow_scroll", 2}};
	const FieldUse first = useItemOutsideBattle(db, party, inventory, "hallow_scroll", 0);
	CHECK(first.rejection.empty());
	const std::vector<std::string> known = knownAbilities(db, party[0]);
	CHECK(std::find(known.begin(), known.end(), "hallow") != known.end());
	CHECK_EQ(inventory.at("hallow_scroll"), 1);
	const FieldUse again = useItemOutsideBattle(db, party, inventory, "hallow_scroll", 0);
	CHECK(!ofType(again.events, EventType::ItemHadNoEffect).empty());
	CHECK_EQ(inventory.at("hallow_scroll"), 1);

	BattleSetup setup = makeSetup(db, {"gel_mite"}, 20);
	setup.party[kFighter] = party[0];
	Battle battle = makeBattle(db, setup);
	CHECK(battle.setCommand(kFighter, Command::ability("hallow", kFirstEnemy)).empty());
	const Result<BattleRecord> record = parseRecord(serializeRecord(battle.record()));
	CHECK(record.ok() && record.value->setup.party[kFighter].taught == std::vector<std::string>{"hallow"});
	CHECK(!battle.setCommand(kMystic, Command::item("hallow_scroll", kFighter)).empty());
}

TEST(theLevelCapIs99) {
	const Database& db = data();
	PartyMember member = makePartyMember(db, *db.character("fighter"), 1);
	grantExperience(member, db, 99999999);
	CHECK_EQ(member.level, 99);
}

TEST(loaderChecksTheNewEnemyLinks) {
	auto files = dataTexts();
	replaceOnce(files["enemies.json"], "\"summons\": \"gel_mite\",", "");
	expectError(files, "ability 'call_kin' summons, so the enemy needs summons");

	files = dataTexts();
	replaceOnce(files["enemies.json"], "\"basic_attack\": \"bite\",\n\t\t\t\"abilities\": [\"core_flare\"],", "\"basic_attack\": \"bite\",\n\t\t\t\"next_form\": \"tide_colossus\",\n\t\t\t\"abilities\": [\"core_flare\"],");
	expectError(files, "the chain of next forms comes back to 'tide_colossus'");

	files = dataTexts();
	replaceOnce(files["enemies.json"], "\"merges_into\": \"grand_ooze\",", "\"merges_into\": \"great_ooze\",");
	expectError(files, "names missing enemy 'great_ooze'");

	files = dataTexts();
	replaceOnce(files["items.json"], "{\"type\": \"teach\", \"ability\": \"hallow\"}", "{\"type\": \"teach\", \"ability\": \"hallow\"},\n\t\t\t{\"type\": \"heal\", \"power\": 5}");
	expectError(files, "a teach effect must be the only effect");

	files = dataTexts();
	replaceOnce(files["abilities.json"], "{\"type\": \"heal\", \"kinds\": [\"machine\"], \"power\": 40}", "{\"type\": \"heal\", \"kinds\": [\"robot\"], \"power\": 40}");
	expectError(files, "unknown kind 'robot'");
}

// Party members marked vulnerable, or of a kind rules.json `instant_defeat_kinds` names, can be felled by an enemy's
// instant defeat, rolled against their affinity for its element; the others are immune.
TEST(enemyInstantDefeatFellsOnlyVulnerableMembers) {
	for (bool byKind : {false, true}) {
		Database db = data();
		db.characters["engineer"].vulnerable = !byKind;
		if (byKind) {
			db.battleRules.instantDefeatKinds = {CreatureKind::Living};
		}
		int felled = 0;
		int immune = 0;
		for (std::uint64_t seed = 1; seed <= 20; ++seed) {
			Battle battle = makeBattle(db, {"gloam_reaper"}, seed);
			partyDefends(battle);
			for (const Event& e : ofType(battle.resolveRound(), EventType::Message)) {
				felled += e.message == "battle.instant_defeat" ? 1 : 0;
				immune += e.message == "battle.instant_defeat_immune" ? 1 : 0;
				CHECK(e.message != "battle.instant_defeat" || byKind || e.target == kEngineer);
			}
		}
		CHECK(felled > 0);
		CHECK_EQ(immune > 0, !byKind);
	}
	Database holy = data();
	holy.characters["engineer"].vulnerable = true;
	holy.characters["engineer"].affinities[Element::Holy] = 0;
	Battle battle = makeBattle(holy, {"gloam_reaper"}, 1);
	partyDefends(battle);
	for (const Event& e : ofType(battle.resolveRound(), EventType::Message)) {
		CHECK(e.message == "battle.instant_defeat_immune");
	}
}

// `next_form_at`: the enemy takes its next form once its hit points fall to that percent, not only when it falls.
TEST(aNextFormCanComeAtAPercentOfHitPoints) {
	const Database& db = data();
	Battle battle = makeBattle(db, {"shellback"}, 2);
	battle.combatant(kFirstEnemy).hp = 71;
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> changed = ofType(events, EventType::EnemyTransformed);
	CHECK_EQ(changed.size(), std::size_t(1));
	CHECK(!changed.empty() && changed.front().ids == std::vector<std::string>{"shellback_bare"});
	CHECK_EQ(battle.combatant(kFirstEnemy).defId, std::string("shellback_bare"));
	CHECK(battle.combatant(kFirstEnemy).alive());
}
