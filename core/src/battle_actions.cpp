#include "rules/battle.h"

#include "rules/formulas.h"
#include "rules/text.h"

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

CancelReason Battle::checkUsable(int actor, const AbilityDef& ability) const {
	const Combatant& c = combatants_[actor];
	if (ability.kind == AbilityKind::Technique) {
		for (const ActiveStatus& active : c.statuses) {
			const StatusDef* def = db_->status(active.id);
			if (def && def->blocksTechniques) {
				return CancelReason::Silenced;
			}
		}
		if (c.tp < ability.cost) {
			return CancelReason::NotEnoughTp;
		}
	}
	if (ability.kind == AbilityKind::Skill) {
		const auto it = c.skillUses.find(ability.id);
		if (it == c.skillUses.end() || it->second <= 0) {
			return CancelReason::NoUsesLeft;
		}
	}
	return CancelReason::None;
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

bool Battle::cancelledByStatus(int actor, const Command& command, std::vector<Event>& out) {
	const std::vector<ActiveStatus> statuses = combatants_[actor].statuses;
	for (const ActiveStatus& active : statuses) {
		const StatusDef* def = db_->status(active.id);
		if (!def || def->action != StatusAction::CannotAct) {
			continue;
		}
		if (def->recoverChance > 0 && rng_.chance(def->recoverChance)) {
			emitStatusRemoved(-1, actor, def->id, StatusEnd::Recovered, out);
			continue;
		}
		cancel(actor, command, CancelReason::ActorUnable, def->id, out);
		return true;
	}
	return false;
}

void Battle::takeTurn(int actor, std::vector<Event>& out) {
	const Command command = commands_[actor];
	if (command.kind == CommandKind::None) {
		return;
	}
	if (!combatants_[actor].alive()) {
		cancel(actor, command, CancelReason::ActorUnable, kDefeatedStatus, out);
		return;
	}
	if (cancelledByStatus(actor, command, out)) {
		return;
	}
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

std::vector<int> Battle::executionTargets(int user, Targeting targeting, int requested, bool revives) {
	if (forced_[user] && requested >= 0 && combatants_[requested].alive()) {
		return {requested};
	}
	std::vector<int> targets = resolveTargets(user, targeting, requested, revives);
	if (targeting == Targeting::RandomEnemy && !targets.empty()) {
		return {targets[rng_.range(0, static_cast<int>(targets.size()) - 1)]};
	}
	return targets;
}

void Battle::noteRetarget(int actor, Targeting targeting, int requested, const std::vector<int>& targets, std::vector<Event>& out) {
	if (requested < 0 || requested >= static_cast<int>(combatants_.size()) || targets.empty()) {
		return;
	}
	int now = requested;
	switch (targeting) {
	case Targeting::OneEnemy:
	case Targeting::OneAlly:
	case Targeting::OneFallenAlly:
		now = targets.front();
		break;
	case Targeting::EnemyGroup:
		if (std::find(targets.begin(), targets.end(), requested) == targets.end()) {
			now = primaryFoe(actor, requested);
		}
		break;
	case Targeting::AllEnemies:
	case Targeting::AllAllies:
	case Targeting::Self:
	case Targeting::RandomEnemy:
		return;
	}
	if (now != requested) {
		Event& e = emit(out, EventType::Retargeted);
		e.actor = actor;
		e.previousTarget = requested;
		e.target = now;
	}
}

void Battle::useAbility(int actor, const AbilityDef& ability, int target, CommandKind kind, std::vector<Event>& out) {
	const Command command = commands_[actor];
	const CancelReason why = checkUsable(actor, ability);
	if (why != CancelReason::None) {
		cancel(actor, command, why, "", out);
		return;
	}
	const std::vector<int> targets = executionTargets(actor, ability.targeting, target, ability.revives());
	if (targets.empty()) {
		cancel(actor, command, CancelReason::NoTarget, "", out);
		return;
	}
	noteRetarget(actor, ability.targeting, target, targets, out);
	Event& e = emit(out, EventType::ActionStarted);
	e.actor = actor;
	e.ability = ability.id;
	e.command = kind;
	e.target = targets.size() == 1 ? targets.front() : -1;
	payCost(actor, ability, out);
	Attacker attacker = attackerOf(actor);
	attacker.rollsToHit = ability.kind == AbilityKind::Attack;
	attacker.singleTarget = ability.targeting == Targeting::OneEnemy || ability.targeting == Targeting::RandomEnemy;
	if (kind == CommandKind::Attack) {
		attacker.element = combatants_[actor].attackElement;
	}
	applyEffects(actor, attacker, ability.id, ability.effects, targets, false, out);
}

void Battle::useItem(int actor, const Command& command, std::vector<Event>& out) {
	const ItemDef* item = db_->item(command.id);
	const auto stock = inventory_.find(command.id);
	if (!item || stock == inventory_.end() || stock->second <= 0) {
		cancel(actor, command, CancelReason::NoItem, "", out);
		return;
	}
	const std::vector<int> targets = executionTargets(actor, item->targeting, command.target, effectsRevive(item->effects));
	if (targets.empty()) {
		cancel(actor, command, CancelReason::NoTarget, "", out);
		return;
	}
	noteRetarget(actor, item->targeting, command.target, targets, out);
	Event& e = emit(out, EventType::ActionStarted);
	e.actor = actor;
	e.ability = item->id;
	e.command = CommandKind::Item;
	e.target = targets.size() == 1 ? targets.front() : -1;
	// An item that changed nothing stays in the bag.
	if (!applyEffects(actor, Attacker(), item->id, item->effects, targets, true, out)) {
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

int Battle::rollSpread() {
	int spread = formulas::kSpreadBase;
	for (int i = 0; i < formulas::kSpreadRolls; ++i) {
		spread += rng_.range(0, formulas::kSpreadRollMax);
	}
	return spread;
}

void Battle::attemptEscape(int actor, std::vector<Event>& out) {
	int partyAgility = 0;
	int enemyAgility = 0;
	bool enemyCanAct = false;
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (!combatants_[i].alive() || combatants_[i].aboard) {
			continue;
		}
		const int agility = combatants_[i].effective(Stat::Agility);
		if (isParty(i)) {
			partyAgility = std::max(partyAgility, agility);
		} else {
			enemyAgility = std::max(enemyAgility, agility);
			bool held = false;
			for (const ActiveStatus& active : combatants_[i].statuses) {
				const StatusDef* def = db_->status(active.id);
				held = held || (def && def->action == StatusAction::CannotAct);
			}
			enemyCanAct = enemyCanAct || !held;
		}
	}
	Event& e = emit(out, EventType::EscapeAttempted);
	e.actor = actor;
	e.command = CommandKind::Escape;
	e.succeeded = !enemyCanAct || formulas::chanceScore(rng_.range(0, formulas::kChanceRollMax), partyAgility, enemyAgility) > formulas::kEscapeFloor;
	if (e.succeeded) {
		outcome_ = Outcome::Escaped;
	}
}

bool Battle::applyEffects(int user, const Attacker& attacker, const std::string& source, const std::vector<Effect>& effects, const std::vector<int>& targets, bool flatPower, std::vector<Event>& out) {
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
				bool critical = false;
				if (attacker.rollsToHit && !flatPower) {
					const int roll = rng_.range(0, formulas::kChanceRollMax);
					const int dodge = t.effective(Stat::Agility) + t.stats.evasion;
					if (formulas::chanceScore(roll, attacker.dexterity, dodge) <= formulas::kHitFloor) {
						Event& e = emit(out, EventType::Missed);
						e.actor = user;
						e.target = target;
						e.ability = source;
						missed = true;
						break;
					}
					critical = attacker.singleTarget && formulas::chanceScore(roll, attacker.dexterity + attacker.critical, dodge) > formulas::kCriticalAbove;
				}
				Event& hit = emit(out, EventType::Hit);
				hit.actor = user;
				hit.target = target;
				hit.ability = source;
				const int attack = flatPower ? effect.power : attacker.strength;
				int bonus = flatPower ? 0 : effect.power;
				if (critical) {
					bonus += formulas::criticalBonus(attack);
					Event& e = emit(out, EventType::Critical);
					e.actor = user;
					e.target = target;
					e.ability = source;
				}
				const int affinity = attacker.element != Element::None ? t.affinity(attacker.element) : 100;
				int damage = formulas::damage(attack, t.effective(Stat::Defense), bonus, affinity, rollSpread());
				if (t.defending && damage > 0) {
					damage = formulas::defended(damage);
				}
				const Effectiveness effectiveness = attacker.element != Element::None ? effectivenessOf(affinity) : Effectiveness::Normal;
				changed = dealDamage(user, target, damage, attacker.element, effectiveness, out) > 0 || changed;
				break;
			}
			case EffectKind::ElementalDamage: {
				const int affinity = t.affinity(effect.element);
				const int attack = flatPower ? effect.power : attacker.mental;
				int damage = formulas::damage(attack, t.effective(Stat::Mental), flatPower ? 0 : effect.power, affinity, rollSpread());
				if (t.defending && damage > 0) {
					damage = formulas::defended(damage);
				}
				changed = dealDamage(user, target, damage, effect.element, effectivenessOf(affinity), out) > 0 || changed;
				break;
			}
			case EffectKind::Heal: {
				const int amount = flatPower ? effect.power : formulas::healAmount(effect.power, attacker.mental, static_cast<int>(combatants_.size()) > partyCount_ ? rollSpread() : formulas::kSpreadMean);
				const int healed = std::min(amount, t.stats.maxHp - t.hp);
				t.hp += healed;
				Event& e = emit(out, EventType::Healed);
				e.actor = user;
				e.target = target;
				e.amount = healed;
				e.hp = t.hp;
				changed = changed || healed > 0;
				break;
			}
			case EffectKind::ApplyStatus: {
				const StatusDef* def = db_->status(effect.status);
				if (!def) {
					break;
				}
				const bool had = t.hasStatus(effect.status);
				if (had && def->stacking == StatusStacking::Keep) {
					Event& e = emit(out, EventType::StatusResisted);
					e.actor = user;
					e.target = target;
					e.status = effect.status;
					e.message = statusMessageKey(effect.status, "unchanged");
					break;
				}
				const int resistance = t.resistance(effect.status);
				if (resistance >= 100 || !rng_.chance(formulas::statusChance(effect.chance, resistance))) {
					Event& e = emit(out, EventType::StatusResisted);
					e.actor = user;
					e.target = target;
					e.status = effect.status;
					e.effectiveness = resistance >= 100 ? Effectiveness::Immune : Effectiveness::Normal;
					e.message = statusMessageKey(effect.status, resistance >= 100 ? "immune" : "resisted");
					break;
				}
				int remaining = def->duration;
				if (had && def->stacking == StatusStacking::Extend) {
					for (ActiveStatus& active : t.statuses) {
						if (active.id == effect.status && active.remaining > 0 && def->duration > 0) {
							active.remaining = std::min(99, active.remaining + def->duration);
						}
						if (active.id == effect.status) {
							remaining = active.remaining;
						}
					}
				} else {
					removeStatus(target, effect.status);
					t.statuses.push_back({effect.status, def->duration});
				}
				Event& e = emit(out, EventType::StatusApplied);
				e.actor = user;
				e.target = target;
				e.status = effect.status;
				e.duration = remaining;
				e.message = statusMessageKey(effect.status, "applied");
				changed = true;
				break;
			}
			case EffectKind::CureStatus:
				if (t.hasStatus(effect.status)) {
					emitStatusRemoved(user, target, effect.status, StatusEnd::Cured, out);
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
				e.duration = effect.duration;
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
					e.hp = t.hp;
					changed = true;
				}
				break;
			}
		}
	}
	return changed;
}

