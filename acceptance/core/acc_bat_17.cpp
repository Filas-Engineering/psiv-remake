#include "support.h"

using namespace rules;

namespace {

constexpr int kFirst = 5;

bool hasIssue(const std::vector<MacroStepResult>& steps, MacroIssue issue) {
	for (const MacroStepResult& step : steps) {
		if (step.issue == issue) {
			return true;
		}
	}
	return false;
}

}

ACC_CHECK(acc_bat_17, "ACC-BAT-17", "BAT-103, BAT-104") {
	const Database& db = support::data(t);
	const Macro& tempest = *db.macro("tempest");

	// The auto macro supplies every round's commands with no other input, until the battle ends.
	{
		Battle battle = support::create(db, support::setup(db, {"gel_mite", "rust_hound"}, 9, 10));
		int rounds = 0;
		while (battle.outcome() == Outcome::Ongoing && rounds < 40) {
			const std::vector<MacroStepResult> steps = battle.applyMacro(tempest);
			for (const MacroStepResult& step : steps) {
				t.expect(step.status == MacroStepStatus::Skipped || battle.command(step.actor).kind != CommandKind::None, "the auto macro left a member without a command");
			}
			battle.resolveRound();
			++rounds;
		}
		t.expect(battle.outcome() != Outcome::Ongoing, "an auto-macro battle did not end");
		t.expect(battle.resolveRound().empty(), "the battle kept resolving rounds after it ended");
	}

	// The signals a host needs to stop auto-battle at the next command phase.
	{
		Battle battle = support::create(db, support::setup(db, {"brass_warden"}, 10));
		support::sturdy(battle, kFirst);
		battle.combatant(4).hp = 0;
		battle.combatant(4).statuses = {{kDefeatedStatus, 0}};
		t.expect(hasIssue(battle.applyMacro(tempest), MacroIssue::ActorDefeated), "a fallen member's auto macro entry is not reported");
	}
	{
		Battle battle = support::create(db, support::setup(db, {"brass_warden"}, 11));
		battle.combatant(1).tp = 0;
		t.expect(hasIssue(battle.applyMacro(tempest), MacroIssue::NotEnoughTp), "an unaffordable auto macro entry is not reported");
	}

	t.gap("core has no auto-battle mode: no auto macro setting, no Auto flag and no stop rule for cancel, a fallen member or an unaffordable entry; the host must loop applyMacro and read MacroStepResult issues itself (BAT-103, BAT-104)");
	t.note("looping the macro supplies every round's commands until the battle ends; fallen and unaffordable entries are reported by applyMacro");
}
