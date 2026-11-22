#include "fixtures.h"
#include "rules/formulas.h"
#include "rules/progression.h"
#include "test.h"

using namespace rules;
using namespace fixtures;

TEST(seededBattleHasAFixedOutcome) {
	// Golden values: if a formula or the data changes on purpose, update them alongside.
	ComboLog log;
	BattleSetup setup = makeSetup(data(), {}, 1);
	CHECK(useEncounter(setup, data(), "warden").empty());
	Battle battle = makeBattle(data(), setup, &log);
	const std::vector<Event> events = playToEnd(battle, 40);
	CHECK(battle.outcome() == Outcome::Victory);
	CHECK_EQ(battle.round(), 3);
	std::vector<int> hp;
	for (int i = 0; i < battle.partyCount(); ++i) {
		hp.push_back(battle.combatant(i).hp);
	}
	CHECK_EQ(hp, (std::vector<int>{29, 38, 0, 42, 75}));
	CHECK_EQ(battle.rewards().experience, 72);
	CHECK_EQ(battle.rewards().currency, 59);
	CHECK(log.isDiscovered("storm_spark"));
	CHECK(ofType(events, EventType::Revived).empty());
	CHECK(events.back().type == EventType::BattleEnded && events.back().outcome == Outcome::Victory);
	CHECK(battle.resolveRound().empty());
}

TEST(partyCanBeDefeated) {
	Battle battle = makeBattle(data(), {"brass_warden", "rust_hound"}, 3);
	for (int i = 0; i < battle.partyCount(); ++i) {
		battle.combatant(i).hp = 1;
	}
	std::vector<Event> last;
	for (int round = 0; round < 30 && battle.outcome() == Outcome::Ongoing; ++round) {
		partyDefends(battle);
		last = battle.resolveRound();
	}
	CHECK(battle.outcome() == Outcome::Defeat);
	CHECK(!last.empty() && last.back().outcome == Outcome::Defeat);
	CHECK_EQ(battle.rewards().experience, 0);
}

TEST(escapingEndsTheBattleWithoutRewards) {
	int fled = 0;
	for (std::uint64_t seed = 1; seed <= 20; ++seed) {
		Battle battle = makeBattle(data(), {"gel_mite"}, seed);
		battle.setCommand(kScout, Command::escape());
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::optional<Event> attempt = firstBy(events, EventType::EscapeAttempted, kScout);
		CHECK(attempt.has_value());
		if (attempt && attempt->succeeded) {
			++fled;
			CHECK(battle.outcome() == Outcome::Escaped);
			CHECK(events.back().type == EventType::BattleEnded);
			CHECK_EQ(battle.rewards().currency, 0);
		} else {
			CHECK(battle.outcome() == Outcome::Ongoing);
		}
	}
	CHECK(fled > 0 && fled < 20);
}

TEST(itemsUseFlatPowerAndAreConsumed) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 1);
	battle.combatant(kFighter).hp = 10;
	battle.combatant(kScout).hp = 0;
	battle.setCommand(kMystic, Command::item("tonic", kFighter));
	battle.setCommand(kMedic, Command::item("second_wind", kScout));
	battle.setCommand(kEngineer, Command::item("second_wind", kScout));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> heals = ofType(events, EventType::Healed);
	CHECK(heals.size() == 1 && heals[0].target == kFighter && heals[0].amount == 30);
	const std::vector<Event> revives = ofType(events, EventType::Revived);
	CHECK(revives.size() == 1 && revives[0].amount == formulas::reviveHp(battle.combatant(kScout).stats.maxHp, 30));
	CHECK_EQ(battle.inventory().at("tonic"), 2);
	CHECK_EQ(battle.inventory().at("second_wind"), 0);
	// The second revive finds the stock empty.
	CHECK_EQ(ofType(events, EventType::ActionCancelled).size(), std::size_t(1));
	CHECK(ofType(events, EventType::ActionCancelled)[0].cancel == CancelReason::NoItem);
}

TEST(reviveTechniqueNeedsADefeatedAlly) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 1);
	const int tp = battle.combatant(kMedic).tp;
	CHECK_EQ(battle.setCommand(kMedic, Command::ability("rekindle", kScout)).key, std::string("command.no_effect_on_target"));
	othersDefend(battle);
	battle.resolveRound();
	CHECK_EQ(battle.combatant(kMedic).tp, tp);

	battle.combatant(kScout).hp = 0;
	battle.combatant(kScout).statuses = {{kDefeatedStatus, 0}};
	battle.setCommand(kMedic, Command::ability("rekindle", kScout));
	othersDefend(battle);
	battle.resolveRound();
	CHECK_EQ(battle.combatant(kScout).hp, formulas::reviveHp(battle.combatant(kScout).stats.maxHp, 50));
	CHECK(!battle.combatant(kScout).hasStatus(kDefeatedStatus));
	CHECK_EQ(battle.combatant(kMedic).tp, tp - 10);
}

