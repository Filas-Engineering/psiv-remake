#include "fixtures.h"
#include "rules/auto_policy.h"
#include "rules/loader.h"
#include "test.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

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

// In a macro round a single-enemy action ignores its stored target and picks by hit points missing every time.
TEST(macroFoeTargetsArePickedAfreshEachRound) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	Battle battle = makeBattle(db, {"gel_mite", "gel_mite", "gel_mite"}, 2);
	for (int i = kFirstEnemy; i < kFirstEnemy + 3; ++i) {
		battle.combatant(i).stats.maxHp = 900;
		battle.combatant(i).hp = 900;
	}
	battle.combatant(kFirstEnemy + 1).hp = 700;
	Macro macro;
	macro.name = "focus";
	macro.steps = {step("fighter", CommandKind::Attack, "", MacroTargetKind::Enemy, "", 0)};
	battle.applyMacro(macro);
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> attack = firstBy(events, EventType::ActionStarted, kFighter);
	CHECK(attack && attack->target == kFirstEnemy + 1);
	const std::optional<Event> turned = firstBy(events, EventType::Retargeted, kFighter);
	CHECK(turned && turned->previousTarget == kFirstEnemy && turned->target == kFirstEnemy + 1);
}

// One-ally actions in a macro: a heal takes the member missing the most hit points (ties to the later member), a
// cure the first member with a status it cures, a revive the first fallen member.
TEST(macroAllyActionsPickTheirOwnTargets) {
	Database db = data();
	db.combos.clear();
	Battle battle = makeBattle(db, {"brass_warden"}, 3);
	battle.combatant(kFighter).hp -= 20;
	battle.combatant(kEngineer).hp -= 20;
	addStatus(battle, kMystic, "poison", 0);
	addStatus(battle, kScout, "poison", 0);
	battle.combatant(kScout).hp = 0;
	battle.combatant(kScout).statuses = {{kDefeatedStatus, 0}};
	battle.setCommand(kFirstEnemy, Command::defend());
	Macro macro;
	macro.name = "care";
	macro.steps = {step("medic", CommandKind::Ability, "mend", MacroTargetKind::Self), step("mystic", CommandKind::Item, "clear_gel", MacroTargetKind::Self),
		step("engineer", CommandKind::Item, "second_wind", MacroTargetKind::Member, "fighter")};
	battle.applyMacro(macro);
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> mend = firstBy(events, EventType::ActionStarted, kMedic);
	CHECK(mend && mend->target == kEngineer);
	const std::optional<Event> gel = firstBy(events, EventType::ActionStarted, kMystic);
	CHECK(gel && gel->target == kMystic);
	const std::optional<Event> wind = firstBy(events, EventType::ActionStarted, kEngineer);
	CHECK(wind && wind->target == kScout);

	// Nobody to cure: the actor. Nobody fallen: the revive falls back to the heal pick and has no target.
	Battle calm = makeBattle(db, {"brass_warden"}, 3);
	calm.combatant(kMystic).hp -= 5;
	calm.setCommand(kFirstEnemy, Command::defend());
	macro.steps = {step("medic", CommandKind::Ability, "purge", MacroTargetKind::Member, "fighter"), step("engineer", CommandKind::Item, "second_wind")};
	calm.applyMacro(macro);
	othersDefend(calm);
	const std::vector<Event> quiet = calm.resolveRound();
	const std::optional<Event> purge = firstBy(quiet, EventType::ActionStarted, kMedic);
	CHECK(purge && purge->target == kMedic);
	const std::optional<Event> none = firstBy(quiet, EventType::ActionCancelled, kEngineer);
	CHECK(none && none->cancel == CancelReason::NoTarget);
}

