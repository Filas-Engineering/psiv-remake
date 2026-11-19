#include "fixtures.h"
#include "test.h"

#include <algorithm>

using namespace rules;
using namespace fixtures;

namespace {

const Macro& opener() {
	return *data().macro("opener");
}

MacroStep step(const char* member, CommandKind kind, const char* id, MacroTargetKind target = MacroTargetKind::Default, const char* ally = "", int enemySlot = -1) {
	MacroStep s;
	s.member = member;
	s.kind = kind;
	s.id = id;
	s.target.kind = target;
	s.target.member = ally;
	s.target.enemySlot = enemySlot;
	return s;
}

}

TEST(macroForcesRelativeOrderAndEnemiesKeepTheirTurns) {
	Database db = data();
	db.characters["medic"].base.agility = 300;
	db.characters["fighter"].base.agility = 200;
	db.characters["mystic"].base.agility = 1;
	const std::vector<int> expected = {kMystic, kScout, kFighter, kEngineer, kMedic};
	int interrupted = 0;
	for (std::uint64_t seed = 1; seed <= 20; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite", "rust_hound"}, seed);
		const std::vector<MacroStepResult> results = battle.applyMacro(*db.macro("opener"));
		for (const MacroStepResult& r : results) {
			CHECK(r.status == MacroStepStatus::Applied);
		}
		battle.resolveRound();
		const std::vector<int>& order = battle.lastTurnOrder();
		std::vector<int> party;
		for (int actor : order) {
			if (battle.isParty(actor)) {
				party.push_back(actor);
			}
		}
		CHECK_EQ(party, expected);
		const auto first = std::find(order.begin(), order.end(), expected.front());
		const auto last = std::find(order.begin(), order.end(), expected.back());
		if (first != order.end() && last != order.end() && last - first > 4) {
			++interrupted;
		}
	}
	CHECK(interrupted > 0);
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

TEST(macroKeepsAnUnaffordableStepAndCancelsItAtExecution) {
	Battle battle = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	battle.combatant(kMystic).tp = 1;
	addStatus(battle, kEngineer, "silence", 3);
	const std::vector<MacroStepResult> results = battle.applyMacro(opener());
	CHECK(results[0].status == MacroStepStatus::Applied && results[0].issue == MacroIssue::NotEnoughTp);
	CHECK(battle.command(kMystic).kind == CommandKind::Ability && battle.command(kMystic).id == "spark");
	CHECK_EQ(battle.command(kMystic).target, kFirstEnemy);
	CHECK(results[3].status == MacroStepStatus::Applied && results[3].issue == MacroIssue::Silenced);
	CHECK(battle.command(kEngineer).kind == CommandKind::Ability && battle.command(kEngineer).target == kFirstEnemy + 1);

	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> mystic = firstBy(events, EventType::ActionCancelled, kMystic);
	CHECK(mystic && mystic->cancel == CancelReason::NotEnoughTp && mystic->ability == "spark");
	const std::optional<Event> engineer = firstBy(events, EventType::ActionCancelled, kEngineer);
	CHECK(engineer && engineer->cancel == CancelReason::Silenced);
	CHECK_EQ(battle.combatant(kMystic).tp, 1);
	CHECK(!firstBy(events, EventType::ActionStarted, kMystic).has_value());
}

TEST(macroStepTheBattleWouldRefuseFallsBackToAttack) {
	Macro macro;
	macro.name = "test";
	macro.steps.push_back(step("mystic", CommandKind::Ability, "cleave", MacroTargetKind::Enemy, "", 1));
	Battle battle = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	const std::vector<MacroStepResult> results = battle.applyMacro(macro);
	CHECK(results[0].status == MacroStepStatus::FellBack && results[0].issue == MacroIssue::UnknownAbility);
	CHECK(battle.command(kMystic).kind == CommandKind::Attack);
	CHECK_EQ(battle.command(kMystic).target, kFirstEnemy + 1);
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

TEST(macroReviveWithNobodyDownWaitsForExecution) {
	Macro macro;
	macro.name = "test";
	macro.steps.push_back(step("medic", CommandKind::Ability, "rekindle", MacroTargetKind::Member, "fighter"));
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	std::vector<MacroStepResult> results = battle.applyMacro(macro);
	CHECK(results[0].status == MacroStepStatus::Applied && results[0].issue == MacroIssue::NoTarget);
	CHECK(battle.command(kMedic).kind == CommandKind::Ability && battle.command(kMedic).target == kFighter);
	othersDefend(battle);
	const std::optional<Event> cancelled = firstBy(battle.resolveRound(), EventType::ActionCancelled, kMedic);
	CHECK(cancelled && cancelled->cancel == CancelReason::NoTarget);

	// The fighter falls to a faster enemy first, so the revive the macro kept now lands.
	Database db = data();
	db.characters["medic"].base.agility = 1;
	db.enemies["gel_mite"].stats.agility = 300;
	db.enemies["gel_mite"].stats.dexterity = 500;
	Battle late = makeBattle(db, {"gel_mite"}, 1);
	late.combatant(kFighter).hp = 1;
	late.applyMacro(macro);
	late.setCommand(kFirstEnemy, Command::attack(kFighter));
	othersDefend(late);
	const std::vector<Event> events = late.resolveRound();
	CHECK(!ofType(events, EventType::CombatantDefeated).empty());
	const std::vector<Event> revived = ofType(events, EventType::Revived);
	CHECK(revived.size() == 1 && revived[0].actor == kMedic && revived[0].target == kFighter);

	Battle downed = makeBattle(data(), {"gel_mite"}, 1);
	downed.combatant(kFighter).hp = 0;
	results = downed.applyMacro(macro);
	CHECK(results[0].status == MacroStepStatus::Applied && results[0].issue == MacroIssue::None);
	CHECK_EQ(downed.command(kMedic).target, kFighter);
}

TEST(macroReservesItemsAcrossSteps) {
	Macro macro;
	macro.name = "test";
	macro.steps.push_back(step("fighter", CommandKind::Item, "tonic", MacroTargetKind::Member, "mystic"));
	macro.steps.push_back(step("scout", CommandKind::Item, "tonic", MacroTargetKind::Member, "mystic"));
	Battle battle = makeBattle(data(), {"gel_mite"}, 1, Inventory{{"tonic", 1}});
	battle.combatant(kMystic).hp = 5;
	const std::vector<MacroStepResult> results = battle.applyMacro(macro);
	CHECK(results[0].status == MacroStepStatus::Applied && results[0].issue == MacroIssue::None);
	CHECK(results[1].status == MacroStepStatus::Applied && results[1].issue == MacroIssue::NoItem);
	CHECK(battle.command(kScout).kind == CommandKind::Item);
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> cancelled = firstBy(events, EventType::ActionCancelled, kScout);
	CHECK(cancelled && cancelled->cancel == CancelReason::NoItem);
	CHECK_EQ(battle.inventory().at("tonic"), 0);
}

TEST(macroTargetsAreRulesNotIndexes) {
	Macro macro;
	macro.name = "test";
	macro.steps.push_back(step("mystic", CommandKind::Ability, "spark", MacroTargetKind::Enemy, "", 2));
	macro.steps.push_back(step("scout", CommandKind::Ability, "overclock", MacroTargetKind::Self));
	macro.steps.push_back(step("medic", CommandKind::Ability, "mend", MacroTargetKind::Member, "engineer"));
	macro.steps.push_back(step("fighter", CommandKind::Ability, "rally", MacroTargetKind::All));

	BattleSetup setup = makeSetup(data(), {"gel_mite", "gel_mite", "rust_hound"}, 1);
	std::swap(setup.party[kMedic], setup.party[kEngineer]);
	Battle battle = makeBattle(data(), setup);
	const std::vector<MacroStepResult> results = battle.applyMacro(macro);
	for (const MacroStepResult& r : results) {
		CHECK(r.status == MacroStepStatus::Applied);
	}
	CHECK_EQ(battle.command(kMystic).target, kFirstEnemy + 2);
	CHECK_EQ(battle.command(kScout).target, -1);
	// Medic and engineer swapped places, and the rule still finds the engineer.
	CHECK_EQ(battle.command(kEngineer).target, kMedic);

	setup.party.erase(setup.party.begin() + kMedic);
	Battle smaller = makeBattle(data(), setup);
	const std::vector<MacroStepResult> missing = smaller.applyMacro(macro);
	CHECK(missing[2].status == MacroStepStatus::Retargeted && missing[2].issue == MacroIssue::TargetGone);
	CHECK_EQ(smaller.command(kMedic).target, kMedic);
}

TEST(roundCanBeSavedAsAMacro) {
	Battle battle = makeBattle(data(), {"gel_mite", "rust_hound"}, 4);
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy + 1));
	battle.setCommand(kMedic, Command::ability("mend", kFighter));
	battle.setCommand(kFighter, Command::ability("rally", -1));
	battle.setCommand(kScout, Command::ability("overclock", -1));
	battle.setCommand(kEngineer, Command::defend());
	battle.resolveRound();
	const Macro saved = battle.lastRoundAsMacro("saved");
	CHECK_EQ(saved.name, std::string("saved"));
	CHECK_EQ(saved.steps.size(), std::size_t(5));

	std::vector<int> partyOrder;
	for (int actor : battle.lastTurnOrder()) {
		if (battle.isParty(actor)) {
			partyOrder.push_back(actor);
		}
	}
	for (std::size_t i = 0; i < saved.steps.size() && i < partyOrder.size(); ++i) {
		const MacroStep& s = saved.steps[i];
		CHECK_EQ(s.member, battle.combatant(partyOrder[i]).defId);
		if (s.member == "mystic") {
			CHECK(s.target.kind == MacroTargetKind::Enemy && s.target.enemySlot == 1);
		} else if (s.member == "medic") {
			CHECK(s.target.kind == MacroTargetKind::Member && s.target.member == "fighter");
		} else if (s.member == "fighter") {
			CHECK(s.target.kind == MacroTargetKind::All);
		} else if (s.member == "scout") {
			CHECK(s.target.kind == MacroTargetKind::Self);
		} else {
			CHECK(s.kind == CommandKind::Defend && s.target.kind == MacroTargetKind::Default);
		}
	}

	battle.applyMacro(saved);
	CHECK_EQ(battle.command(kMystic).target, kFirstEnemy + 1);
	CHECK_EQ(battle.command(kMedic).target, kFighter);
	CHECK(battle.command(kEngineer).kind == CommandKind::Defend);
	battle.resolveRound();
	std::vector<int> block;
	for (int actor : battle.lastTurnOrder()) {
		if (battle.isParty(actor)) {
			block.push_back(actor);
		}
	}
	CHECK_EQ(block, partyOrder);
}
