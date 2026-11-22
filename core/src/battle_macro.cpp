#include "rules/auto_policy.h"
#include "rules/battle.h"

#include <algorithm>
#include <vector>

namespace rules {

namespace {

MacroIssue issueFor(CancelReason reason) {
	switch (reason) {
	case CancelReason::Silenced: return MacroIssue::Silenced;
	case CancelReason::NotEnoughTp: return MacroIssue::NotEnoughTp;
	case CancelReason::NoUsesLeft: return MacroIssue::NoUsesLeft;
	case CancelReason::NoItem: return MacroIssue::NoItem;
	case CancelReason::NoTarget: return MacroIssue::NoTarget;
	case CancelReason::ActorUnable: return MacroIssue::CannotAct;
	case CancelReason::None: return MacroIssue::None;
	}
	return MacroIssue::None;
}

bool singleTarget(Targeting targeting) {
	return targeting == Targeting::OneEnemy || targeting == Targeting::EnemyGroup || targeting == Targeting::OneAlly || targeting == Targeting::OneFallenAlly;
}

}

bool isCostIssue(MacroIssue issue) {
	return issue == MacroIssue::Silenced || issue == MacroIssue::NotEnoughTp || issue == MacroIssue::NoUsesLeft || issue == MacroIssue::NoItem;
}

bool macroTargetFits(MacroTargetKind target, CommandKind command, Targeting targeting) {
	if (target == MacroTargetKind::Default) {
		return true;
	}
	switch (command) {
	case CommandKind::Attack:
		return target == MacroTargetKind::Enemy || target == MacroTargetKind::All;
	case CommandKind::Ability:
	case CommandKind::Item:
		switch (targeting) {
		case Targeting::OneEnemy:
		case Targeting::EnemyGroup:
			return target == MacroTargetKind::Enemy;
		case Targeting::AllEnemies:
		case Targeting::AllAllies:
			return target == MacroTargetKind::All;
		case Targeting::OneAlly:
		case Targeting::OneFallenAlly:
			return target == MacroTargetKind::Self || target == MacroTargetKind::Member;
		case Targeting::Self:
			return target == MacroTargetKind::Self;
		case Targeting::RandomEnemy:
			return false;
		}
		return false;
	case CommandKind::None:
	case CommandKind::Defend:
	case CommandKind::Escape:
		return false;
	}
	return false;
}

// A step whose cost cannot be paid, or that has no target yet, keeps its command: the battle validates it at
// execution (BAT-015, BAT-053). Only a command the battle would refuse outright (an ability the member does not
// know, an escape the battle forbids) falls back to a basic attack on the intended enemy, or the first living one.
// With MacroFallback::Automatic every one of those takes autoCommand's choice instead, with Attack a basic attack, and with
// LoseTurn the member does nothing. With rules.json's macro_ally_pick would_change, a one-ally step with the default target
// that would change nobody falls back in the same way.
std::vector<MacroStepResult> Battle::planMacro(const Macro& macro, MacroFallback fallback) const {
	std::vector<MacroStepResult> results;
	Inventory reserved;
	const bool caughtOffGuard = !sideActsNextRound(Side::Party);

	auto partyIndexOf = [this](const std::string& characterId) {
		for (int i = 0; i < partyCount_; ++i) {
			if (combatants_[i].defId == characterId && !combatants_[i].aboard) {
				return i;
			}
		}
		return -1;
	};

	for (const MacroStep& step : macro.steps) {
		MacroStepResult result;
		result.member = step.member;
		result.actor = partyIndexOf(step.member);
		MacroIssue skip = MacroIssue::None;
		if (result.actor < 0) {
			skip = MacroIssue::NotInParty;
		} else if (caughtOffGuard) {
			skip = MacroIssue::CaughtOffGuard;
		} else if (!combatants_[result.actor].alive()) {
			skip = MacroIssue::ActorDefeated;
		} else if (statusWith(result.actor, StatusAction::Forced)) {
			skip = MacroIssue::ForcedByStatus;
		} else if (statusWith(result.actor, StatusAction::CannotAct)) {
			skip = MacroIssue::CannotAct;
		}
		if (skip != MacroIssue::None) {
			result.status = MacroStepStatus::Skipped;
			result.issue = skip;
			results.push_back(result);
			continue;
		}
		const int actor = result.actor;
		const int enemyIndex = step.target.kind == MacroTargetKind::Enemy ? enemyAt(step.target.enemySlot) : -1;
		const bool enemyThere = enemyIndex >= partyCount_ && enemyIndex < static_cast<int>(combatants_.size());
		int requested = -1;
		bool intendedGone = false;
		switch (step.target.kind) {
		case MacroTargetKind::Default:
		case MacroTargetKind::All:
			break;
		case MacroTargetKind::Self:
			requested = actor;
			break;
		case MacroTargetKind::Member:
			requested = partyIndexOf(step.target.member);
			intendedGone = requested < 0;
			break;
		case MacroTargetKind::Enemy:
			requested = enemyThere ? enemyIndex : -1;
			intendedGone = !enemyThere;
			break;
		}

		Targeting targeting = Targeting::Self;
		const std::vector<Effect>* effects = nullptr;
		bool aimed = true;
		bool revives = false;
		MacroIssue issue = MacroIssue::None;
		bool refused = false;
		Command command;
		command.kind = step.kind;
		command.id = step.id;
		command.target = requested;
		switch (step.kind) {
		case CommandKind::Attack:
			if (const AbilityDef* ability = db_->ability(combatants_[actor].basicAttack)) {
				targeting = ability->targeting;
			}
			break;
		case CommandKind::Ability:
			if (const AbilityDef* ability = db_->ability(step.id); ability && combatants_[actor].knows(step.id) && ability->kind != AbilityKind::Combo) {
				targeting = ability->targeting;
				effects = &ability->effects;
				revives = ability->revives();
				issue = issueFor(checkUsable(actor, *ability));
			} else {
				issue = MacroIssue::UnknownAbility;
				refused = true;
			}
			break;
		case CommandKind::Item:
			if (step.piece) {
				const EquipmentDef* def = db_->equipmentPiece(step.id);
				const AbilityDef* use = def ? db_->ability(def->use) : nullptr;
				if (!use) {
					issue = MacroIssue::NoItem;
					refused = true;
				} else if (wornPiece(actor, step.id) || bagPiece(actor, step.id)) {
					targeting = use->targeting;
					effects = &use->effects;
					revives = use->revives();
				} else {
					targeting = use->targeting;
					effects = &use->effects;
					issue = MacroIssue::NoItem;
				}
			} else if (const ItemDef* item = db_->item(step.id)) {
				targeting = item->targeting;
				effects = &item->effects;
				revives = effectsRevive(item->effects);
				const auto stock = inventory_.find(step.id);
				if (stock == inventory_.end() || stock->second - reserved[step.id] <= 0) {
					issue = MacroIssue::NoItem;
				}
			} else {
				issue = MacroIssue::NoItem;
				refused = true;
			}
			break;
		case CommandKind::Escape:
			aimed = false;
			if (!escapeAllowed()) {
				issue = MacroIssue::EscapeNotAllowed;
				refused = true;
			}
			break;
		case CommandKind::None:
		case CommandKind::Defend:
			aimed = false;
			break;
		}

		const bool automatic = fallback == MacroFallback::Automatic;
		const bool failing = fallback == MacroFallback::Attack || fallback == MacroFallback::LoseTurn;
		const auto fail = [&](MacroIssue why) {
			result.issue = why;
			if (fallback == MacroFallback::LoseTurn) {
				result.status = MacroStepStatus::LostTurn;
				command = Command();
				return;
			}
			result.status = MacroStepStatus::FellBack;
			const std::vector<int> foes = livingFoes(actor);
			const bool intended = enemyThere && combatants_[enemyIndex].alive();
			command = Command::attack(intended ? enemyIndex : (foes.empty() ? -1 : foes.front()));
		};
		bool nobodyToChange = false;
		if (db_->battleRules.macroAllyWouldChange && step.target.kind == MacroTargetKind::Default && effects &&
			(targeting == Targeting::OneAlly || targeting == Targeting::OneFallenAlly)) {
			const std::vector<int> party = allies(actor);
			nobodyToChange = std::none_of(party.begin(), party.end(), [&](int ally) { return wouldChange(ally, *effects); });
		}
		if (automatic && (refused || isCostIssue(issue) || nobodyToChange)) {
			result.status = MacroStepStatus::Automatic;
			result.issue = refused || isCostIssue(issue) ? issue : MacroIssue::NoTarget;
			command = autoCommand(*this, actor, autoExpected(&macro));
		} else if (failing && (refused || isCostIssue(issue) || nobodyToChange)) {
			fail(refused || isCostIssue(issue) ? issue : MacroIssue::NoTarget);
		} else if (refused || nobodyToChange) {
			fail(refused ? issue : MacroIssue::NoTarget);
		} else if (aimed) {
			const std::vector<int> targets = resolveTargets(actor, targeting, requested, revives);
			if (targets.empty() && automatic) {
				result.status = MacroStepStatus::Automatic;
				issue = MacroIssue::NoTarget;
				command = autoCommand(*this, actor, autoExpected(&macro));
			} else if (targets.empty() && failing) {
				fail(MacroIssue::NoTarget);
				issue = MacroIssue::None;
			} else if (targets.empty()) {
				issue = issue == MacroIssue::None ? MacroIssue::NoTarget : issue;
			} else if (singleTarget(targeting)) {
				command.target = targeting == Targeting::EnemyGroup ? primaryFoe(actor, requested) : targets.front();
				if (intendedGone || (requested >= 0 && command.target != requested)) {
					result.status = MacroStepStatus::Retargeted;
					result.issue = MacroIssue::TargetGone;
				}
			} else {
				command.target = -1;
			}
			if (issue != MacroIssue::None) {
				result.issue = issue;
			}
		} else {
			command.target = -1;
		}
		if (command.kind == CommandKind::Item && !step.piece) {
			++reserved[command.id];
		}
		result.command = command;
		results.push_back(result);
	}
	return results;
}

bool Battle::autoExpected(const Macro* macro) const {
	const MacroPolicy policy = autoPolicy_ != MacroPolicy::Default ? autoPolicy_ : macro ? macro->policy : MacroPolicy::Default;
	return policy == MacroPolicy::Default ? db_->battleRules.autoExpected : policy == MacroPolicy::Expected;
}

std::vector<MacroStepResult> Battle::applyMacro(const Macro& macro, MacroFallback fallback) {
	std::vector<MacroStepResult> results = planMacro(macro, fallback);
	forcedOrder_.clear();
	macroName_ = macro.name;
	std::vector<bool> named(static_cast<std::size_t>(partyCount_), false);
	for (const MacroStepResult& result : results) {
		if (result.actor >= 0) {
			named[static_cast<std::size_t>(result.actor)] = true;
		}
		if (result.status == MacroStepStatus::Skipped) {
			continue;
		}
		commands_[result.actor] = result.command;
		passing_[result.actor] = result.status == MacroStepStatus::LostTurn;
		if (!passing_[result.actor]) {
			forcedOrder_.push_back(result.actor);
		}
	}
	const MacroUnnamed unnamed = macro.unnamed != MacroUnnamed::Default ? macro.unnamed : db_->battleRules.macroUnnamed;
	if (unnamed == MacroUnnamed::Attack || !sideActsNextRound(Side::Party)) {
		return results;
	}
	for (int i = 0; i < partyCount_; ++i) {
		const Combatant& c = combatants_[i];
		if (named[i] || c.aboard || !c.alive() || commands_[i].kind != CommandKind::None || statusWith(i, StatusAction::Forced)) {
			continue;
		}
		if (unnamed == MacroUnnamed::None) {
			passing_[i] = true;
		} else {
			commands_[i] = autoCommand(*this, i, autoExpected(&macro));
		}
	}
	return results;
}

bool stepTroubled(const MacroStepResult& result) {
	switch (result.status) {
	case MacroStepStatus::FellBack:
	case MacroStepStatus::Automatic:
	case MacroStepStatus::LostTurn:
		return true;
	case MacroStepStatus::Skipped:
		return result.issue == MacroIssue::ActorDefeated || result.issue == MacroIssue::CannotAct || result.issue == MacroIssue::ForcedByStatus;
	case MacroStepStatus::Applied:
	case MacroStepStatus::Retargeted:
		return isCostIssue(result.issue) || result.issue == MacroIssue::NoTarget;
	}
	return false;
}

MacroTroubleReport Battle::macroTrouble(const Macro& macro, const std::vector<MacroStepResult>& results) const {
	MacroTroubleReport report;
	for (int i = 0; i < partyCount_; ++i) {
		if (!combatants_[i].aboard && !combatants_[i].alive()) {
			report.fallen.push_back(i);
		}
	}
	for (std::size_t k = 0; k < results.size() && k < macro.steps.size(); ++k) {
		if (stepTroubled(results[k])) {
			report.troubledSteps.push_back(static_cast<int>(k));
			if (results[k].actor >= 0) {
				report.troubledMembers.push_back(results[k].actor);
			}
		}
	}
	report.holds = !report.fallen.empty() || !report.troubledSteps.empty();
	return report;
}

MacroTroubleReport Battle::macroTrouble(const Macro& macro) const {
	return macroTrouble(macro, planMacro(macro, MacroFallback::Automatic));
}

MacroTroubleReport Battle::applyMacroRound(const Macro& macro, MacroTrouble mode) {
	const std::vector<MacroStepResult> results = applyMacro(macro, MacroFallback::Automatic);
	MacroTroubleReport report = macroTrouble(macro, results);
	report.mode = mode;
	if (mode != MacroTrouble::Recover || !report.holds) {
		return report;
	}
	const MacroRecoverWho who = macro.recoverWho != MacroRecoverWho::Default ? macro.recoverWho : db_->battleRules.macroRecoverWho;
	if (who == MacroRecoverWho::Troubled) {
		for (const MacroStepResult& result : results) {
			if (result.status == MacroStepStatus::Automatic) {
				report.recovered.push_back(result.actor);
			}
		}
		return report;
	}
	for (int i = 0; i < partyCount_; ++i) {
		const bool covered = std::any_of(results.begin(), results.end(), [i](const MacroStepResult& r) { return r.actor == i; });
		if ((who == MacroRecoverWho::Uncovered && covered) || !combatants_[i].alive()) {
			continue;
		}
		if (setCommand(i, autoCommand(*this, i, autoExpected(&macro))).empty()) {
			report.recovered.push_back(i);
		}
	}
	if (who == MacroRecoverWho::All) {
		forcedOrder_.clear();
		macroName_.clear();
	}
	return report;
}

std::vector<MacroStepCheck> Battle::checkMacro(const Macro& macro) const {
	const std::vector<MacroStepResult> results = planMacro(macro, MacroFallback::BasicAttack);
	std::vector<MacroStepCheck> checks;
	for (std::size_t k = 0; k < results.size(); ++k) {
		const MacroStepResult& result = results[k];
		const MacroStep& step = macro.steps[k];
		MacroStepCheck check;
		check.member = result.member;
		check.actor = result.actor;
		check.issue = result.issue;
		if (result.issue == MacroIssue::CannotAct || result.issue == MacroIssue::ForcedByStatus) {
			const StatusDef* status = statusWith(result.actor, result.issue == MacroIssue::CannotAct ? StatusAction::CannotAct : StatusAction::Forced);
			check.status = status ? status->id : "";
		}
		const bool asWritten = result.status == MacroStepStatus::Applied || result.status == MacroStepStatus::Retargeted;
		if (result.actor >= 0 && result.status != MacroStepStatus::Skipped && !(step.piece && result.issue == MacroIssue::NoItem)) {
			check.refusal = checkCommand(result.actor, {step.kind, step.id, asWritten ? result.command.target : -1});
		}
		check.ok = (check.issue == MacroIssue::None || check.issue == MacroIssue::TargetGone) && check.refusal.empty();
		checks.push_back(check);
	}
	return checks;
}

}
