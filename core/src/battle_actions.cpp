#include "rules/battle.h"

#include "rules/formulas.h"
#include "rules/text.h"

#include <algorithm>

namespace rules {

namespace {

Effectiveness effectivenessOf(int affinity, int divisor) {
	if (affinity <= 0) {
		return Effectiveness::Immune;
	}
	if (affinity > divisor) {
		return Effectiveness::Weak;
	}
	return affinity < divisor ? Effectiveness::Resisted : Effectiveness::Normal;
}

}

CancelReason Battle::checkUsable(int actor, const AbilityDef& ability) const {
	const Combatant& c = combatants_[actor];
	if (ability.kind == AbilityKind::Technique) {
		if (c.tp < ability.cost) {
			return CancelReason::NotEnoughTp;
		}
		for (const ActiveStatus& active : c.statuses) {
			const StatusDef* def = db_->status(active.id);
			if (def && def->blocksTechniques) {
				return CancelReason::Silenced;
			}
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
		cancel(actor, command, CancelReason::ActorUnable, def->id, out);
		return true;
	}
	return false;
}

// An actor that cannot act loses the turn and pays nothing. An enemy the AI drives picks its action only now.
int Battle::takeTurn(const std::vector<int>& order, std::size_t index, std::vector<Event>& out) {
	const int actor = order[index];
	if (commands_[actor].kind == CommandKind::None) {
		return 1;
	}
	if (!combatants_[actor].alive()) {
		cancel(actor, commands_[actor], CancelReason::ActorUnable, kDefeatedStatus, out);
		return 1;
	}
	if (cancelledByStatus(actor, commands_[actor], out)) {
		return 1;
	}
	if (aiTurn_[actor]) {
		chooseEnemyAction(actor, out);
		if (const int consumed = tryEnemyCombo(order, index, out)) {
			return consumed;
		}
	}
	const Command command = commands_[actor];
	switch (command.kind) {
	case CommandKind::None:
		break;
	case CommandKind::Defend: {
		Event& e = emit(out, EventType::ActionStarted);
		e.actor = actor;
		e.command = CommandKind::Defend;
		break;
	}
	case CommandKind::Escape:
		attemptEscape(actor, out);
		break;
	case CommandKind::Attack:
		if (const AbilityDef* ability = db_->ability(combatants_[actor].basicAttack)) {
			useAbility(actor, *ability, command.target, CommandKind::Attack, out);
		}
		break;
	case CommandKind::Ability:
		if (const AbilityDef* ability = db_->ability(command.id)) {
			useAbility(actor, *ability, command.target, CommandKind::Ability, out);
		}
		break;
	case CommandKind::Item:
		useItem(actor, command, out);
		break;
	}
	return 1;
}

void Battle::useAbility(int actor, const AbilityDef& ability, int target, CommandKind kind, std::vector<Event>& out) {
	const Command command = commands_[actor];
	const CancelReason why = checkUsable(actor, ability);
	// Blocked techniques still cost their points (BAT-081).
	if (why == CancelReason::Silenced) {
		payCost(actor, ability, out);
	}
	if (why != CancelReason::None) {
		cancel(actor, command, why, "", out);
		return;
	}
	const std::vector<int> targets = executionTargets(actor, ability.targeting, ability.effects, target, out);
	if (targets.empty()) {
		cancel(actor, command, CancelReason::NoTarget, "", out);
		return;
	}
	Event& e = emit(out, EventType::ActionStarted);
	e.actor = actor;
	e.ability = ability.id;
	e.command = kind;
	e.target = targets.size() == 1 ? targets.front() : -1;
	payCost(actor, ability, out);
	Attacker attacker = attackerOf(actor);
	attacker.rollsToHit = ability.kind == AbilityKind::Attack;
	attacker.singleTarget = ability.targeting == Targeting::OneEnemy || ability.targeting == Targeting::RandomEnemy;
	attacker.technique = ability.kind == AbilityKind::Technique;
	attacker.groupTarget = ability.targeting == Targeting::EnemyGroup || ability.targeting == Targeting::AllEnemies;
	attacker.changeAmount = attacker.technique ? attacker.mental : attacker.strength;
	if (ability.attackStat) {
		attacker.strength = attacker.mental = attacker.changeAmount = combatants_[actor].effective(*ability.attackStat);
	}
	if (kind == CommandKind::Attack) {
		attacker.element = combatants_[actor].attackElement;
	}
	applyEffects(actor, attacker, ability.id, ability.effects, targets, false, out);
	for (const Effect& effect : ability.effects) {
		if (effect.kind == EffectKind::SelfDestruct && !isParty(actor) && !combatants_[actor].gone && combatants_[actor].alive()) {
			Event& e = emit(out, EventType::Message);
			e.actor = actor;
			e.message = "battle.self_destruct";
			e.params = {combatants_[actor].defId};
			leaveBattle(actor, -1, effect.rewards, out);
			break;
		}
	}
}

// A piece the party member wears that has a use, or null.
const EquipmentDef* Battle::wornPiece(int actor, const std::string& id) const {
	if (actor < 0 || actor >= static_cast<int>(record_.setup.party.size()) || combatants_[actor].aboard) {
		return nullptr;
	}
	for (const auto& [slot, worn] : record_.setup.party[static_cast<std::size_t>(actor)].equipment) {
		const EquipmentDef* piece = db_->equipmentPiece(worn);
		if (worn == id && piece && !piece->use.empty()) {
			return piece;
		}
	}
	return nullptr;
}

// An unworn piece in the bag whose data lets any member who can use items cast it, or null.
const EquipmentDef* Battle::bagPiece(int actor, const std::string& id) const {
	if (!isParty(actor) || actor == vehicleIndex_ || combatants_[actor].aboard) {
		return nullptr;
	}
	const auto held = record_.setup.pieces.find(id);
	const EquipmentDef* piece = db_->equipmentPiece(id);
	return held != record_.setup.pieces.end() && held->second > 0 && piece && piece->useFromBag && !piece->use.empty() ? piece : nullptr;
}

// A piece's own ability: worn, cast with the wearer's stats; from the bag, at the item rule's flat power. Nothing is
// paid and the piece stays where it is (P12).
void Battle::useWornPiece(int actor, const Command& command, const EquipmentDef& piece, bool fromBag, std::vector<Event>& out) {
	const AbilityDef* ability = db_->ability(piece.use);
	if (!ability) {
		cancel(actor, command, CancelReason::NoItem, "", out);
		return;
	}
	const std::vector<int> targets = executionTargets(actor, ability->targeting, ability->effects, command.target, out);
	if (targets.empty()) {
		cancel(actor, command, CancelReason::NoTarget, "", out);
		return;
	}
	Event& e = emit(out, EventType::ActionStarted);
	e.actor = actor;
	e.ability = piece.id;
	e.command = CommandKind::Item;
	e.target = targets.size() == 1 ? targets.front() : -1;
	Attacker attacker = attackerOf(actor);
	attacker.singleTarget = ability->targeting == Targeting::OneEnemy || ability->targeting == Targeting::RandomEnemy;
	attacker.groupTarget = ability->targeting == Targeting::EnemyGroup || ability->targeting == Targeting::AllEnemies;
	attacker.technique = ability->kind == AbilityKind::Technique;
	attacker.changeAmount = attacker.technique ? attacker.mental : attacker.strength;
	if (ability->attackStat) {
		attacker.strength = attacker.mental = attacker.changeAmount = combatants_[actor].effective(*ability->attackStat);
	}
	if (fromBag) {
		Attacker flat;
		flat.groupTarget = attacker.groupTarget;
		applyEffects(actor, flat, ability->id, ability->effects, targets, true, out);
		return;
	}
	applyEffects(actor, attacker, ability->id, ability->effects, targets, false, out);
}

void Battle::useItem(int actor, const Command& command, std::vector<Event>& out) {
	if (const EquipmentDef* piece = wornPiece(actor, command.id)) {
		useWornPiece(actor, command, *piece, false, out);
		return;
	}
	if (const EquipmentDef* piece = bagPiece(actor, command.id)) {
		useWornPiece(actor, command, *piece, true, out);
		return;
	}
	const ItemDef* item = db_->item(command.id);
	const auto stock = inventory_.find(command.id);
	if (!item || stock == inventory_.end() || stock->second <= 0) {
		cancel(actor, command, CancelReason::NoItem, "", out);
		return;
	}
	const std::vector<int> targets = executionTargets(actor, item->targeting, item->effects, command.target, out);
	if (targets.empty()) {
		cancel(actor, command, CancelReason::NoTarget, "", out);
		return;
	}
	Event& e = emit(out, EventType::ActionStarted);
	e.actor = actor;
	e.ability = item->id;
	e.command = CommandKind::Item;
	e.target = targets.size() == 1 ? targets.front() : -1;
	// An item that changed nothing stays in the bag, except on a fallen ally it cannot revive: that one is used up.
	const bool wasted = item->targeting == Targeting::OneAlly && !combatants_[targets.front()].alive();
	Attacker flat;
	flat.groupTarget = item->targeting == Targeting::EnemyGroup || item->targeting == Targeting::AllEnemies;
	if (!applyEffects(actor, flat, item->id, item->effects, targets, true, out) && !wasted) {
		Event& kept = emit(out, EventType::ItemHadNoEffect);
		kept.actor = actor;
		kept.ability = item->id;
		return;
	}
	if (!item->consumed) {
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
	bool enemyCanAct = false;
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (!combatants_[i].alive() || combatants_[i].aboard) {
			continue;
		}
		const int agility = combatants_[i].effective(Stat::Agility);
		if (isParty(i)) {
			partyAgility = std::max(partyAgility, agility);
		} else {
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
	e.succeeded = !enemyCanAct || formulas::chanceScore(rng_.range(0, formulas::kChanceRollMax), partyAgility, record_.setup.escapeRating) > formulas::kEscapeFloor;
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
			if (missed || t.gone || (effect.kind != EffectKind::Revive && !t.alive())) {
				break;
			}
			if (!effect.reaches(t.kind)) {
				continue;
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
				const int attack = flatPower ? effect.power : (attacker.combo ? attacker.comboAttack : attacker.strength);
				int bonus = flatPower ? 0 : (attacker.combo ? attacker.comboBonus : effect.power);
				if (critical) {
					bonus += formulas::criticalBonus(attack);
					Event& e = emit(out, EventType::Critical);
					e.actor = user;
					e.target = target;
					e.ability = source;
				}
				const int divisor = db_->battleRules.affinityDivisor;
				const int affinity = attacker.element != Element::None ? t.affinity(attacker.element) : (t.defending ? formulas::kDefendedAffinity * divisor / 100 : divisor);
				const int damage = formulas::damage(attack, t.effective(Stat::Defense), bonus, affinity, rollSpread(), divisor);
				const Effectiveness effectiveness = attacker.element != Element::None ? effectivenessOf(affinity, divisor) : Effectiveness::Normal;
				changed = dealDamage(user, target, damage, attacker.element, effectiveness, out) > 0 || changed;
				noteHit(user, target, attacker, flatPower);
				break;
			}
			case EffectKind::ElementalDamage: {
				const int affinity = t.affinity(effect.element);
				const int attack = flatPower ? effect.power : (attacker.combo ? attacker.comboAttack : attacker.mental);
				const int bonus = flatPower ? 0 : (attacker.combo ? attacker.comboBonus : effect.power);
				const BattleRules& rules = db_->battleRules;
				const int guard = t.effective(rules.magicDefense ? Stat::MagicDefense : Stat::Mental);
				const int damage = formulas::damage(attack, guard, bonus, affinity, rollSpread(), rules.affinityDivisor);
				changed = dealDamage(user, target, damage, effect.element, effectivenessOf(affinity, rules.affinityDivisor), out) > 0 || changed;
				noteHit(user, target, attacker, flatPower);
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
				if (resistance >= 100 || formulas::statusScore(rng_.range(0, formulas::kChanceRollMax), attacker.mental, t.effective(Stat::Mental), resistance) <= effect.threshold) {
					Event& e = emit(out, EventType::StatusResisted);
					e.actor = user;
					e.target = target;
					e.status = effect.status;
					e.effectiveness = resistance >= 100 ? Effectiveness::Immune : Effectiveness::Normal;
					e.message = statusMessageKey(effect.status, resistance >= 100 ? "immune" : "resisted");
					break;
				}
				const int duration = t.side == Side::Enemy && def->enemyDuration > 0 ? def->enemyDuration : def->duration;
				int remaining = duration;
				if (had && def->stacking == StatusStacking::Extend) {
					for (ActiveStatus& active : t.statuses) {
						if (active.id == effect.status && active.remaining > 0 && duration > 0) {
							active.remaining = std::min(99, active.remaining + duration);
						}
						if (active.id == effect.status) {
							remaining = active.remaining;
						}
					}
				} else {
					removeStatus(target, effect.status);
					t.statuses.push_back({effect.status, duration});
					syncPins(target);
				}
				Event& e = emit(out, EventType::StatusApplied);
				e.actor = user;
				e.target = target;
				e.status = effect.status;
				e.duration = remaining;
				e.message = statusMessageKey(effect.status, "applied");
				for (const std::string& removed : def->removes) {
					if (t.hasStatus(removed)) {
						emitStatusRemoved(user, target, removed, StatusEnd::Cured, out);
					}
				}
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
				// Lowering a foe's stat is a secondary effect and rolls like a status; raising always lands.
				const bool rolls = !effect.raises && user >= 0 && combatants_[user].side != t.side;
				if (rolls && formulas::statusScore(rng_.range(0, formulas::kChanceRollMax), attacker.mental, t.effective(Stat::Mental), 0) <= effect.threshold) {
					Event& e = emit(out, EventType::StatusResisted);
					e.actor = user;
					e.target = target;
					e.stat = effect.stat;
					e.message = "battle.stat_resisted";
					break;
				}
				const int amount = flatPower || effect.power > 0 ? effect.power : attacker.changeAmount;
				const int base = t.base(effect.stat);
				const int floor = effect.stat == Stat::Agility || effect.stat == Stat::Dexterity ? 1 : 0;
				const int value = effect.raises ? base + amount : std::max(floor, base - amount);
				t.setModifier(effect.stat, value - base);
				Event& e = emit(out, EventType::StatModified);
				e.actor = user;
				e.target = target;
				e.stat = effect.stat;
				e.amount = value - base;
				changed = true;
				break;
			}
			case EffectKind::Travel:
			case EffectKind::Teach:
				break;
			case EffectKind::InstantDefeat: {
				// With an element the roll's factor is the target's affinity for it, as the original's death effect reads it
				// (original-battle-rules.md, section 6); an affinity of 0 is immunity.
				const int elementAffinity = effect.element == Element::None ? 1 : t.affinity(effect.element);
				const bool immune = !t.vulnerable || elementAffinity <= 0;
				const int roll = immune ? 0 : rng_.range(0, formulas::kChanceRollMax);
				const int score = effect.element == Element::None ? formulas::statusScore(roll, attacker.mental, t.effective(Stat::Mental), 0)
					: formulas::elementScore(roll, attacker.mental, t.effective(Stat::Mental), elementAffinity, db_->battleRules.affinityDivisor);
				if (immune || score <= effect.threshold) {
					Event& e = emit(out, EventType::Message);
					e.actor = user;
					e.target = target;
					e.effectiveness = immune ? Effectiveness::Immune : Effectiveness::Normal;
					e.message = immune ? "battle.instant_defeat_immune" : "battle.instant_defeat_resisted";
					e.params = {t.defId};
					break;
				}
				Event& e = emit(out, EventType::Message);
				e.actor = user;
				e.target = target;
				e.message = "battle.instant_defeat";
				e.params = {t.defId};
				t.hp = 0;
				defeat(target, out);
				changed = true;
				break;
			}
			case EffectKind::RestoreTp: {
				const int restored = std::min(effect.power, t.stats.maxTp - t.tp);
				t.tp += restored;
				Event& e = emit(out, EventType::Healed);
				e.actor = user;
				e.target = target;
				e.amount = restored;
				e.resource = ResourceKind::Tp;
				changed = changed || restored > 0;
				break;
			}
			case EffectKind::StripModifiers: {
				const std::vector<ActiveModifier> modifiers = t.modifiers;
				t.modifiers.clear();
				for (const ActiveModifier& modifier : modifiers) {
					Event& e = emit(out, EventType::StatModified);
					e.actor = user;
					e.target = target;
					e.stat = modifier.stat;
					e.amount = 0;
					changed = true;
				}
				break;
			}
			case EffectKind::Summon: {
				const EnemyDef* self = db_->enemy(t.defId);
				const EnemyDef* summoned = self ? db_->enemy(self->summons) : nullptr;
				const std::vector<int> free = freeSlotsBeside(target);
				if (!summoned || free.empty()) {
					break;
				}
				const int position = free.size() == 1 ? free.front() : free[static_cast<std::size_t>(rng_.range(0, 1))];
				addEnemy(*summoned, "", position, user, out);
				changed = true;
				break;
			}
			case EffectKind::Merge: {
				const EnemyDef* self = isParty(target) ? nullptr : db_->enemy(t.defId);
				const EnemyDef* merged = self ? db_->enemy(self->mergesInto) : nullptr;
				const std::string partnerId = self && !self->mergesWith.empty() ? self->mergesWith : t.defId;
				int partner = -1;
				for (int i = partyCount_; i < static_cast<int>(combatants_.size()) && partner < 0; ++i) {
					if (i != target && combatants_[i].alive() && combatants_[i].defId == partnerId) {
						partner = i;
					}
				}
				if (!merged || partner < 0) {
					break;
				}
				leaveBattle(partner, target, false, out);
				transform(target, *merged, out);
				changed = true;
				break;
			}
			case EffectKind::Transform: {
				const EnemyDef* self = isParty(target) ? nullptr : db_->enemy(t.defId);
				if (const EnemyDef* next = self ? db_->enemy(self->transformsInto) : nullptr) {
					transform(target, *next, out);
					changed = true;
				}
				break;
			}
			case EffectKind::Split: {
				const EnemyDef* self = isParty(target) ? nullptr : db_->enemy(t.defId);
				if (const EnemyDef* part = self ? db_->enemy(self->splitsInto) : nullptr) {
					split(target, *part, effect.count, out);
					changed = true;
				}
				break;
			}
			case EffectKind::SelfDestruct:
				break;
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
	for (const ActiveStatus& active : t.statuses) {
		const StatusDef* def = db_->status(active.id);
		if (def && def->damageCap > 0) {
			amount = std::min(amount, def->damageCap);
		}
	}
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
	const EnemyDef* enemyDef = isParty(target) ? nullptr : db_->enemy(t.defId);
	if (!t.alive()) {
		defeat(target, out);
	} else if (enemyDef && enemyDef->nextFormAt > 0 && dealt > 0 && t.hp * 100 <= t.stats.maxHp * enemyDef->nextFormAt && db_->enemy(enemyDef->nextForm)) {
		transform(target, *db_->enemy(enemyDef->nextForm), out);
	} else if (dealt > 0) {
		const std::vector<ActiveStatus> statuses = t.statuses;
		for (const ActiveStatus& active : statuses) {
			const StatusDef* def = db_->status(active.id);
			if (def && def->wakesOnDamage) {
				emitStatusRemoved(-1, target, active.id, StatusEnd::WokeOnDamage, out);
			}
		}
	}
	return dealt;
}

// Party damage that lands on an enemy marks what hit it, for the AI's overrides.
void Battle::noteHit(int user, int target, const Attacker& attacker, bool item) {
	if (user < 0 || !isParty(user) || isParty(target)) {
		return;
	}
	Combatant& t = combatants_[target];
	const bool basicAttack = attacker.rollsToHit && !item;
	t.hitByAttack = t.hitByAttack || basicAttack;
	t.hitByAbility = t.hitByAbility || !basicAttack;
	t.hitByTechnique = t.hitByTechnique || attacker.technique;
	t.hitByGroup = t.hitByGroup || attacker.groupTarget;
}

// Every status the fall clears gets its own removal event first, so the stream shows each change once. Statuses that
// survive defeat and stat changes stay. A fallen vehicle takes every member aboard down with it. An enemy with
// another form takes it instead of falling; in a battle that replaces the fallen, a fresh one takes the fallen's place.
void Battle::defeat(int target, std::vector<Event>& out) {
	const EnemyDef* enemyDef = isParty(target) ? nullptr : db_->enemy(combatants_[target].defId);
	if (enemyDef && !enemyDef->nextForm.empty()) {
		if (const EnemyDef* next = db_->enemy(enemyDef->nextForm)) {
			transform(target, *next, out);
			return;
		}
	}
	const std::vector<ActiveStatus> statuses = combatants_[target].statuses;
	Combatant& t = combatants_[target];
	for (const ActiveStatus& active : statuses) {
		const StatusDef* def = db_->status(active.id);
		if (active.id != kDefeatedStatus && !(def && def->survivesDefeat)) {
			emitStatusRemoved(-1, target, active.id, StatusEnd::Defeated, out);
		}
	}
	t.hp = 0;
	t.statuses.push_back({kDefeatedStatus, 0});
	t.defending = false;
	if (isParty(target) && !t.aboard && autoActive_) {
		autoMemberFell_ = true;
	}
	Event& e = emit(out, EventType::CombatantDefeated);
	e.target = target;
	const Encounter* encounter = enemyDef ? db_->encounter(record_.setup.enemyGroup) : nullptr;
	if (encounter && encounter->replaceFallen) {
		addEnemy(*enemyDef, combatants_[target].group, combatants_[target].position, -1, out);
	}
	if (target == vehicleIndex_) {
		for (int i = 0; i < vehicleIndex_; ++i) {
			if (combatants_[i].alive()) {
				defeat(i, out);
			}
		}
	}
}

void Battle::removeStatus(int target, const std::string& statusId) {
	std::vector<ActiveStatus>& statuses = combatants_[target].statuses;
	statuses.erase(std::remove_if(statuses.begin(), statuses.end(), [&](const ActiveStatus& s) {
		return s.id == statusId;
	}), statuses.end());
	syncPins(target);
}

void Battle::syncPins(int target) {
	Combatant& c = combatants_[target];
	c.pinned.clear();
	for (const ActiveStatus& active : c.statuses) {
		if (const StatusDef* def = db_->status(active.id)) {
			for (Stat stat : def->statsToOne) {
				if (std::find(c.pinned.begin(), c.pinned.end(), stat) == c.pinned.end()) {
					c.pinned.push_back(stat);
				}
			}
		}
	}
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