int Battle::dealDamage(int user, int target, int amount, Element element, Effectiveness effectiveness, std::vector<Event>& out) {
	Combatant& t = combatants_[target];
	const int dealt = std::min(amount, t.hp);
	t.hp -= dealt;
	if (dealt > 0 && user >= 0 && combatants_[user].side != t.side) {
		t.lastAttacker = user;
	}
	Event& e = emit(out, EventType::DamageDealt);
	e.actor = user;
	e.target = target;
	e.amount = dealt;
	e.hp = t.hp;
	e.element = element;
	e.effectiveness = effectiveness;
	if (!t.alive()) {
		defeat(target, out);
	} else if (dealt > 0) {
		const std::vector<ActiveStatus> statuses = t.statuses;
		for (const ActiveStatus& active : statuses) {
			const StatusDef* def = db_->status(active.id);
			if (def && def->wakesOnDamage) {
				emitStatusRemoved(-1, target, active.id, StatusEnd::WokeOnDamage, out);
			}
		}
	}
	if (target == vehicleIndex_) {
		passDamageToCrew(user, dealt, out);
	}
	return dealt;
}

void Battle::passDamageToCrew(int user, int dealt, std::vector<Event>& out) {
	const int amount = dealt * crewDamagePercent_ / 100;
	if (amount <= 0) {
		return;
	}
	for (int i = 0; i < vehicleIndex_; ++i) {
		if (combatants_[i].alive()) {
			dealDamage(user, i, amount, Element::None, Effectiveness::Normal, out);
		}
	}
}

