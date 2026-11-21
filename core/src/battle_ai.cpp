#include "rules/battle.h"

#include <algorithm>

namespace rules {

namespace {

bool atMostPercent(const Combatant& c, int percent) {
	return c.hp * 100 <= c.stats.maxHp * percent;
}

bool lowerHpShare(const Combatant& a, const Combatant& b) {
	return a.hp * b.stats.maxHp < b.hp * a.stats.maxHp;
}

}

bool Battle::overrideHolds(const AiOverride& entry, int actor) const {
	const Combatant& self = combatants_[actor];
	switch (entry.when) {
	case AiConditionKind::SelfHpHalf:
		return atMostPercent(self, 50);
	case AiConditionKind::SelfHpQuarterOnce:
		return !quarterOverrideUsed_ && atMostPercent(self, 25);
	case AiConditionKind::AnyAllyHpHalf:
		for (int ally : allies(actor)) {
			if (combatants_[ally].alive() && atMostPercent(combatants_[ally], 50)) {
				return true;
			}
		}
		return false;
	case AiConditionKind::LastStanding:
		for (int ally : allies(actor)) {
			if (ally != actor && combatants_[ally].alive()) {
				return false;
			}
		}
		return true;
	case AiConditionKind::HitByAttack:
		return self.hitByAttack;
	case AiConditionKind::HitByAbility:
		return self.hitByAbility;
	case AiConditionKind::HitByTechnique:
		return self.hitByTechnique;
	case AiConditionKind::TechniquesBlocked:
		for (const ActiveStatus& active : self.statuses) {
			const StatusDef* def = db_->status(active.id);
			if (def && def->blocksTechniques) {
				return true;
			}
		}
		return false;
	case AiConditionKind::HitByGroup:
		return self.hitByGroup;
	case AiConditionKind::Ambushed:
		return self.ambushed;
	case AiConditionKind::AllyPresent:
	case AiConditionKind::AllyAbsent: {
		bool present = false;
		for (int ally : allies(actor)) {
			present = present || (ally != actor && combatants_[ally].alive() && combatants_[ally].defId == entry.enemy);
		}
		return present == (entry.when == AiConditionKind::AllyPresent);
	}
	case AiConditionKind::FreeSlotBeside:
		return !freeSlotsBeside(actor).empty();
	case AiConditionKind::AfterTurns:
		return self.turnsTaken >= entry.turns;
	case AiConditionKind::GroupIntact:
		for (int i = partyCount_; i < startingCount_; ++i) {
			if (combatants_[i].group == self.group && !combatants_[i].alive()) {
				return false;
			}
		}
		return true;
	}
	return false;
}

// The combatants an action may aim at. Empty means it has no target.
std::vector<int> Battle::aiCandidates(int actor, const AbilityDef& ability, const AiTarget& target) const {
	std::vector<int> pool;
	switch (ability.targeting) {
	case Targeting::OneEnemy:
	case Targeting::EnemyGroup:
		pool = livingFoes(actor);
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
		const bool has = combatants_[candidate].hasStatus(target.status);
		switch (target.rule) {
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

int Battle::aiTarget(int actor, const AbilityDef& ability, const AiTarget& target) {
	const std::vector<int> candidates = aiCandidates(actor, ability, target);
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
	switch (target.rule) {
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
		return commands_[actor].target;
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
	const bool transforms = ability.hasEffect(EffectKind::Transform);
	auto canTransform = [this](int index) {
		const EnemyDef* def = isParty(index) ? nullptr : db_->enemy(combatants_[index].defId);
		return def && !def->transformsInto.empty();
	};
	int best = actor;
	for (int ally : candidates) {
		const bool better = transforms && canTransform(ally) != canTransform(best) ? canTransform(ally) : lowerHpShare(combatants_[ally], combatants_[best]);
		if (better) {
			best = ally;
		}
	}
	return best;
}

// At the start of the enemy's turn (BAT-091): re-draw a fallen foe, draw one of the eight slots (never the index
// any enemy drew last), then let the first override whose condition holds and that has a target replace it. An
// action the enemy cannot pay for, or that finds no target, becomes the basic attack on the drawn foe.
void Battle::chooseEnemyAction(int actor, std::vector<Event>& out) {
	Command& command = commands_[actor];
	if (command.target >= 0 && !combatants_[command.target].alive()) {
		Event& e = emit(out, EventType::Retargeted);
		e.actor = actor;
		e.previousTarget = command.target;
		command.target = drawFoeByPartyOrder(actor);
		e.target = command.target;
	}
	const EnemyDef* def = db_->enemy(combatants_[actor].defId);
	if (!def || def->slots.empty()) {
		return;
	}
	int slot = 0;
	if (previousSlot_ < 0) {
		slot = rng_.range(0, kAiSlots - 1);
	} else {
		slot = rng_.range(0, kAiSlots - 2);
		slot += slot >= previousSlot_ ? 1 : 0;
	}
	previousSlot_ = slot;
	const AbilityDef* chosen = db_->ability(def->slots[static_cast<std::size_t>(slot)]);
	AiTarget rule;
	for (const AiOverride& entry : def->overrides) {
		const AbilityDef* ability = db_->ability(entry.ability);
		if (!ability || !overrideHolds(entry, actor) || aiCandidates(actor, *ability, entry.target).empty()) {
			continue;
		}
		Combatant& self = combatants_[actor];
		switch (entry.when) {
		case AiConditionKind::SelfHpQuarterOnce:
			quarterOverrideUsed_ = true;
			break;
		case AiConditionKind::HitByAttack:
		case AiConditionKind::HitByAbility:
		case AiConditionKind::HitByTechnique:
		case AiConditionKind::HitByGroup:
			self.hitByAttack = self.hitByAbility = self.hitByTechnique = self.hitByGroup = false;
			break;
		case AiConditionKind::Ambushed:
			self.ambushed = false;
			break;
		case AiConditionKind::SelfHpHalf:
		case AiConditionKind::AnyAllyHpHalf:
		case AiConditionKind::LastStanding:
		case AiConditionKind::TechniquesBlocked:
		case AiConditionKind::AfterTurns:
		case AiConditionKind::AllyPresent:
		case AiConditionKind::AllyAbsent:
		case AiConditionKind::FreeSlotBeside:
		case AiConditionKind::GroupIntact:
			break;
		}
		chosen = ability;
		rule = entry.target;
		break;
	}
	if (db_->battleRules.ambushOpening == Opening::PartyFirst) {
		combatants_[actor].ambushed = false;
	}
	++combatants_[actor].turnsTaken;
	const CancelReason why = chosen ? checkUsable(actor, *chosen) : CancelReason::None;
	if (!chosen || why == CancelReason::NotEnoughTp || why == CancelReason::NoUsesLeft || aiCandidates(actor, *chosen, rule).empty()) {
		command = Command::attack(command.target);
		return;
	}
	const int target = aiTarget(actor, *chosen, rule);
	command = chosen->id == combatants_[actor].basicAttack ? Command::attack(target) : Command::ability(chosen->id, target);
}

}