TEST(setCommandRejectsInvalidCommands) {
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	CHECK(!battle.setCommand(kFighter, Command::ability("spark", kFirstEnemy)).empty());
	CHECK(!battle.setCommand(kMystic, Command::ability("storm_spark", kFirstEnemy)).empty());
	CHECK(!battle.setCommand(kFirstEnemy, Command::escape()).empty());
	CHECK(!battle.setCommand(kFighter, Command::item("nothing", -1)).empty());
	CHECK(!battle.setCommand(99, Command::defend()).empty());
	battle.combatant(kScout).hp = 0;
	CHECK(!battle.setCommand(kScout, Command::defend()).empty());
	CHECK(battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy)).empty());
}

TEST(createValidatesTheSetup) {
	const Database& db = data();
	BattleSetup setup = makeSetup(db, {"gel_mite"}, 1);
	setup.party.push_back(makePartyMember(db, db.characters.at("fighter"), 1));
	CHECK(!Battle::create(db, setup, nullptr).ok());
	setup.party.resize(2);
	setup.enemies.clear();
	CHECK(!Battle::create(db, setup, nullptr).ok());
	setup.enemies = {{"ghost", "ghost"}};
	const Result<Battle> unknown = Battle::create(db, setup, nullptr);
	CHECK(!unknown.ok() && unknown.error == "unknown enemy 'ghost'");
	setup.enemies = {{"gel_mite", "gel_mite"}};
	setup.difficulty = "nightmare";
	CHECK(!Battle::create(db, setup, nullptr).ok());
	setup.difficulty.clear();
	setup.party[1].equipment[EquipSlot::Weapon] = "copper_blade";
	const Result<Battle> wrongGear = Battle::create(db, setup, nullptr);
	CHECK(!wrongGear.ok() && wrongGear.error == "'mystic' cannot wear 'copper_blade' (equip.wrong_category)");
	setup.party[1].equipment[EquipSlot::Weapon] = "ember_rod";
	CHECK(Battle::create(db, setup, nullptr).ok());
}

TEST(partyStateCarriesResourcesOutOfBattle) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 1);
	battle.setCommand(kFighter, Command::ability("focus_shot", kFirstEnemy));
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
	othersDefend(battle);
	battle.resolveRound();
	std::vector<PartyMember> party = battle.partyState();
	CHECK_EQ(party[kFighter].skillUses.at("focus_shot"), 2);
	CHECK_EQ(party[kMystic].tp, battle.combatant(kMystic).stats.maxTp - 3);

	rest(party[kFighter], data());
	rest(party[kMystic], data());
	CHECK_EQ(party[kFighter].skillUses.at("focus_shot"), 3);
	CHECK_EQ(party[kMystic].tp, battle.combatant(kMystic).stats.maxTp);
}

TEST(experienceLevelsUpWithGrowth) {
	const Database& db = data();
	const CharacterDef& fighter = db.characters.at("fighter");
	PartyMember member = makePartyMember(db, fighter, 1);
	CHECK_EQ(formulas::experienceToNextLevel(1), 25);
	CHECK_EQ(formulas::experienceToNextLevel(2), 70);
	CHECK_EQ(grantExperience(member, db, 24).levelsGained, 0);
	CHECK_EQ(member.level, 1);
	const LevelUp levelUp = grantExperience(member, db, 76);
	CHECK_EQ(levelUp.levelsGained, 2);
	CHECK_EQ(levelUp.learned, (std::vector<std::string>{"cleave"}));
	CHECK_EQ(member.level, 3);
	CHECK_EQ(member.experience, 5);
	// Base 60 and padded coat 10: the levels raise the maximum by two growths of 8 but do not heal.
	CHECK_EQ(member.hp, 60 + 10);
	CHECK_EQ(memberStats(db, member).maxHp, 60 + 10 + 2 * 8);
	CHECK_EQ(statsAtLevel(fighter, 3).strength, 18);
	CHECK_EQ(statsAtLevel(fighter, 3).level, 3);
	CHECK_EQ(statsAtLevel(fighter, 500).level, formulas::kMaxLevel);
}

TEST(itemWithNoEffectIsKept) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 1);
	battle.combatant(kScout).hp = 10;
	battle.setCommand(kFighter, Command::item("tonic", kMystic));
	battle.setCommand(kMystic, Command::item("clear_gel", kMedic));
	battle.setCommand(kMedic, Command::item("tonic", kScout));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> kept = ofType(events, EventType::ItemHadNoEffect);
	CHECK_EQ(kept.size(), std::size_t(2));
	const std::vector<Event> spent = ofType(events, EventType::ResourceSpent);
	CHECK(spent.size() == 1 && spent[0].actor == kMedic && spent[0].resource == ResourceKind::Item);
	CHECK_EQ(battle.inventory().at("tonic"), 2);
	CHECK_EQ(battle.inventory().at("clear_gel"), 2);
}

