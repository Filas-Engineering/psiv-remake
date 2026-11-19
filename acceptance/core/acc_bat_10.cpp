#include "support.h"

#include "rules/formulas.h"
#include "rules/names.h"

#include <algorithm>

using namespace rules;

namespace {

constexpr int kFirst = 5;

int partyIndex(const Battle& battle, const std::string& character) {
	for (int i = 0; i < battle.partyCount(); ++i) {
		if (battle.combatant(i).defId == character) {
			return i;
		}
	}
	return -1;
}

std::vector<int> subsequence(const std::vector<int>& order, bool party, const Battle& battle) {
	std::vector<int> out;
	for (int actor : order) {
		if (battle.isParty(actor) == party) {
			out.push_back(actor);
		}
	}
	return out;
}

}

ACC_CHECK(acc_bat_10, "ACC-BAT-10", "BAT-051, BAT-052, BAT-053, BAT-054") {
	const Database& db = support::data(t);
	const std::vector<std::string> enemies = {"rust_hound", "gel_mite", "bog_wisp"};
	int rounds = 0;
	for (const Macro& macro : db.macros) {
		for (std::uint64_t seed = 1; seed <= 300; ++seed) {
			// With the macro.
			Battle with = support::create(db, support::setup(db, enemies, seed, 6));
			for (int i = 0; i < static_cast<int>(with.combatants().size()); ++i) {
				support::sturdy(with, i);
			}
			const std::vector<MacroStepResult> steps = with.applyMacro(macro);
			std::vector<int> macroOrder;
			for (const MacroStepResult& step : steps) {
				t.expect(step.status == MacroStepStatus::Applied, macro.name + ": step for " + step.member + " did not apply as written");
				macroOrder.push_back(step.actor);
				t.expect(with.command(step.actor).kind != CommandKind::None, macro.name + ": " + step.member + " has no command after the macro");
			}
			for (int i = 0; i < with.partyCount(); ++i) {
				t.expect(std::find(macroOrder.begin(), macroOrder.end(), i) != macroOrder.end(), macro.name + ": party member " + std::to_string(i) + " got no command from a full macro");
			}
			for (int e = kFirst; e < static_cast<int>(with.combatants().size()); ++e) {
				with.setCommand(e, Command::defend());
			}

			// Without it: the same commands set one by one, so every draw is the same.
			Battle without = support::create(db, support::setup(db, enemies, seed, 6));
			for (int i = 0; i < static_cast<int>(without.combatants().size()); ++i) {
				support::sturdy(without, i);
				without.setCommand(i, with.command(i));
			}

			// Each initiative, replayed from the seed before anything changes agility.
			int blockTotal = 0;
			std::map<int, int> initiative;
			Rng rng(seed);
			for (int i = 0; i < static_cast<int>(without.combatants().size()); ++i) {
				const int agility = without.combatant(i).effective(Stat::Agility);
				initiative[i] = formulas::initiative(agility, rng.range(0, formulas::initiativeSpread(agility)));
			}
			for (int actor : macroOrder) {
				blockTotal += initiative[actor];
			}
			const int blockInitiative = blockTotal / static_cast<int>(macroOrder.size());

			const std::vector<Event> events = with.resolveRound();
			without.resolveRound();
			const std::vector<int>& order = with.lastTurnOrder();

			// The party block is contiguous, in macro order.
			const auto start = std::find(order.begin(), order.end(), macroOrder.front());
			const bool contiguous = start != order.end() && static_cast<std::size_t>(order.end() - start) >= macroOrder.size() && std::equal(macroOrder.begin(), macroOrder.end(), start);
			t.expect(contiguous, macro.name + " seed " + std::to_string(seed) + ": party actions are not one block in macro order");
			const std::vector<int> acted = support::actingOrder(events);
			t.expect(subsequence(acted, true, with) == macroOrder, macro.name + ": party actions did not execute in macro order");

			// Enemies keep their relative order and sit before or after the block by its average initiative.
			t.expect(subsequence(order, false, with) == subsequence(without.lastTurnOrder(), false, without), macro.name + " seed " + std::to_string(seed) + ": enemy order differs from the run without the macro");
			const std::size_t blockAt = static_cast<std::size_t>(start - order.begin());
			for (std::size_t i = 0; contiguous && i < order.size(); ++i) {
				if (!with.isParty(order[i])) {
					const bool before = i < blockAt;
					t.expect(before == (initiative[order[i]] > blockInitiative), macro.name + " seed " + std::to_string(seed) + ": enemy " + std::to_string(order[i]) +
						" is on the wrong side of the block for the block's average initiative");
				}
			}
			++rounds;
		}
	}

	// Absent, fallen, disabled and unaffordable entries.
	{
		Macro macro;
		macro.name = "acc_gaps";
		macro.steps = {
			{"mystic", CommandKind::Ability, "spark", {MacroTargetKind::Enemy, "", 0}},
			{"acc_stranger", CommandKind::Attack, "", {}},
			{"fighter", CommandKind::Attack, "", {MacroTargetKind::Enemy, "", 0}},
			{"medic", CommandKind::Ability, "wide_mend", {MacroTargetKind::All, "", -1}},
			{"engineer", CommandKind::Attack, "", {MacroTargetKind::Enemy, "", 0}},
		};
		Battle battle = support::create(db, support::setup(db, {"rust_hound"}, 4));
		support::sturdy(battle, kFirst);
		battle.combatant(partyIndex(battle, "fighter")).hp = 0;
		battle.combatant(partyIndex(battle, "fighter")).statuses = {{kDefeatedStatus, 0}};
		battle.combatant(partyIndex(battle, "medic")).tp = 1;
		battle.combatant(partyIndex(battle, "engineer")).statuses.push_back({"sleep", 0});
		const std::vector<MacroStepResult> steps = battle.applyMacro(macro);
		t.expect(steps[1].status == MacroStepStatus::Skipped && steps[1].issue == MacroIssue::NotInParty, "an entry for a member not in the party was not skipped");
		t.expect(steps[2].status == MacroStepStatus::Skipped && steps[2].issue == MacroIssue::ActorDefeated, "an entry for a fallen member was not skipped");
		t.expect(steps[4].status == MacroStepStatus::Skipped && steps[4].issue == MacroIssue::CannotAct, std::string("an entry for a member who cannot act was not skipped: ") + toString(steps[4].status) + " " + toString(steps[4].issue));
		t.expect(steps[3].status == MacroStepStatus::Applied && steps[3].issue == MacroIssue::NotEnoughTp && isCostIssue(steps[3].issue),
			std::string("an unaffordable entry was not kept as written and marked with a cost issue for the menu (BAT-053): ") + toString(steps[3].status) + " " + toString(steps[3].issue));
		const int medic = partyIndex(battle, "medic");
		const bool kept = battle.command(medic).kind == CommandKind::Ability && battle.command(medic).id == "wide_mend";
		support::othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		bool costEvent = false;
		for (const Event& e : events) {
			costEvent = costEvent || (e.type == EventType::ActionCancelled && e.actor == medic && e.cancel == CancelReason::NotEnoughTp);
		}
		t.expect(kept && costEvent, "an unaffordable entry was not cancelled at execution with a cost reason per BAT-015 (BAT-053)");
	}

	// Targets are rules: an enemy-position rule follows the formation, not a stored combatant.
	{
		Macro macro;
		macro.name = "acc_rules";
		macro.steps = {{"scout", CommandKind::Ability, "gust", {MacroTargetKind::Enemy, "", 1}}, {"medic", CommandKind::Ability, "mend", {MacroTargetKind::Member, "mystic", -1}}};
		for (const std::vector<std::string>& formation : std::vector<std::vector<std::string>>{{"gel_mite", "rust_hound"}, {"bog_wisp", "gel_mite", "gel_mite"}}) {
			Battle battle = support::create(db, support::setup(db, formation, 5));
			const std::vector<MacroStepResult> steps = battle.applyMacro(macro);
			t.expect(steps[0].command.target == kFirst + 1, "an enemy-position rule did not resolve to formation position 1");
			t.expect(steps[1].command.target == partyIndex(battle, "mystic"), "a member rule did not resolve to that character");
		}
	}

	t.note(std::to_string(rounds) + " rounds with the data macros: every member's command comes from the macro, the block runs contiguously in macro order, enemy order matches the run without it; absent, fallen and disabled entries are skipped; an unaffordable entry is kept, marked with a cost issue and cancelled at execution with not_enough_tp; targets are rules");
}
