#include "fixtures.h"
#include "test.h"

#include <algorithm>

using namespace rules;
using namespace fixtures;

namespace {

const Macro& opener() {
	return *data().macro("opener");
}

MacroStep step(const char* member, CommandKind kind, const char* id, int enemySlot, const char* ally = "") {
	MacroStep s;
	s.member = member;
	s.kind = kind;
	s.id = id;
	s.enemySlot = enemySlot;
	s.allyMember = ally;
	return s;
}

}

TEST(macroForcesRelativeOrderAsOneBlock) {
	Database db = data();
	db.characters["medic"].base.agility = 300;
	db.characters["fighter"].base.agility = 200;
	db.characters["mystic"].base.agility = 1;
	const std::vector<int> expected = {kMystic, kScout, kFighter, kEngineer, kMedic};
	for (std::uint64_t seed = 1; seed <= 20; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite", "rust_hound"}, seed);
		const std::vector<MacroStepResult> results = battle.applyMacro(*db.macro("opener"));
		for (const MacroStepResult& r : results) {
			CHECK(r.status == MacroStepStatus::Applied);
		}
		battle.resolveRound();
		const std::vector<int>& order = battle.lastTurnOrder();
		const auto start = std::find(order.begin(), order.end(), kMystic);
		CHECK(start != order.end() && order.end() - start >= 5);
		if (start != order.end() && order.end() - start >= 5) {
			CHECK_EQ(std::vector<int>(start, start + 5), expected);
		}
	}
}

TEST(macroFillsCommandsWithTargets) {
	Battle battle = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	battle.applyMacro(opener());
	CHECK(battle.command(kMystic).kind == CommandKind::Ability);
	CHECK_EQ(battle.command(kMystic).id, std::string("spark"));
	CHECK_EQ(battle.command(kMystic).target, kFirstEnemy);
	CHECK_EQ(battle.command(kEngineer).target, kFirstEnemy + 1);
	CHECK_EQ(battle.command(kMedic).target, kFighter);
	CHECK(battle.command(kFighter).kind == CommandKind::Attack);
}

TEST(macroSkipsDefeatedMembers) {
	Battle battle = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	battle.combatant(kScout).hp = 0;
	const std::vector<MacroStepResult> results = battle.applyMacro(opener());
	CHECK(results[1].status == MacroStepStatus::Skipped);
	CHECK(results[1].issue == MacroIssue::ActorDefeated);
	CHECK(battle.command(kScout).kind == CommandKind::None);
	battle.resolveRound();
	const std::vector<int>& order = battle.lastTurnOrder();
	CHECK(std::find(order.begin(), order.end(), kScout) == order.end());
}

TEST(macroFallsBackToAttackWhenAStepCannotRun) {
	Battle battle = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	battle.combatant(kMystic).tp = 1;
	addStatus(battle, kEngineer, "silence", 3);
	const std::vector<MacroStepResult> results = battle.applyMacro(opener());
	CHECK(results[0].status == MacroStepStatus::FellBack);
	CHECK(results[0].issue == MacroIssue::NotEnoughTp);
	CHECK(battle.command(kMystic).kind == CommandKind::Attack);
	CHECK_EQ(battle.command(kMystic).target, kFirstEnemy);
	CHECK(results[3].issue == MacroIssue::Silenced);
	CHECK_EQ(battle.command(kEngineer).target, kFirstEnemy + 1);
}

TEST(macroRetargetsWhenTheTargetIsGone) {
	Battle battle = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	battle.combatant(kFirstEnemy).hp = 0;
	const std::vector<MacroStepResult> results = battle.applyMacro(opener());
	CHECK(results[0].status == MacroStepStatus::Retargeted);
	CHECK(results[0].issue == MacroIssue::TargetGone);
	CHECK_EQ(battle.command(kMystic).target, kFirstEnemy + 1);
	CHECK_EQ(battle.command(kMystic).id, std::string("spark"));
}

TEST(macroReviveWithNobodyDownFallsBack) {
	Macro macro;
	macro.name = "test";
	macro.steps.push_back(step("medic", CommandKind::Ability, "rekindle", -1, "fighter"));
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	std::vector<MacroStepResult> results = battle.applyMacro(macro);
	CHECK(results[0].status == MacroStepStatus::FellBack);
	CHECK(results[0].issue == MacroIssue::NoTarget);
	CHECK(battle.command(kMedic).kind == CommandKind::Attack);

	Battle downed = makeBattle(data(), {"gel_mite"}, 1);
	downed.combatant(kFighter).hp = 0;
	results = downed.applyMacro(macro);
	CHECK(results[0].status == MacroStepStatus::Applied);
	CHECK_EQ(downed.command(kMedic).target, kFighter);
}

TEST(macroReservesItemsAcrossSteps) {
	Macro macro;
	macro.name = "test";
	macro.steps.push_back(step("fighter", CommandKind::Item, "tonic", -1, "mystic"));
	macro.steps.push_back(step("scout", CommandKind::Item, "tonic", -1, "mystic"));
	Battle battle = makeBattle(data(), {"gel_mite"}, 1, Inventory{{"tonic", 1}});
	const std::vector<MacroStepResult> results = battle.applyMacro(macro);
	CHECK(results[0].status == MacroStepStatus::Applied);
	CHECK(results[1].status == MacroStepStatus::FellBack);
	CHECK(results[1].issue == MacroIssue::NoItem);
	CHECK(battle.command(kScout).kind == CommandKind::Attack);
}
