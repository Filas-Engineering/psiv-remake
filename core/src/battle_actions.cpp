#include "rules/battle.h"

#include "rules/formulas.h"

#include <algorithm>

namespace rules {

namespace {

Effectiveness effectivenessOf(int affinity) {
	if (affinity <= 0) {
		return Effectiveness::Immune;
	}
	if (affinity > 100) {
		return Effectiveness::Weak;
	}
	return affinity < 100 ? Effectiveness::Resisted : Effectiveness::Normal;
}

}

FailReason Battle::checkUsable(int actor, const AbilityDef& ability) const {
	const Combatant& c = combatants_[actor];
	if (ability.kind == AbilityKind::Technique) {
		for (const ActiveStatus& active : c.statuses) {
			const StatusDef* def = db_->status(active.id);
			if (def && def->blocksTechniques) {
				return FailReason::Silenced;
			}
		}
		if (c.tp < ability.cost) {
			return FailReason::NotEnoughTp;
		}
	}
	if (ability.kind == AbilityKind::Skill) {
		const auto it = c.skillUses.find(ability.id);
		if (it == c.skillUses.end() || it->second <= 0) {
			return FailReason::NoUsesLeft;
		}
	}
	return FailReason::None;
}

void Battle::payCost(int actor, const AbilityDef& ability, std::vector<Event>& out) {
	Combatant& c = combatants_[actor];
	ResourceKind resource = ResourceKind::None;
	int amount = 0;
	if (ability.kind == AbilityKind::Technique && ability.cost > 0) {
		c.tp -= ability.cost;
		resource = ResourceKind::Tp;
		amount = ability.cost;
	} else if (ability.kind == AbilityKind::Skill) {
		--c.skillUses[ability.id];
		resource = ResourceKind::SkillUse;
		amount = 1;
	}
	if (resource != ResourceKind::None) {
		Event& e = emit(out, EventType::ResourceSpent);
		e.actor = actor;
		e.ability = ability.id;
		e.resource = resource;
		e.amount = amount;
	}
}

bool Battle::skipsTurn(int actor) const {
	for (const ActiveStatus& active : combatants_[actor].statuses) {
		const StatusDef* def = db_->status(active.id);
		if (def && def->skipsTurn) {
			return true;
		}
	}
	return false;
}

bool Battle::preventedByStatus(int actor, std::vector<Event>& out) {
	const std::vector<ActiveStatus> statuses = combatants_[actor].statuses;
	for (const ActiveStatus& active : statuses) {
		const StatusDef* def = db_->status(active.id);
		if (!def || !def->skipsTurn) {
			continue;
		}
		if (def->recoverChance > 0 && rng_.chance(def->recoverChance)) {
			removeStatus(actor, def->id);
			Event& e = emit(out, EventType::StatusExpired);
			e.target = actor;
			e.status = def->id;
			e.statusEnd = StatusEnd::Recovered;
			continue;
		}
		Event& e = emit(out, EventType::ActionPrevented);
		e.actor = actor;
		e.status = def->id;
		return true;
	}
	return false;
}

void Battle::takeTurn(int actor, std::vector<Event>& out) {
	if (!combatants_[actor].alive() || preventedByStatus(actor, out)) {
		return;
	}
	const Command command = commands_[actor];
	switch (command.kind) {
	case CommandKind::None:
		return;
	case CommandKind::Defend: {
		Event& e = emit(out, EventType::ActionStarted);
		e.actor = actor;
		e.command = CommandKind::Defend;
		return;
	}
	case CommandKind::Escape:
		attemptEscape(actor, out);
		return;
	case CommandKind::Attack:
		if (const AbilityDef* ability = db_->ability(combatants_[actor].basicAttack)) {
			useAbility(actor, *ability, command.target, CommandKind::Attack, out);
		}
		return;
	case CommandKind::Ability:
		if (const AbilityDef* ability = db_->ability(command.id)) {
			useAbility(actor, *ability, command.target, CommandKind::Ability, out);
		}
		return;
	case CommandKind::Item:
		useItem(actor, command, out);
		return;
	}
}

void Battle::useAbility(int actor, const AbilityDef& ability, int target, CommandKind kind, std::vector<Event>& out) {
	const FailReason why = checkUsable(actor, ability);
	const std::vector<int> targets = why == FailReason::None ? resolveTargets(actor, ability.targeting, target, ability.revives()) : std::vector<int>();
	if (why != FailReason::None || targets.empty()) {
		Event& e = emit(out, EventType::ActionFailed);
		e.actor = actor;
		e.ability = ability.id;
		e.command = kind;
		e.fail = why != FailReason::None ? why : FailReason::NoTarget;
		return;
	}
	Event& e = emit(out, EventType::ActionStarted);
	e.actor = actor;
	e.ability = ability.id;
	e.command = kind;
	e.target = targets.size() == 1 ? targets.front() : -1;
	payCost(actor, ability, out);
	Attacker attacker = attackerOf(actor);
	if (kind == CommandKind::Attack) {
		attacker.element = combatants_[actor].attackElement;
	}
	applyEffects(actor, attacker, ability.effects, targets, false, out);
}

void Battle::useItem(int actor, const Command& command, std::vector<Event>& out) {
	const ItemDef* item = db_->item(command.id);
	const auto stock = inventory_.find(command.id);
	const bool inStock = item && stock != inventory_.end() && stock->second > 0;
	const std::vector<int> targets = inStock ? resolveTargets(actor, item->targeting, command.target, effectsRevive(item->effects)) : std::vector<int>();
	if (!inStock || targets.empty()) {
		Event& e = emit(out, EventType::ActionFailed);
		e.actor = actor;
		e.ability = command.id;
		e.command = CommandKind::Item;
		e.fail = inStock ? FailReason::NoTarget : FailReason::NoItem;
		return;
	}
	Event& e = emit(out, EventType::ActionStarted);
	e.actor = actor;
	e.ability = item->id;
	e.command = CommandKind::Item;
	e.target = targets.size() == 1 ? targets.front() : -1;
	// An item that changed nothing stays in the bag.
	if (!applyEffects(actor, Attacker(), item->effects, targets, true, out)) {
		Event& kept = emit(out, EventType::ItemHadNoEffect);
		kept.actor = actor;
		kept.ability = item->id;
		return;
	}
	--stock->second;
	Event& spent = emit(out, EventType::ResourceSpent);
	spent.actor = actor;
	spent.ability = item->id;
	spent.resource = ResourceKind::Item;
	spent.amount = 1;
}

void Battle::attemptEscape(int actor, std::vector<Event>& out) {
	int partyAgility = 0;
	int partyCount = 0;
	int enemyAgility = 0;
	int enemyCount = 0;
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (!combatants_[i].alive()) {
			continue;
		}
		const int agility = combatants_[i].effective(Stat::Agility);
		if (isParty(i)) {
			partyAgility += agility;
			++partyCount;
		} else {
			enemyAgility += agility;
			++enemyCount;
		}
	}
	const int chance = formulas::escapeChance(partyAgility / std::max(1, partyCount), enemyAgility / std::max(1, enemyCount), db_->battleRules.escape);
	Event& e = emit(out, EventType::EscapeAttempted);
	e.actor = actor;
	e.command = CommandKind::Escape;
	e.succeeded = rng_.chance(chance);
	if (e.succeeded) {
		outcome_ = Outcome::Escaped;
	}
}