// R48: a step that names a piece the member wears with a use, or one cast from the bag, is kept and cast; a piece the
// party does not have keeps its step, to be cancelled when it runs, and one with no use falls back to the attack.
TEST(macroStepsCastWornAndBagPieces) {
	const Database& db = data();
	BattleSetup setup = makeSetup(db, {"gel_mite", "rust_hound"}, 3);
	setup.party[kMystic].equipment[EquipSlot::Weapon] = "storm_rod";
	setup.pieces["squall_fan"] = 1;
	Macro macro;
	macro.name = "pieces";
	macro.steps.push_back(step("mystic", CommandKind::Item, "storm_rod", MacroTargetKind::Enemy, "", 1));
	macro.steps.push_back(step("scout", CommandKind::Item, "squall_fan", MacroTargetKind::Enemy, "", 0));
	macro.steps.push_back(step("fighter", CommandKind::Item, "squall_fan"));
	for (MacroStep& s : macro.steps) {
		s.piece = true;
	}
	Battle battle = makeBattle(db, setup);
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).stats.maxHp = battle.combatant(i).hp = 5000;
	}
	const std::vector<MacroStepResult> results = battle.applyMacro(macro);
	CHECK(results[0].status == MacroStepStatus::Applied && results[0].issue == MacroIssue::None);
	CHECK(battle.command(kMystic).kind == CommandKind::Item && battle.command(kMystic).id == "storm_rod" && battle.command(kMystic).target == kFirstEnemy + 1);
	CHECK(results[1].status == MacroStepStatus::Applied && battle.command(kScout).id == "squall_fan");
	// The bag piece is not used up, so a second member may cast it in the same round.
	CHECK(results[2].status == MacroStepStatus::Applied && results[2].issue == MacroIssue::None);
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> rod = firstBy(events, EventType::ActionStarted, kMystic);
	CHECK(rod && rod->ability == "storm_rod" && rod->command == CommandKind::Item);
	const std::optional<Event> fan = firstBy(events, EventType::ActionStarted, kScout);
	CHECK(fan && fan->ability == "squall_fan");
	CHECK(battle.lastRoundAsMacro("again").steps.size() >= 2);
	for (const MacroStep& again : battle.lastRoundAsMacro("again").steps) {
		CHECK_EQ(again.piece, again.member != "medic" && again.member != "engineer");
	}

	Battle without = makeBattle(db, makeSetup(db, {"gel_mite", "rust_hound"}, 3));
	const std::vector<MacroStepResult> missing = without.applyMacro(macro);
	CHECK(missing[0].status == MacroStepStatus::Applied && missing[0].issue == MacroIssue::NoItem);
	CHECK(without.command(kMystic).kind == CommandKind::Item && without.command(kMystic).id == "storm_rod");
	Macro plain;
	plain.name = "plain";
	plain.steps.push_back(step("fighter", CommandKind::Item, "rivet_plate", MacroTargetKind::Enemy, "", 0));
	plain.steps.back().piece = true;
	const std::vector<MacroStepResult> noUse = without.applyMacro(plain);
	CHECK(noUse[0].status == MacroStepStatus::FellBack && noUse[0].issue == MacroIssue::NoItem && without.command(kFighter).kind == CommandKind::Attack);
}

// R49: with the automatic fallback a step that cannot be paid for, has no target or would be refused takes core's
// automatic choice for the member; the default keeps today's handling.
TEST(automaticFallbackGivesStuckStepsCoresChoice) {
	Macro macro;
	macro.name = "stuck";
	macro.steps.push_back(step("mystic", CommandKind::Ability, "spark", MacroTargetKind::Enemy, "", 0));
	macro.steps.push_back(step("medic", CommandKind::Ability, "rekindle"));
	macro.steps.push_back(step("scout", CommandKind::Ability, "cleave"));
	macro.steps.push_back(step("fighter", CommandKind::Item, "gear_salve"));
	Battle manual = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	manual.combatant(kMystic).tp = 0;
	std::vector<MacroStepResult> results = manual.applyMacro(macro);
	CHECK(results[0].status == MacroStepStatus::Applied && results[0].issue == MacroIssue::NotEnoughTp);
	CHECK(results[1].status == MacroStepStatus::Applied && results[1].issue == MacroIssue::NoTarget);
	CHECK(results[2].status == MacroStepStatus::FellBack);
	CHECK(results[3].status == MacroStepStatus::Applied && results[3].issue == MacroIssue::NoItem);

	Battle automatic = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	automatic.combatant(kMystic).tp = 0;
	results = automatic.applyMacro(macro, MacroFallback::Automatic);
	for (const MacroStepResult& r : results) {
		CHECK(r.status == MacroStepStatus::Automatic);
		const Command expected = autoCommand(automatic, r.actor);
		CHECK(automatic.command(r.actor).kind == expected.kind && automatic.command(r.actor).id == expected.id && automatic.command(r.actor).target == expected.target);
	}
	CHECK(results[0].issue == MacroIssue::NotEnoughTp && results[1].issue == MacroIssue::NoTarget && results[2].issue == MacroIssue::UnknownAbility);
	CHECK(results[3].issue == MacroIssue::NoItem);
	CHECK(automatic.command(kMystic).kind == CommandKind::Attack);
	othersDefend(automatic);
	const std::vector<Event> events = automatic.resolveRound();
	CHECK(ofType(events, EventType::ActionCancelled).empty());
}

