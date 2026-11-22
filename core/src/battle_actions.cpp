#include "rules/battle.h"

#include "rules/formulas.h"
#include "rules/text.h"

#include <algorithm>

namespace rules {

namespace {

Effectiveness effectivenessOf(int affinity, int normal) {
	if (affinity <= 0) {
		return Effectiveness::Immune;
	}
	if (affinity > normal) {
		return Effectiveness::Weak;
	}
	return affinity < normal ? Effectiveness::Resisted : Effectiveness::Normal;
}

}

int Battle::elementAffinity(const Combatant& target, const Element& element) const {
	const int affinity = target.affinity(element);
	const BattleRules& rules = db_->battleRules;
	return target.defending && rules.defends(element) ? std::min(affinity, rules.defendedAffinity()) : affinity;
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
		if (db_->battleRules.defendFromTurn) {
			combatants_[actor].defending = true;
		}
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
	Attacker attacker = abilityAttacker(actor, ability, kind);
	attacker.rollsToHit = ability.kind == AbilityKind::Attack;
	applyEffects(actor, attacker, ability.id, ability.effects, targets, false, out);
	for (const Effect& effect : ability.effects) {
		if (effect.kind == EffectKind::RemoveBeside && !isParty(actor) && !combatants_[actor].gone && combatants_[actor].alive()) {
			removeBeside(actor, effect.rewards, out);
		}
	}
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

// The enemies standing in the formation places right beside the user leave the battle; a free place beside it stops
// the reach on that side.
void Battle::removeBeside(int actor, bool pays, std::vector<Event>& out) {
	const int position = combatants_[actor].position;
	for (int place : {position - 1, position + 1}) {
		for (int i = partyCount_; i < static_cast<int>(combatants_.size()); ++i) {
			if (i == actor || !combatants_[i].alive() || combatants_[i].gone || combatants_[i].position != place) {
				continue;
			}
			Event& e = emit(out, EventType::Message);
			e.actor = actor;
			e.target = i;
			e.message = "battle.removed_beside";
			e.params = {combatants_[i].defId};
			leaveBattle(i, -1, pays, out);
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
	Attacker attacker = abilityAttacker(actor, *ability, CommandKind::Item);
	if (piece.usePower) {
		attacker.strength = attacker.mental = attacker.changeAmount = *piece.usePower;
		attacker.rollUser.reset();
	} else if (fromBag) {
		Attacker flat;
		flat.groupTarget = attacker.groupTarget;
		flat.variance = attacker.variance;
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
	const bool singleAlly = item->targeting == Targeting::OneAlly || item->targeting == Targeting::OneFallenAlly;
	const bool namedAlly = singleAlly && command.target >= 0 && command.target < static_cast<int>(combatants_.size()) &&
		combatants_[command.target].side == combatants_[actor].side && !combatants_[command.target].aboard;
	std::vector<int> targets = executionTargets(actor, item->targeting, item->effects, command.target, out);
	const bool noReachSpent = namedAlly && db_->spentOnNoReach(*item) && (targets.empty() || targets == std::vector<int>{command.target}) &&
		!canAffect(command.target, item->effects);
	if (noReachSpent) {
		targets = {command.target};
	}
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
	const bool applied = !noReachSpent && applyEffects(actor, flat, item->id, item->effects, targets, true, out);
	if (!applied && (!wasted || noReachSpent)) {
		Event& kept = emit(out, EventType::ItemHadNoEffect);
		kept.actor = actor;
		kept.ability = item->id;
		if (!item->consumeWhenNoEffect.value_or(db_->battleRules.consumeWhenNoEffect)) {
			return;
		}
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

Battle::Attacker Battle::abilityAttacker(int actor, const AbilityDef& ability, CommandKind kind) const {
	Attacker attacker = attackerOf(actor);
	if (kind == CommandKind::Attack) {
		attacker.strength = combatants_[actor].effective(Stat::Attack);
		attacker.element = combatants_[actor].attackElement;
	}
	attacker.singleTarget = ability.targeting == Targeting::OneEnemy || ability.targeting == Targeting::RandomEnemy;
	attacker.technique = ability.kind == AbilityKind::Technique;
	attacker.groupTarget = ability.targeting == Targeting::EnemyGroup || ability.targeting == Targeting::AllEnemies;
	attacker.changeAmount = attacker.technique ? attacker.mental : attacker.strength;
	if (ability.attackStat) {
		attacker.strength = attacker.mental = attacker.changeAmount = combatants_[actor].effective(*ability.attackStat);
		if (*ability.attackStat == Stat::Attack) {
			attacker.rollUser = combatants_[actor].effective(Stat::Strength);
		}
	}
	attacker.variance = ability.variance;
	return attacker;
}

int Battle::rollSpread() {
	int spread = formulas::kSpreadBase;
	for (int i = 0; i < formulas::kSpreadRolls; ++i) {
		spread += rng_.range(0, formulas::kSpreadRollMax);
	}
	return spread;
}

int Battle::rollDamage(int attack, int defense, int bonus, int affinity, const Attacker& attacker) {
	const BattleRules& rules = db_->battleRules;
	const std::optional<DamageVariance>& variance = attacker.variance ? attacker.variance : rules.damageVariance;
	if (!variance) {
		return formulas::damage(attack, defense, bonus, affinity, rollSpread(), rules.affinityDivisor, rules.damageWraparound, rules.immuneMinDamage);
	}
	if (affinity <= 0 && rules.immuneMinDamage) {
		return 1;
	}
	int offset = 0;
	int scale = 100;
	if (variance->percent > 0 && variance->shape == VarianceShape::Uniform) {
		offset = rng_.range(-variance->percent, variance->percent);
	} else if (variance->percent > 0) {
		// The original's spread around its mean, -56 to 56, scaled so that its ends reach the percent.
		const int half = formulas::kSpreadMean - formulas::kSpreadBase;
		offset = variance->percent * (rollSpread() - formulas::kSpreadMean);
		scale = 100 * half;
	}
	return formulas::variedDamage(attack, defense, bonus, affinity, scale + offset, scale, rules.affinityDivisor);
}

int Battle::rollEffect(int user, const Attacker& attacker, const Effect& effect, const Combatant& target, int affinity, int resistance) {
	const int roll = rng_.range(0, formulas::kChanceRollMax);
	const int userSide = effect.userPower ? *effect.userPower : effect.userStat && user >= 0 ? combatants_[user].effective(*effect.userStat) : attacker.rollUser.value_or(attacker.mental);
	const int targetSide = target.effective(effect.targetStat.value_or(Stat::Mental));
	return formulas::effectScore(roll, userSide, targetSide, affinity, db_->battleRules.affinityDivisor, resistance);
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
	const bool openingRound = db_->battleRules.escapePartyFirstRound && round_ == 1 && record_.setup.opening == Opening::PartyFirst;
	e.succeeded = !enemyCanAct || openingRound || formulas::chanceScore(rng_.range(0, formulas::kChanceRollMax), partyAgility, record_.setup.escapeRating) > formulas::kEscapeFloor;
	if (e.succeeded) {
		outcome_ = Outcome::Escaped;
	}
}

bool Battle::applyEffects(int user, const Attacker& attacker, const std::string& source, const std::vector<Effect>& effects, const std::vector<int>& targets, bool flatPower, std::vector<Event>& out) {
	bool changed = false;
	for (int target : targets) {
		bool missed = false;
		// A revive with `hp: 0` stands the target at 0 for the rest of the action; its event's amount is settled at the end.
		std::optional<std::size_t> standingAtZero;
		// A fallen target's revives come first, so the other effects reach it once it stands, whatever the listed order.
		std::vector<const Effect*> ordered;
		for (const Effect& effect : effects) {
			if (effect.kind == EffectKind::Revive || combatants_[target].alive()) {
				ordered.push_back(&effect);
			}
		}
		if (!combatants_[target].alive()) {
			for (const Effect& effect : effects) {
				if (effect.kind != EffectKind::Revive) {
					ordered.push_back(&effect);
				}
			}
		}
		for (const Effect* listed : ordered) {
			const Effect& effect = *listed;
			Combatant& t = combatants_[target];
			if (missed || t.gone || (effect.kind != EffectKind::Revive && !t.alive() && !(standingAtZero && t.hp == 0))) {
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
				const int attack = flatPower ? std::max(0, effect.power) : (attacker.combo ? attacker.comboAttack : attacker.strength) * effect.statPermille / 1000;
				int bonus = flatPower ? effect.statValue.value_or(0) : (attacker.combo ? attacker.comboBonus : effect.power);
				if (critical) {
					bonus += db_->battleRules.criticalLowByte ? formulas::criticalBonusLowByte(attack) : formulas::criticalBonus(attack);
					Event& e = emit(out, EventType::Critical);
					e.actor = user;
					e.target = target;
					e.ability = source;
				}
				const BattleRules& rules = db_->battleRules;
				const Element weapon = effect.weaponElement && user >= 0 ? combatants_[user].attackElement : Element::None;
				const Element element = effect.weaponElement ? weapon : effect.element != Element::None ? effect.element : attacker.element;
				const int affinity = element != Element::None ? elementAffinity(t, element) : (t.defending && rules.defends(Element::None) ? rules.defendedAffinity() : rules.elementlessHitAffinity());
				const int damage = rollDamage(attack, t.effective(Stat::Defense), bonus, affinity, attacker);
				const Effectiveness effectiveness = element != Element::None ? effectivenessOf(affinity, rules.normalAffinity(element)) : Effectiveness::Normal;
				changed = dealDamage(user, target, damage, element, effectiveness, out) > 0 || changed;
				noteHit(user, target, attacker, flatPower);
				break;
			}
			case EffectKind::ElementalDamage: {
				const int affinity = elementAffinity(t, effect.element);
				const int attack = flatPower ? std::max(0, effect.power) : (attacker.combo ? attacker.comboAttack : attacker.mental) * effect.statPermille / 1000;
				const int bonus = flatPower ? effect.statValue.value_or(0) : (attacker.combo ? attacker.comboBonus : effect.power);
				const BattleRules& rules = db_->battleRules;
				const int guard = t.effective(rules.magicDefense ? Stat::MagicDefense : Stat::Mental);
				const int damage = rollDamage(attack, guard, bonus, affinity, attacker);
				changed = dealDamage(user, target, damage, effect.element, effectivenessOf(affinity, rules.normalAffinity(effect.element)), out) > 0 || changed;
				noteHit(user, target, attacker, flatPower);
				break;
			}
			case EffectKind::Heal: {
				// Only a heal that rolls draws the spread, so a flat heal leaves the random draws as they were.
				const bool rolls = !flatPower || effect.statValue;
				const int spread = rolls && static_cast<int>(combatants_.size()) > partyCount_ ? rollSpread() : formulas::kSpreadMean;
				int amount = flatPower ? effect.power : formulas::healAmount(effect.power, attacker.mental, spread);
				if (flatPower && effect.statValue) {
					amount = formulas::healAmount(*effect.statValue, effect.power, spread);
				}
				const int healed = std::min(amount, t.stats.maxHp - t.hp);
				t.hp += healed;
				Event& e = emit(out, EventType::Healed);
				e.actor = user;
				e.target = target;
				e.amount = healed;
				e.rolled = amount;
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
				const int affinity = effect.element == Element::None ? db_->battleRules.affinityDivisor : t.affinity(effect.element);
				const bool immune = resistance >= 100 || affinity <= 0;
				if (immune || (!effect.alwaysLands && rollEffect(user, attacker, effect, t, affinity, resistance) <= effect.threshold)) {
					Event& e = emit(out, EventType::StatusResisted);
					e.actor = user;
					e.target = target;
					e.status = effect.status;
					e.effectiveness = immune ? Effectiveness::Immune : Effectiveness::Normal;
					e.message = statusMessageKey(effect.status, immune ? "immune" : "resisted");
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
				const int affinity = effect.element == Element::None ? db_->battleRules.affinityDivisor : t.affinity(effect.element);
				const bool immune = rolls && affinity <= 0;
				if (immune || (rolls && !effect.alwaysLands && rollEffect(user, attacker, effect, t, affinity, 0) <= effect.threshold)) {
					Event& e = emit(out, EventType::StatusResisted);
					e.actor = user;
					e.target = target;
					e.stat = effect.stat;
					e.effectiveness = immune ? Effectiveness::Immune : Effectiveness::Normal;
					e.message = "battle.stat_resisted";
					break;
				}
				const int amount = flatPower || effect.power > 0 ? effect.power : attacker.changeAmount;
				const int base = t.base(effect.stat);
				// A lowering never raises a stat that is already below the floor (0 in the data).
				const int floor = std::min(base, effect.stat == Stat::Agility || effect.stat == Stat::Dexterity ? 1 : 0);
				const BattleRules& rules = db_->battleRules;
				const bool wraps = effect.raises && std::count(rules.statWraparound.begin(), rules.statWraparound.end(), effect.stat) > 0;
				const int value = wraps ? (base + amount) % 256 : effect.raises ? base + amount : std::max(floor, base - amount);
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
				const int affinity = effect.element == Element::None ? db_->battleRules.affinityDivisor : t.affinity(effect.element);
				const bool immune = !t.vulnerable || affinity <= 0;
				if (immune || (!effect.alwaysLands && rollEffect(user, attacker, effect, t, affinity, 0) <= effect.threshold)) {
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
				e.rolled = effect.power;
				e.resource = ResourceKind::Tp;
				changed = changed || restored > 0;
				break;
			}
			case EffectKind::StripModifiers: {
				const std::vector<ActiveModifier> modifiers = t.modifiers;
				t.modifiers.clear();
				if (effect.equipmentAffinities && isParty(target) && db_->character(t.defId) && t.affinities != t.unequippedAffinities) {
					t.affinities = t.unequippedAffinities;
					for (const auto& [element, affinity] : t.grantedAffinities) {
						t.affinities[element] = affinity;
					}
					changed = true;
				}
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
			case EffectKind::ResetStat: {
				const auto modifier = std::find_if(t.modifiers.begin(), t.modifiers.end(), [&](const ActiveModifier& m) { return m.stat == effect.stat; });
				if (modifier == t.modifiers.end()) {
					break;
				}
				t.modifiers.erase(modifier);
				Event& e = emit(out, EventType::StatModified);
				e.actor = user;
				e.target = target;
				e.stat = effect.stat;
				e.amount = 0;
				changed = true;
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
					transform(target, *next, out, self->keepsAmbush);
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
			case EffectKind::RemoveBeside:
				break;
			case EffectKind::SetHpPercent: {
				const int before = t.hp;
				t.hp = std::max(1, t.stats.maxHp * effect.power / 100);
				if (t.hp == before) {
					break;
				}
				Event& e = emit(out, t.hp > before ? EventType::Healed : EventType::DamageDealt);
				e.actor = user;
				e.target = target;
				e.amount = std::abs(t.hp - before);
				e.rolled = e.amount;
				e.hp = t.hp;
				changed = true;
				break;
			}
			case EffectKind::GrantAffinity: {
				t.affinities[effect.element] = effect.affinity;
				t.unequippedAffinities[effect.element] = effect.affinity;
				if (effect.permanent && isParty(target) && db_->character(t.defId)) {
					t.grantedAffinities[effect.element] = effect.affinity;
				}
				Event& e = emit(out, EventType::Message);
				e.actor = user;
				e.target = target;
				e.message = "battle.affinity_changed";
				e.params = {t.defId, effect.element.id};
				changed = true;
				break;
			}
			case EffectKind::Revive:
				if (!t.alive() && !standingAtZero) {
					t.hp = effect.zeroHp ? 0 : effect.flatHp > 0 ? std::min(effect.flatHp, t.stats.maxHp) : formulas::reviveHp(t.stats.maxHp, effect.power);
					removeStatus(target, kDefeatedStatus);
					if (effect.zeroHp) {
						standingAtZero = out.size();
					}
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
		if (standingAtZero && combatants_[target].hp == 0) {
			combatants_[target].hp = 1;
			out[*standingAtZero].amount = 1;
			out[*standingAtZero].hp = 1;
		}
	}
	return changed;
}

int Battle::dealDamage(int user, int target, int amount, Element element, Effectiveness effectiveness, std::vector<Event>& out) {
	Combatant& t = combatants_[target];
	const int rolled = amount;
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
	e.rolled = rolled;
	e.hp = t.hp;
	e.element = element;
	e.effectiveness = effectiveness;
	const EnemyDef* enemyDef = isParty(target) ? nullptr : db_->enemy(t.defId);
	if (!t.alive()) {
		defeat(target, out);
	} else if (enemyDef && enemyDef->nextFormAt > 0 && dealt > 0 && t.hp * 100 <= t.stats.maxHp * enemyDef->nextFormAt && db_->enemy(enemyDef->nextForm)) {
		transform(target, *db_->enemy(enemyDef->nextForm), out, enemyDef->keepsAmbush);
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
			transform(target, *next, out, enemyDef->keepsAmbush);
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
