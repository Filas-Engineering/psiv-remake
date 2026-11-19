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

bool targetsFoes(Targeting targeting) {
	return targeting == Targeting::OneEnemy || targeting == Targeting::EnemyGroup || targeting == Targeting::AllEnemies;
}

}

// A step that cannot run as written falls back to a basic attack on its intended enemy (or the first
// living one), so the macro still spends every member's turn.
std::vector<MacroStepResult> Battle::applyMacro(const Macro& macro) {
	std::vector<MacroStepResult> results;
	forcedOrder_.clear();
	Inventory reserved;

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
		if (result.actor < 0 || !combatants_[result.actor].alive()) {
			result.status = MacroStepStatus::Skipped;
			result.issue = result.actor < 0 ? MacroIssue::NotInParty : MacroIssue::ActorDefeated;
			results.push_back(result);
			continue;
		}
		const int actor = result.actor;
		const int enemyIndex = step.enemySlot >= 0 ? partyCount_ + step.enemySlot : -1;
		const bool enemyThere = enemyIndex >= partyCount_ && enemyIndex < static_cast<int>(combatants_.size());
		const int allyIndex = step.allyMember.empty() ? -1 : partyIndexOf(step.allyMember);

		Targeting targeting = Targeting::Self;
		bool revives = false;
		FailReason why = FailReason::None;
		Command command;
		command.kind = step.kind;
		command.id = step.id;
		switch (step.kind) {
		case CommandKind::Attack:
			targeting = Targeting::OneEnemy;
			break;
		case CommandKind::Ability:
			if (const AbilityDef* ability = db_->ability(step.id); ability && combatants_[actor].knows(step.id)) {
				targeting = ability->targeting;
				revives = ability->revives();
				why = checkUsable(actor, *ability);
			} else {
				why = FailReason::NoTarget;
			}
			break;
		case CommandKind::Item:
			if (const ItemDef* item = db_->item(step.id)) {
				targeting = item->targeting;
				revives = effectsRevive(item->effects);
				const auto stock = inventory_.find(step.id);
				if (stock == inventory_.end() || stock->second - reserved[step.id] <= 0) {
					why = FailReason::NoItem;
				}
			} else {
				why = FailReason::NoItem;
			}
			break;
		case CommandKind::None:
		case CommandKind::Defend:
		case CommandKind::Flee:
			break;
		}

		if (why == FailReason::None && step.kind != CommandKind::Defend && step.kind != CommandKind::Flee) {
			const int requested = targetsFoes(targeting) ? (enemyThere ? enemyIndex : -1) : allyIndex;
			const std::vector<int> targets = resolveTargets(actor, targeting, requested, revives);
			if (targets.empty()) {
				why = FailReason::NoTarget;
			} else if (targeting == Targeting::OneEnemy || targeting == Targeting::EnemyGroup || targeting == Targeting::OneAlly) {
				command.target = targets.front();
				if (requested >= 0 && targets.front() != requested) {
					result.status = MacroStepStatus::Retargeted;
					result.issue = MacroIssue::TargetGone;
				}
			}
		}

		if (why != FailReason::None) {
			result.status = MacroStepStatus::FellBack;
			result.issue = issueFor(why);
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