bool Battle::applyEffects(int user, const Attacker& attacker, const std::vector<Effect>& effects, const std::vector<int>& targets, bool flatPower, std::vector<Event>& out) {
	bool changed = false;
	for (int target : targets) {
		bool missed = false;
		for (const Effect& effect : effects) {
			Combatant& t = combatants_[target];
			if (missed || (effect.kind != EffectKind::Revive && !t.alive())) {
				break;
			}
			switch (effect.kind) {
			case EffectKind::PhysicalDamage: {
				const int chance = flatPower ? 100 : formulas::hitChance(attacker.dexterity, t.effective(Stat::Agility), t.stats.evasion);
				if (!rng_.chance(chance)) {
					Event& e = emit(out, EventType::Missed);
					e.actor = user;
					e.target = target;
					missed = true;
					break;
				}
				const bool critical = !flatPower && rng_.chance(formulas::criticalChance(attacker.critical, effect.critical));
				const int variance = rng_.range(formulas::kVarianceMin, formulas::kVarianceMax);
				const int strength = flatPower ? effect.power : attacker.strength;
				const int power = flatPower ? 100 : effect.power;
				int damage = formulas::physicalDamage(strength, t.effective(Stat::Defense), power, variance);
				if (critical) {
					damage = formulas::criticalDamage(damage, db_->battleRules.criticalDamagePercent);
					Event& e = emit(out, EventType::Critical);
					e.actor = user;
					e.target = target;
				}
				Effectiveness effectiveness = Effectiveness::Normal;
				if (attacker.element != Element::None) {
					const int affinity = t.affinity(attacker.element);
					damage = formulas::elementScaled(damage, affinity);
					effectiveness = effectivenessOf(affinity);
				}
				if (t.defending && damage > 0) {
					damage = formulas::defended(damage);
				}
				changed = dealDamage(target, damage, attacker.element, effectiveness, out) > 0 || changed;
				break;
			}
			case EffectKind::ElementalDamage: {
				const int affinity = t.affinity(effect.element);
				const int variance = rng_.range(formulas::kVarianceMin, formulas::kVarianceMax);
				const int mental = flatPower ? 0 : attacker.mental;
				int damage = formulas::elementalDamage(effect.power, mental, t.effective(Stat::Mental), affinity, variance);
				if (t.defending && damage > 0) {
					damage = formulas::defended(damage);
				}
				changed = dealDamage(target, damage, effect.element, effectivenessOf(affinity), out) > 0 || changed;
				break;
			}
			case EffectKind::Heal: {
				const int amount = flatPower ? effect.power : formulas::healAmount(effect.power, attacker.mental);
				const int healed = std::min(amount, t.stats.maxHp - t.hp);
				t.hp += healed;
				Event& e = emit(out, EventType::Healed);
				e.actor = user;
				e.target = target;
				e.amount = healed;
				changed = changed || healed > 0;
				break;
			}
			case EffectKind::ApplyStatus: {
				const StatusDef* def = db_->status(effect.status);
				if (!def) {
					break;
				}
				const int resistance = t.resistance(effect.status);
				if (resistance >= 100 || !rng_.chance(formulas::statusChance(effect.chance, resistance))) {
					Event& e = emit(out, EventType::StatusResisted);
					e.actor = user;
					e.target = target;
					e.status = effect.status;
					e.effectiveness = resistance >= 100 ? Effectiveness::Immune : Effectiveness::Normal;
					break;
				}
				removeStatus(target, effect.status);
				t.statuses.push_back({effect.status, def->duration});
				Event& e = emit(out, EventType::StatusApplied);
				e.actor = user;
				e.target = target;
				e.status = effect.status;
				changed = true;
				break;
			}
			case EffectKind::CureStatus:
				if (t.hasStatus(effect.status)) {
					removeStatus(target, effect.status);
					Event& e = emit(out, EventType::StatusCured);
					e.actor = user;
					e.target = target;
					e.status = effect.status;
					changed = true;
				}
				break;
			case EffectKind::ModifyStat: {
				// One modifier per stat: a new one replaces the old.
				t.modifiers.erase(std::remove_if(t.modifiers.begin(), t.modifiers.end(), [&](const ActiveModifier& m) {
					return m.stat == effect.stat;
				}), t.modifiers.end());
				t.modifiers.push_back({effect.stat, effect.percent, effect.duration});
				Event& e = emit(out, EventType::StatModified);
				e.actor = user;
				e.target = target;
				e.stat = effect.stat;
				e.amount = effect.percent;
				changed = true;
				break;
			}
			case EffectKind::Revive:
				if (!t.alive()) {
					t.hp = formulas::reviveHp(t.stats.maxHp, effect.power);
					removeStatus(target, kDefeatedStatus);
					Event& e = emit(out, EventType::Revived);
					e.actor = user;
					e.target = target;
					e.amount = t.hp;
					changed = true;
				}
				break;
			}
		}
	}
	return changed;
}

