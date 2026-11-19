#include "rules/battle.h"

namespace rules {

namespace {

MacroIssue issueFor(FailReason reason) {
	switch (reason) {
	case FailReason::Silenced: return MacroIssue::Silenced;
	case FailReason::NotEnoughTp: return MacroIssue::NotEnoughTp;
	case FailReason::NoUsesLeft: return MacroIssue::NoUsesLeft;
	case FailReason::NoItem: return MacroIssue::NoItem;
	case FailReason::NoTarget: return MacroIssue::NoTarget;
	case FailReason::None: return MacroIssue::None;
	}
	return MacroIssue::None;
}

}

// A step that cannot run as written falls back to a basic attack on its intended enemy (or the first
// living one), so the macro still spends every member's turn.
std::vector<MacroStepResult> Battle::applyMacro(const Macro& macro) {
	std::vector<MacroStepResult> results;
	forcedOrder_.clear();
	Inventory reserved;
	const bool caughtOffGuard = !sideActsNextRound(Side::Party);

	auto partyIndexOf = [this](const std::string& characterId) {
		for (int i = 0; i < partyCount_; ++i) {
			if (combatants_[i].defId == characterId) {
				return i;
			}
		}
		return -1;
	};

	for (const MacroStep& step : macro.steps) {
		MacroStepResult result;
		result.member = step.member;
		result.actor = partyIndexOf(step.member);
		if (result.actor < 0 || !combatants_[result.actor].alive() || caughtOffGuard) {
			result.status = MacroStepStatus::Skipped;
			result.issue = result.actor < 0 ? MacroIssue::NotInParty : caughtOffGuard ? MacroIssue::CaughtOffGuard : MacroIssue::ActorDefeated;
			results.push_back(result);
			continue;
		}
		const int actor = result.actor;
		const int enemyIndex = step.target.kind == MacroTargetKind::Enemy ? partyCount_ + step.target.enemySlot : -1;
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
		bool aimed = true;
		bool revives = false;
		MacroIssue issue = MacroIssue::None;
		Command command;
		command.kind = step.kind;
		command.id = step.id;
		switch (step.kind) {
		case CommandKind::Attack:
			if (const AbilityDef* ability = db_->ability(combatants_[actor].basicAttack)) {
				targeting = ability->targeting;
			}
			break;
		case CommandKind::Ability:
			if (const AbilityDef* ability = db_->ability(step.id); ability && combatants_[actor].knows(step.id)) {
				targeting = ability->targeting;
				revives = ability->revives();
				issue = issueFor(checkUsable(actor, *ability));
			} else {
				issue = MacroIssue::UnknownAbility;
			}
			break;
		case CommandKind::Item:
			if (const ItemDef* item = db_->item(step.id)) {
				targeting = item->targeting;
				revives = effectsRevive(item->effects);
				const auto stock = inventory_.find(step.id);
				if (stock == inventory_.end() || stock->second - reserved[step.id] <= 0) {
					issue = MacroIssue::NoItem;
				}
			} else {
				issue = MacroIssue::NoItem;
			}
			break;
		case CommandKind::Escape:
			aimed = false;
			if (!escapeAllowed()) {
				issue = MacroIssue::EscapeNotAllowed;
			}
			break;
		case CommandKind::None:
		case CommandKind::Defend:
			aimed = false;
			break;
		}

		if (issue == MacroIssue::None && aimed) {
			const std::vector<int> targets = resolveTargets(actor, targeting, requested, revives);
			if (targets.empty()) {
				issue = MacroIssue::NoTarget;
			} else if (targeting == Targeting::OneEnemy || targeting == Targeting::EnemyGroup || targeting == Targeting::OneAlly) {
				command.target = targets.front();
				if (intendedGone || (requested >= 0 && targets.front() != requested)) {
					result.status = MacroStepStatus::Retargeted;
					result.issue = MacroIssue::TargetGone;
				}
			}
		}

		if (issue != MacroIssue::None) {
			result.status = MacroStepStatus::FellBack;
			result.issue = issue;
			const std::vector<int> foes = livingFoes(actor);
			const bool intended = enemyThere && combatants_[enemyIndex].alive();
			command = Command::attack(intended ? enemyIndex : (foes.empty() ? -1 : foes.front()));
		}
		if (command.kind == CommandKind::Item) {
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
