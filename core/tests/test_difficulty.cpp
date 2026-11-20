#include "fixtures.h"
#include "rules/formulas.h"
#include "rules/loader.h"
#include "test.h"

#include <algorithm>

using namespace rules;
using namespace fixtures;

namespace {

// Every enemy has 1 hit point and the fighter cannot miss, so one round wins.
Battle quickWin(const Database& db, BattleSetup setup) {
	Battle battle = makeBattle(db, setup);
	for (int i = kFirstEnemy; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).hp = 1;
	}
	for (int i = 0; i < battle.partyCount(); ++i) {
		battle.setCommand(i, Command::attack(-1));
	}
	for (int round = 0; round < 10 && battle.outcome() == Outcome::Ongoing; ++round) {
		battle.resolveRound();
	}
	return battle;
}

}

TEST(hardOverlayChangesDataOnly) {
	const Database& db = data();
	const Database hard = applyDifficulty(db, *db.difficulty("hard"));
	CHECK_EQ(hard.enemies.at("brass_warden").stats.maxHp, 280 * 115 / 100);
	CHECK_EQ(hard.enemies.at("brass_warden").stats.strength, 18 * 110 / 100);
	CHECK_EQ(hard.enemies.at("brass_warden").stats.defense, 12);
	CHECK_EQ(hard.abilities.at("mend").effects[0].power, 28 * 80 / 100);
	const std::vector<std::string>& abilities = hard.enemies.at("brass_warden").abilities;
	CHECK(std::find(abilities.begin(), abilities.end(), "numbing_sting") != abilities.end());
	CHECK_EQ(hard.enemies.at("brass_warden").slots.size(), std::size_t(kAiSlots));
	CHECK_EQ(hard.enemies.at("brass_warden").slots.front(), std::string("lock_bolt"));
	CHECK(hard.enemies.at("brass_warden").overrides.size() == 1 && hard.enemies.at("brass_warden").overrides[0].target.rule == AiTargetRule::LastAttacker);
	CHECK_EQ(hard.enemies.at("rust_hound").statusResist.at("sleep"), 50);
	CHECK_EQ(hard.enemies.at("gel_mite").stats.maxHp, 60);
}

TEST(battleReadsTheOverlaidData) {
	BattleSetup setup = makeSetup(data(), {"brass_warden"}, 5);
	setup.difficulty = "hard";
	Battle battle = makeBattle(data(), setup);
	CHECK_EQ(battle.combatant(kFirstEnemy).stats.maxHp, 322);
	battle.combatant(kFighter).hp = 1;
	// Low enough that the spread cannot carry the overlaid power into the range of the unscaled one.
	battle.combatant(kMedic).stats.mental = 2;
	battle.setCommand(kMedic, Command::ability("mend", kFighter));
	othersDefend(battle);
	const std::vector<Event> heals = ofType(battle.resolveRound(), EventType::Healed);
	CHECK(heals.size() == 1 && heals[0].amount >= formulas::healAmount(22, 2, formulas::kSpreadMin) && heals[0].amount <= formulas::healAmount(22, 2, formulas::kSpreadMax));
}

TEST(emptyOverlayPlaysLikeNoOverlay) {
	BattleSetup plain = makeSetup(data(), {"brass_warden", "bog_wisp"}, 11);
	BattleSetup original = plain;
	original.difficulty = "original";
	Battle a = makeBattle(data(), plain);
	Battle b = makeBattle(data(), original);
	CHECK_EQ(serializeEvents(playToEnd(a, 40)), serializeEvents(playToEnd(b, 40)));
}

TEST(rewardsScaleAndRoundDown) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	BattleSetup setup = makeSetup(db, {"gel_mite"}, 1);
	setup.rewardMultiplier = RewardMultiplier::X1_5;
	Battle x15 = quickWin(db, setup);
	CHECK(x15.outcome() == Outcome::Victory);
	CHECK_EQ(x15.rewards().experience, 9);
	CHECK_EQ(x15.rewards().currency, 6);

	// Hard pays 120% before the multiplier: 6 * 1.8 = 10.8 and 4 * 1.8 = 7.2.
	setup.difficulty = "hard";
	const Rewards hard = quickWin(db, setup).rewards();
	CHECK_EQ(hard.experience, 10);
	CHECK_EQ(hard.currency, 7);

	setup.rewardMultiplier = RewardMultiplier::X2;
	setup.difficulty.clear();
	CHECK_EQ(quickWin(db, setup).rewards().experience, 12);
}

// Experience is split evenly, rounded down, among the members who are not defeated; asleep and paralyzed members
// take a share, a defeated one none (SAV-031).
TEST(experienceIsSplitAmongMembersNotDefeated) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	BattleSetup setup = makeSetup(db, {"rust_hound", "gel_mite"}, 1);
	setup.party[kMedic].hp = 0;
	Battle battle = makeBattle(db, setup);
	addStatus(battle, kScout, "sleep", 0);
	addStatus(battle, kEngineer, "paralysis", 0);
	for (int i = kFirstEnemy; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).hp = 1;
	}
	for (int round = 0; round < 10 && battle.outcome() == Outcome::Ongoing; ++round) {
		battle.setCommand(kFighter, Command::attack(-1));
		battle.setCommand(kMystic, Command::attack(-1));
		battle.resolveRound();
	}
	CHECK(battle.outcome() == Outcome::Victory);
	CHECK_EQ(battle.rewards().experience, 16);
	CHECK_EQ(battle.rewards().memberExperience, (std::vector<int>{4, 4, 4, 0, 4}));
}

TEST(loaderKeepsHardWithinTheSpecLimits) {
	auto files = fixtures::dataTexts();
	const std::string from = "\"stats_percent\": {\"hp\": 115, \"strength\": 110}";
	const std::size_t at = files["difficulties.json"].find(from);
	CHECK(at != std::string::npos);
	files["difficulties.json"].replace(at, from.size(), "\"stats_percent\": {\"hp\": 130, \"strength\": 110}");
	const Result<Database> loaded = loadDatabaseFromTexts(files);
	CHECK(!loaded.ok() && loaded.error.find("difficulty 'hard': field 'hp' must be between 80 and 120") != std::string::npos);
}