int Battle::dealDamage(int target, int amount, Element element, Effectiveness effectiveness, std::vector<Event>& out) {
	Combatant& t = combatants_[target];
	const int dealt = std::min(amount, t.hp);
	t.hp -= dealt;
	Event& e = emit(out, EventType::DamageDealt);
	e.target = target;
	e.amount = dealt;
	e.element = element;
	e.effectiveness = effectiveness;
	if (!t.alive()) {
		defeat(target, out);
		return dealt;
	}
	if (dealt == 0) {
		return dealt;
	}
	const std::vector<ActiveStatus> statuses = t.statuses;
	for (const ActiveStatus& active : statuses) {
		const StatusDef* def = db_->status(active.id);
		if (def && def->wakesOnDamage) {
			removeStatus(target, active.id);
			Event& woke = emit(out, EventType::StatusExpired);
			woke.target = target;
			woke.status = active.id;
			woke.statusEnd = StatusEnd::WokeOnDamage;
		}
	}
	return dealt;
}

void Battle::defeat(int target, std::vector<Event>& out) {
	Combatant& t = combatants_[target];
	t.hp = 0;
	t.statuses.clear();
	t.statuses.push_back({kDefeatedStatus, 0});
	t.modifiers.clear();
	t.defending = false;
	Event& e = emit(out, EventType::CombatantDefeated);
	e.target = target;
}

void Battle::removeStatus(int target, const std::string& statusId) {
	std::vector<ActiveStatus>& statuses = combatants_[target].statuses;
	statuses.erase(std::remove_if(statuses.begin(), statuses.end(), [&](const ActiveStatus& s) {
		return s.id == statusId;
	}), statuses.end());
}

}