// R76: with rules.json's macro_ally_pick would_change a default one-ally step passes by members it would not change and,
// with nobody left, falls back by the mode in effect; by default a heal still goes to a member at full health and a
// raise to the fallen first member in line.
TEST(macroAllyPicksCanPassByMembersTheEffectWouldNotChange) {
	Database db = data();
	AbilityDef steel;
	steel.id = "steel_up";
	steel.name = "Steel Up";
	steel.kind = AbilityKind::Technique;
	steel.cost = 1;
	steel.targeting = Targeting::OneAlly;
	Effect raise;
	raise.kind = EffectKind::ModifyStat;
	raise.stat = Stat::Defense;
	raise.raises = true;
	steel.effects = {raise};
	db.abilities[steel.id] = steel;
	db.characters["scout"].abilities.push_back(steel.id);
	Macro heal;
	heal.name = "heal";
	heal.steps.push_back(step("medic", CommandKind::Ability, "mend"));
	Macro steady;
	steady.name = "steady";
	steady.steps.push_back(step("scout", CommandKind::Ability, "steel_up"));
	auto sturdyParty = [](Battle& battle) {
		for (int i = 0; i < kFirstEnemy; ++i) {
			battle.combatant(i).stats.maxHp = battle.combatant(i).hp = 5000;
		}
	};
	auto aimed = [](const std::vector<Event>& events, EventType type) {
		for (const Event& e : events) {
			if (e.type == type) {
				return e.target;
			}
		}
		return -1;
	};

	for (const bool byChange : {false, true}) {
		db.battleRules.macroAllyWouldChange = byChange;
		Battle full = makeBattle(db, {"gel_mite", "rust_hound"}, 1);
		sturdyParty(full);
		std::vector<MacroStepResult> results = full.applyMacro(heal);
		CHECK_EQ(results[0].status == MacroStepStatus::FellBack, byChange);
		CHECK_EQ(results[0].issue == MacroIssue::NoTarget, byChange);
		CHECK_EQ(full.command(kMedic).kind == CommandKind::Attack, byChange);
		Battle automatic = makeBattle(db, {"gel_mite", "rust_hound"}, 1);
		sturdyParty(automatic);
		results = automatic.applyMacro(heal, MacroFallback::Automatic);
		CHECK_EQ(results[0].status == MacroStepStatus::Automatic, byChange);
		if (byChange) {
			const Command expected = autoCommand(automatic, kMedic);
			CHECK(automatic.command(kMedic).kind == expected.kind && automatic.command(kMedic).id == expected.id && automatic.command(kMedic).target == expected.target);
		}

		Battle hurt = makeBattle(db, {"gel_mite", "rust_hound"}, 1);
		sturdyParty(hurt);
		hurt.combatant(kMystic).hp -= 10;
		results = hurt.applyMacro(heal);
		CHECK(results[0].status == MacroStepStatus::Applied && hurt.command(kMedic).kind == CommandKind::Ability);
		othersDefend(hurt);
		CHECK_EQ(aimed(hurt.resolveRound(), EventType::Healed), kMystic);

		Battle fallen = makeBattle(db, {"gel_mite", "rust_hound"}, 1);
		sturdyParty(fallen);
		fallen.combatant(kFighter).hp = 0;
		results = fallen.applyMacro(steady);
		CHECK(results[0].status == MacroStepStatus::Applied);
		othersDefend(fallen);
		const std::vector<Event> events = fallen.resolveRound();
		const std::optional<Event> started = firstBy(events, EventType::ActionStarted, kScout);
		CHECK(started && started->ability == "steel_up");
		CHECK_EQ(aimed(events, EventType::StatModified), byChange ? kMystic : -1);
	}

	std::map<std::string, std::string> texts = dataTexts();
	texts["rules.json"].replace(texts["rules.json"].find('{'), 1, "{\"macro_ally_pick\": \"would_change\",");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->battleRules.macroAllyWouldChange);
	texts = dataTexts();
	texts["rules.json"].replace(texts["rules.json"].find('{'), 1, "{\"macro_ally_pick\": \"nearest\",");
	const Result<Database> bad = loadDatabaseFromTexts(texts);
	CHECK(!bad.ok() && bad.error.find("macro_ally_pick is 'nearest'; it takes original, would_change") != std::string::npos);
}
