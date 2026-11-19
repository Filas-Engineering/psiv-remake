#include "rules/battle.h"

#include <algorithm>

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

// The combatants an entry may aim at. Empty means the entry has no target and does not qualify.
std::vector<int> Battle::aiCandidates(int actor, const AbilityDef& ability, const AiRule& rule) const {
	std::vector<int> pool;
	switch (ability.targeting) {
	case Targeting::OneEnemy:
	case Targeting::EnemyGroup:
		pool = livingFoes(actor);
		if (rule.target.rule == AiTargetRule::Default && rule.condition.kind == AiConditionKind::FoeHasStatus) {
			std::vector<int> marked;
			for (int foe : pool) {
				if (combatants_[foe].hasStatus(rule.condition.status)) {
					marked.push_back(foe);
				}
			}
			if (!marked.empty()) {
				pool = marked;
			}
		}
		break;
	case Targeting::OneAlly:
		for (int ally : allies(actor)) {
			if (combatants_[ally].alive()) {
				pool.push_back(ally);
			}
		}
		break;
	case Targeting::OneFallenAlly:
		for (int ally : allies(actor)) {
			if (!combatants_[ally].alive()) {
				pool.push_back(ally);
			}
		}
		break;
	case Targeting::AllEnemies:
	case Targeting::AllAllies:
	case Targeting::Self:
	case Targeting::RandomEnemy:
		return resolveTargets(actor, ability.targeting, -1, ability.revives());
	}
	std::vector<int> kept;
	for (int candidate : pool) {
		const bool has = combatants_[candidate].hasStatus(rule.target.status);
		switch (rule.target.rule) {
		case AiTargetRule::HasStatus:
			if (has) {
				kept.push_back(candidate);
			}
			break;
		case AiTargetRule::LacksStatus:
			if (!has) {
				kept.push_back(candidate);
			}
			break;
		case AiTargetRule::LastAttacker:
			if (candidate == combatants_[actor].lastAttacker) {
				kept.push_back(candidate);
			}
			break;
		case AiTargetRule::Default:
		case AiTargetRule::Random:
		case AiTargetRule::LowestHp:
		case AiTargetRule::HighestHp:
			kept.push_back(candidate);
			break;
		}
	}
	return kept;
}

int Battle::aiTarget(int actor, const AbilityDef& ability, const AiRule& rule) {
	const std::vector<int> candidates = aiCandidates(actor, ability, rule);
	if (candidates.empty()) {
		return -1;
	}
	switch (ability.targeting) {
	case Targeting::AllEnemies:
	case Targeting::AllAllies:
	case Targeting::Self:
	case Targeting::RandomEnemy:
		return -1;
	case Targeting::OneEnemy:
	case Targeting::EnemyGroup:
	case Targeting::OneAlly:
	case Targeting::OneFallenAlly:
		break;
	}
	auto byHp = [this](int a, int b) {
		return combatants_[a].hp != combatants_[b].hp ? combatants_[a].hp < combatants_[b].hp : a < b;
	};
	switch (rule.target.rule) {
	case AiTargetRule::LowestHp:
		return *std::min_element(candidates.begin(), candidates.end(), byHp);
	case AiTargetRule::HighestHp:
		return *std::max_element(candidates.begin(), candidates.end(), [&](int a, int b) {
			return combatants_[a].hp != combatants_[b].hp ? combatants_[a].hp < combatants_[b].hp : a > b;
		});
	case AiTargetRule::LastAttacker:
		return candidates.front();
	case AiTargetRule::Random:
	case AiTargetRule::HasStatus:
	case AiTargetRule::LacksStatus:
		return candidates[rng_.range(0, static_cast<int>(candidates.size()) - 1)];
	case AiTargetRule::Default:
		break;
	}
	if (ability.targeting == Targeting::OneEnemy || ability.targeting == Targeting::EnemyGroup) {
		return candidates[rng_.range(0, static_cast<int>(candidates.size()) - 1)];
	}
	if (ability.targeting == Targeting::OneFallenAlly) {
		return candidates.front();
	}
	for (const Effect& effect : ability.effects) {
		if (effect.kind != EffectKind::CureStatus) {
			continue;
		}
		for (int ally : candidates) {
			if (combatants_[ally].hasStatus(effect.status)) {
				return ally;
			}
		}
	}
	int best = actor;
	for (int ally : candidates) {
		if (lowerHpShare(combatants_[ally], combatants_[best])) {
			best = ally;
		}
	}
	return best;
}

Command Battle::chooseEnemyCommand(int actor) {
	const Combatant& self = combatants_[actor];
	const EnemyDef* def = db_->enemy(self.defId);
	std::vector<const AiRule*> options;
	int total = 0;
	if (def) {
		for (const AiRule& rule : def->ai) {
			const AbilityDef* ability = db_->ability(rule.ability);
			if (!ability || !conditionHolds(rule.condition, actor) || checkUsable(actor, *ability) != CancelReason::None) {
				continue;
			}
			if (aiCandidates(actor, *ability, rule).empty()) {
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
	const int target = aiTarget(actor, ability, *chosen);
	if (ability.id == self.basicAttack) {
		return Command::attack(target);
	}
	return Command::ability(ability.id, target);
}

}
