#include "rules/battle.h"

namespace rules {

namespace {

bool hpBelow(const Combatant& c, int percent) {
	return c.hp * 100 < c.stats.maxHp * percent;
}

bool lowerHpShare(const Combatant& a, const Combatant& b) {
	return a.hp * b.stats.maxHp < b.hp * a.stats.maxHp;
}

}

bool Battle::conditionHolds(const AiCondition& condition, int actor) const {
	const Combatant& self = combatants_[actor];
	switch (condition.kind) {
	case AiConditionKind::Always:
		return true;
	case AiConditionKind::SelfHpBelow:
		return hpBelow(self, condition.value);
	case AiConditionKind::SelfHasStatus:
		return self.hasStatus(condition.status);
	case AiConditionKind::AllyHpBelow:
	case AiConditionKind::AllyDefeated:
		for (int ally : allies(actor)) {
			const Combatant& c = combatants_[ally];
			if (condition.kind == AiConditionKind::AllyDefeated ? ally != actor && !c.alive() : c.alive() && hpBelow(c, condition.value)) {
				return true;
			}
		}
		return false;
	case AiConditionKind::FoeHasStatus:
		for (int foe : livingFoes(actor)) {
			if (combatants_[foe].hasStatus(condition.status)) {
				return true;
			}
		}
		return false;
	}
	return false;
}

int Battle::aiTarget(int actor, const AbilityDef& ability, const AiCondition& condition) {
	switch (ability.targeting) {
	case Targeting::OneEnemy:
	case Targeting::EnemyGroup: {
		std::vector<int> candidates = livingFoes(actor);
		if (condition.kind == AiConditionKind::FoeHasStatus) {
			std::vector<int> marked;
			for (int foe : candidates) {
				if (combatants_[foe].hasStatus(condition.status)) {
					marked.push_back(foe);
				}
			}
			if (!marked.empty()) {
				candidates = marked;
			}
		}
		if (candidates.empty()) {
			return -1;
		}
		return candidates[rng_.range(0, static_cast<int>(candidates.size()) - 1)];
	}
	case Targeting::OneAlly: {
		if (ability.revives()) {
			return resolveTargets(actor, ability.targeting, -1, true).front();
		}
		int best = actor;
		for (int ally : allies(actor)) {
			if (combatants_[ally].alive() && lowerHpShare(combatants_[ally], combatants_[best])) {
				best = ally;
			}
		}
		for (const Effect& effect : ability.effects) {
			if (effect.kind != EffectKind::CureStatus) {
				continue;
			}
			for (int ally : allies(actor)) {
				if (combatants_[ally].alive() && combatants_[ally].hasStatus(effect.status)) {
					return ally;
				}
			}
		}
		return best;
	}
	case Targeting::AllEnemies:
	case Targeting::AllAllies:
	case Targeting::Self:
		return -1;
	}
	return -1;
}

Command Battle::chooseEnemyCommand(int actor) {
	const Combatant& self = combatants_[actor];
	const EnemyDef* def = db_->enemy(self.defId);
	std::vector<const AiRule*> options;
	int total = 0;
	if (def) {
		for (const AiRule& rule : def->ai) {
			const AbilityDef* ability = db_->ability(rule.ability);
			if (!ability || !conditionHolds(rule.condition, actor) || checkUsable(actor, *ability) != FailReason::None) {
				continue;
			}
			if (resolveTargets(actor, ability->targeting, -1, ability->revives()).empty()) {
				continue;
			}
			options.push_back(&rule);
			total += rule.weight;
		}
	}
	if (options.empty()) {
		const std::vector<int> foes = livingFoes(actor);
		return Command::attack(foes.empty() ? -1 : foes[rng_.range(0, static_cast<int>(foes.size()) - 1)]);
	}
	int roll = rng_.range(1, total);
	const AiRule* chosen = options.back();
	for (const AiRule* rule : options) {
		roll -= rule->weight;
		if (roll <= 0) {
			chosen = rule;
			break;
		}
	}
	const AbilityDef& ability = *db_->ability(chosen->ability);
	const int target = aiTarget(actor, ability, chosen->condition);
	if (ability.id == self.basicAttack) {
		return Command::attack(target);
	}
	return Command::ability(ability.id, target);
}

}