TEST(spentResourcesAppearAsEvents) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 1);
	battle.setCommand(kFighter, Command::ability("focus_shot", kFirstEnemy));
	battle.setCommand(kMystic, Command::ability("hush", kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> spent = ofType(battle.resolveRound(), EventType::ResourceSpent);
	CHECK_EQ(spent.size(), std::size_t(2));
	for (const Event& e : spent) {
		CHECK(e.actor == kFighter ? (e.resource == ResourceKind::SkillUse && e.amount == 1) : (e.resource == ResourceKind::Tp && e.amount == 3));
	}
}

namespace {

void makeSturdy(Battle& battle) {
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).stats.maxHp = 9999;
		battle.combatant(i).hp = 9999;
	}
}

}

TEST(survivingTheRoundsEndsTheBattleAsScripted) {
	BattleSetup setup = makeSetup(data(), {"gel_mite", "rust_hound"}, 4);
	setup.scriptedEnd = {ScriptedEndKind::SurviveRounds, 2, -1};
	Battle battle = makeBattle(data(), setup);
	makeSturdy(battle);
	partyDefends(battle);
	battle.resolveRound();
	CHECK(battle.outcome() == Outcome::Ongoing);
	partyDefends(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK(battle.outcome() == Outcome::ScriptedEnd);
	CHECK(events.back().type == EventType::BattleEnded && events.back().outcome == Outcome::ScriptedEnd);
	CHECK_EQ(battle.rewards().experience, 0);
	CHECK(battle.resolveRound().empty());
}

TEST(defeatingTheNamedEnemyEndsTheBattleAsScripted) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	db.characters["fighter"].base.agility = 900;
	BattleSetup setup = makeSetup(db, {"gel_mite", "rust_hound", "gel_mite"}, 2);
	setup.scriptedEnd = {ScriptedEndKind::EnemyDefeated, 0, 1};
	Battle battle = makeBattle(db, setup);
	battle.combatant(kFirstEnemy + 1).hp = 1;
	battle.setCommand(kFighter, Command::attack(kFirstEnemy + 1));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK(battle.outcome() == Outcome::ScriptedEnd);
	// The fighter acts first and nothing acts after the condition is met.
	CHECK_EQ(ofType(events, EventType::ActionStarted).size(), std::size_t(1));
	// Only the fallen enemy pays out.
	CHECK_EQ(battle.rewards().experience, 10);
	CHECK_EQ(events.back().experience, 10);

	// When the named enemy is the last one, the battle is a plain Victory.
	setup.enemies = {{"rust_hound", "rust_hound"}};
	setup.scriptedEnd.enemySlot = 0;
	Battle alone = makeBattle(db, setup);
	alone.combatant(kFirstEnemy).hp = 1;
	alone.setCommand(kFighter, Command::attack(kFirstEnemy));
	othersDefend(alone);
	alone.resolveRound();
	CHECK(alone.outcome() == Outcome::Victory);

	setup.scriptedEnd.enemySlot = 3;
	CHECK(!Battle::create(db, setup, nullptr).ok());
	setup.scriptedEnd = {ScriptedEndKind::SurviveRounds, 0, -1};
	CHECK(!Battle::create(db, setup, nullptr).ok());
}

TEST(defeatWithoutABranchIsAGameOver) {
	for (bool branch : {false, true}) {
		BattleSetup setup = makeSetup(data(), {"brass_warden"}, 9);
		setup.flags.defeatHasBranch = branch;
		Battle battle = makeBattle(data(), setup);
		for (int i = 0; i < battle.partyCount(); ++i) {
			battle.combatant(i).hp = 1;
		}
		std::vector<Event> last;
		for (int round = 0; round < 30 && battle.outcome() == Outcome::Ongoing; ++round) {
			partyDefends(battle);
			last = battle.resolveRound();
		}
		CHECK(battle.outcome() == Outcome::Defeat);
		CHECK_EQ(battle.gameOver(), !branch);
		CHECK(!last.empty() && last.back().gameOver == !branch);
	}
}

TEST(hardSeededBattleHasAFixedOutcome) {
	// Golden values for the Hard overlay's target rules and added abilities; the CLI test plays the same battle.
	ComboLog log;
	BattleSetup setup = makeSetup(data(), {}, 11);
	CHECK(useEncounter(setup, data(), "warden").empty());
	setup.difficulty = "hard";
	setup.rewardMultiplier = RewardMultiplier::X1_5;
	Battle battle = makeBattle(data(), setup, &log);
	playToEnd(battle, 50);
	CHECK(battle.outcome() == Outcome::Victory);
	CHECK_EQ(battle.round(), 4);
	CHECK(!battle.gameOver());
	CHECK(log.isDiscovered("storm_spark"));
}

TEST(travelItemsAndTechniquesAreRefusedInBattle) {
	BattleSetup setup = makeSetup(data(), {"gel_mite"}, 3);
	setup.inventory["homing_shell"] = 1;
	setup.party[kMystic] = makePartyMember(data(), data().characters.at("mystic"), 9);
	Battle battle = makeBattle(data(), setup);
	CHECK_EQ(battle.setCommand(kFighter, Command::item("homing_shell", -1)).key, std::string("command.field_only"));
	CHECK_EQ(battle.setCommand(kMystic, Command::ability("homeward", -1)).key, std::string("command.field_only"));
}
