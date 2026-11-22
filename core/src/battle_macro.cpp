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
// With MacroFallback::Automatic every one of those takes autoCommand's choice instead. With rules.json's macro_ally_pick
// would_change, a one-ally step with the default target that would change nobody falls back in the same way.
std::vector<MacroStepResult> Battle::applyMacro(const Macro& macro, MacroFallback fallback) {
	std::vector<MacroStepResult> results;
	forcedOrder_.clear();
	macroName_ = macro.name;
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
		bool nobodyToChange = false;
		if (db_->battleRules.macroAllyWouldChange && step.target.kind == MacroTargetKind::Default && effects &&
			(targeting == Targeting::OneAlly || targeting == Targeting::OneFallenAlly)) {
			const std::vector<int> party = allies(actor);
			nobodyToChange = std::none_of(party.begin(), party.end(), [&](int ally) { return wouldChange(ally, *effects); });
		}
		if (automatic && (refused || isCostIssue(issue) || nobodyToChange)) {
			result.status = MacroStepStatus::Automatic;
			result.issue = refused || isCostIssue(issue) ? issue : MacroIssue::NoTarget;
			command = autoCommand(*this, actor);
		} else if (refused || nobodyToChange) {
			result.status = MacroStepStatus::FellBack;
			result.issue = refused ? issue : MacroIssue::NoTarget;
			const std::vector<int> foes = livingFoes(actor);
			const bool intended = enemyThere && combatants_[enemyIndex].alive();
			command = Command::attack(intended ? enemyIndex : (foes.empty() ? -1 : foes.front()));
		} else if (aimed) {
			const std::vector<int> targets = resolveTargets(actor, targeting, requested, revives);
			if (targets.empty() && automatic) {
				result.status = MacroStepStatus::Automatic;
				issue = MacroIssue::NoTarget;
				command = autoCommand(*this, actor);
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
		commands_[actor] = command;
		forcedOrder_.push_back(actor);
		result.command = command;
		results.push_back(result);
	}
	return results;
}

}