// Every status the fall clears gets its own removal event first, so the stream shows each change once.
void Battle::defeat(int target, std::vector<Event>& out) {
	const std::vector<ActiveStatus> statuses = combatants_[target].statuses;
	for (const ActiveStatus& active : statuses) {
		if (active.id != kDefeatedStatus) {
			emitStatusRemoved(-1, target, active.id, StatusEnd::Defeated, out);
		}
	}
	Combatant& t = combatants_[target];
	t.hp = 0;
	t.statuses = {{kDefeatedStatus, 0}};
	t.modifiers.clear();
	t.defending = false;
	if (isParty(target) && !combatants_[target].aboard && autoActive_) {
		autoMemberFell_ = true;
	}
	Event& e = emit(out, EventType::CombatantDefeated);
	e.target = target;
}

void Battle::removeStatus(int target, const std::string& statusId) {
	std::vector<ActiveStatus>& statuses = combatants_[target].statuses;
	statuses.erase(std::remove_if(statuses.begin(), statuses.end(), [&](const ActiveStatus& s) {
		return s.id == statusId;
	}), statuses.end());
}

void Battle::emitStatusRemoved(int actor, int target, const std::string& statusId, StatusEnd end, std::vector<Event>& out) {
	removeStatus(target, statusId);
	Event& e = emit(out, EventType::StatusRemoved);
	e.actor = actor;
	e.target = target;
	e.status = statusId;
	e.statusEnd = end;
	e.message = statusMessageKey(statusId, "removed");
}

}
