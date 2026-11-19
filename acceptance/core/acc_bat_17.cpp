#include "support.h"

#include "rules/names.h"

using namespace rules;

namespace {

constexpr int kFirst = 5;

Macro attackMacro(const Database& db) {
	Macro macro;
	macro.name = "acc_auto";
	for (const std::string& member : db.lineup) {
		macro.steps.push_back({member, CommandKind::Attack, "", {MacroTargetKind::Enemy, "", 0}});
	}
	return macro;
}

Battle sturdyBattle(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed) {
	Battle battle = support::create(db, support::setup(db, enemies, seed, 6));
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		support::sturdy(battle, i);
	}
	return battle;
}

std::string stopName(const Battle& battle) {
	return toString(battle.autoStop());
}

}

ACC_CHECK(acc_bat_17, "ACC-BAT-17", "BAT-103, BAT-104") {
	const Database& db = support::data(t);
	const Macro autoMacro = attackMacro(db);

	// Each round the auto macro supplies the commands with no input.
	{
		Battle battle = sturdyBattle(db, {"rust_hound", "gel_mite"}, 9);
		t.expect(battle.startAuto(autoMacro) && battle.autoActive(), "auto-battle did not turn on");
		for (int round = 1; round <= 5; ++round) {
			const bool filled = battle.fillFromAuto();
			t.expect(filled, "round " + std::to_string(round) + ": auto-battle stopped with " + stopName(battle) + " although no stop rule applies");
			bool all = true;
			for (int i = 0; i < battle.partyCount(); ++i) {
				all = all && battle.command(i).kind == CommandKind::Attack && battle.command(i).target == kFirst;
			}
			t.expect(all, "round " + std::to_string(round) + ": the auto macro did not supply every party member's command");
			const std::vector<Event> locked = support::ofType(battle.resolveRound(), EventType::CommandsLocked);
			t.expect(locked.size() == 1 && locked[0].macro == autoMacro.name, "round " + std::to_string(round) + ": CommandsLocked does not name the auto macro");
		}
	}

	// Cancel: stops at the next command phase.
	{
		Battle battle = sturdyBattle(db, {"rust_hound"}, 10);
		battle.startAuto(autoMacro);
		battle.fillFromAuto();
		battle.cancelAuto();
		battle.resolveRound();
		t.expect(!battle.fillFromAuto() && !battle.autoActive() && battle.autoStop() == AutoStop::Cancelled, "the player's cancel did not stop auto-battle at the next command phase (stop: " + stopName(battle) + ")");
		t.expect(battle.command(0).kind == CommandKind::None, "auto-battle filled the round after it was cancelled");
	}

	// A party member falls: stops at the next command phase.
	{
		Battle battle = sturdyBattle(db, {"brass_warden"}, 11);
		battle.combatant(4).hp = 1;
		battle.combatant(4).stats.evasion = 0;
		battle.combatant(kFirst).stats.dexterity = 400;
		battle.startAuto(autoMacro);
		battle.fillFromAuto();
		battle.setCommand(kFirst, Command::attack(4));
		battle.resolveRound();
		t.expect(!battle.combatant(4).alive(), "setup: the party member did not fall");
		t.expect(!battle.fillFromAuto() && battle.autoStop() == AutoStop::MemberFell, "a fallen party member did not stop auto-battle at the next command phase (stop: " + stopName(battle) + ")");
	}

	// An auto macro entry that cannot be paid: stops at the next command phase.
	{
		Battle battle = sturdyBattle(db, {"brass_warden"}, 12);
		Macro paid = autoMacro;
		paid.steps[1] = {"mystic", CommandKind::Ability, "spark", {MacroTargetKind::Enemy, "", 0}};
		battle.startAuto(paid);
		t.expect(battle.fillFromAuto(), "setup: the paid macro did not fill the first round");
		battle.resolveRound();
		battle.combatant(1).tp = 0;
		t.expect(!battle.fillFromAuto() && battle.autoStop() == AutoStop::CannotPay, "an auto macro entry that cannot be paid did not stop auto-battle (stop: " + stopName(battle) + ")");
	}

	// Battle end: stops at once.
	{
		Battle battle = sturdyBattle(db, {"gel_mite"}, 13);
		battle.combatant(kFirst).hp = 1;
		for (int i = 0; i < battle.partyCount(); ++i) {
			battle.combatant(i).stats.dexterity = 400;
		}
		battle.startAuto(autoMacro);
		battle.fillFromAuto();
		battle.resolveRound();
		t.expect(battle.outcome() == Outcome::Victory, "setup: the battle did not end");
		t.expect(!battle.autoActive() && battle.autoStop() == AutoStop::BattleEnded, "auto-battle stayed on after the battle ended (stop: " + stopName(battle) + ")");
		t.expect(!battle.startAuto(autoMacro), "auto-battle turned on in a battle that has ended");
	}

	// Allowed in every battle: each encounter, including one that forbids escape, runs to its end on auto.
	int encounters = 0;
	for (const auto& [id, encounter] : db.encounters) {
		BattleSetup s = support::setup(db, {}, 14, 12);
		useEncounter(s, db, id);
		Battle battle = support::create(db, s);
		t.expect(battle.startAuto(*db.macro("tempest")), id + ": auto-battle was refused");
		int rounds = 0;
		while (battle.outcome() == Outcome::Ongoing && rounds < 60) {
			if (!battle.fillFromAuto()) {
				battle.startAuto(*db.macro("tempest"));
				battle.fillFromAuto();
			}
			battle.resolveRound();
			++rounds;
		}
		t.expect(battle.outcome() != Outcome::Ongoing, id + ": an auto-battle did not end within 60 rounds");
		++encounters;
	}

	t.note("the auto macro fills every round with no input until a stop rule; cancel, a fallen member and an unpayable entry stop it at the next command phase with the matching reason; battle end stops it at once; " +
		std::to_string(encounters) + " encounters, including one that forbids escape, accept and finish auto-battle");
}
