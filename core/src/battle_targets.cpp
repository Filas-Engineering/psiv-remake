#include "rules/battle.h"

#include <algorithm>

namespace rules {

namespace {

bool plainDamage(const std::vector<Effect>& effects) {
	return !effects.empty() && (effects.front().kind == EffectKind::PhysicalDamage || effects.front().kind == EffectKind::ElementalDamage);
}

}

std::vector<int> Battle::livingFoes(int actor) const {
	std::vector<int> result;
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (combatants_[i].side != combatants_[actor].side && combatants_[i].alive() && !combatants_[i].aboard) {
			result.push_back(i);
		}
	}
	return result;
}

std::vector<int> Battle::allies(int actor) const {
	std::vector<int> result;
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (combatants_[i].side == combatants_[actor].side && !combatants_[i].aboard) {
			result.push_back(i);
		}
	}
	return result;
}

bool Battle::wouldChange(int target, const std::vector<Effect>& effects) const {
	const Combatant& t = combatants_[target];
	if (t.gone) {
		return false;
	}
	for (const Effect& effect : effects) {
		if (!effect.reaches(t.kind) || (effect.kind != EffectKind::Revive && !t.alive())) {
			continue;
		}
		switch (effect.kind) {
		case EffectKind::Heal:
			if (t.hp < t.stats.maxHp) {
				return true;
			}
			break;
		case EffectKind::Revive:
			if (!t.alive()) {
				return true;
			}
			break;
		case EffectKind::CureStatus:
			if (t.hasStatus(effect.status)) {
				return true;
			}
			break;
		case EffectKind::RestoreTp:
			if (t.tp < t.stats.maxTp) {
				return true;
			}
			break;
		case EffectKind::ApplyStatus: {
			const StatusDef* def = db_->status(effect.status);
			const bool elementImmune = effect.element != Element::None && t.affinity(effect.element) <= 0;
			if (def && t.resistance(effect.status) < 100 && !elementImmune && !(def->stacking == StatusStacking::Keep && t.hasStatus(effect.status))) {
				return true;
			}
			break;
		}
		case EffectKind::PhysicalDamage:
		case EffectKind::ElementalDamage:
		case EffectKind::ModifyStat:
		case EffectKind::Travel:
		case EffectKind::InstantDefeat:
		case EffectKind::StripModifiers:
		case EffectKind::Summon:
		case EffectKind::Merge:
		case EffectKind::Teach:
		case EffectKind::Transform:
		case EffectKind::Split:
		case EffectKind::SelfDestruct:
		case EffectKind::RemoveBeside:
		case EffectKind::GrantAffinity:
			return true;
		case EffectKind::SetHpPercent:
			if (t.hp != std::max(1, t.stats.maxHp * effect.power / 100)) {
				return true;
			}
			break;
		case EffectKind::ResetStat:
			if (std::any_of(t.modifiers.begin(), t.modifiers.end(), [&](const ActiveModifier& m) { return m.stat == effect.stat; })) {
				return true;
			}
			break;
		}
	}
	return false;
}

bool Battle::supports(const std::vector<Effect>& effects) {
	if (effects.empty()) {
		return false;
	}
	for (const Effect& effect : effects) {
		const bool helps = effect.kind == EffectKind::Heal || effect.kind == EffectKind::Revive || effect.kind == EffectKind::CureStatus
			|| effect.kind == EffectKind::RestoreTp || (effect.kind == EffectKind::ModifyStat && effect.raises);
		if (!helps) {
			return false;
		}
	}
	return true;
}

// The requested foe while it stands, otherwise the first living foe.
int Battle::primaryFoe(int user, int requested) const {
	const std::vector<int> foes = livingFoes(user);
	if (foes.empty()) {
		return -1;
	}
	return std::find(foes.begin(), foes.end(), requested) != foes.end() ? requested : foes.front();
}

// Party members who are not defeated, in party order, weighted count, count - 1, ... 1 (BAT-090), or by rules.json's
// target_weights for that many members.
int Battle::drawFoeByPartyOrder(int actor) {
	const std::vector<int> foes = livingFoes(actor);
	if (foes.empty()) {
		return -1;
	}
	const int count = static_cast<int>(foes.size());
	const auto weights = db_->battleRules.targetWeights.find(count);
	if (weights != db_->battleRules.targetWeights.end()) {
		int roll = rng_.range(0, 255);
		for (int k = 0; k < count; ++k) {
			roll -= weights->second[static_cast<std::size_t>(k)];
			if (roll < 0) {
				return foes[static_cast<std::size_t>(k)];
			}
		}
		return foes.back();
	}
	int roll = rng_.range(0, count * (count + 1) / 2 - 1);
	for (int k = 0; k < count; ++k) {
		roll -= count - k;
		if (roll < 0) {
			return foes[static_cast<std::size_t>(k)];
		}
	}
	return foes.back();
}

// A party action whose foe is gone (BAT-015): plain damage turns to the living foe missing the most hit points,
// anything else to the one missing the fewest. Each later tie takes the place on a coin flip.
int Battle::retargetFoe(int user, const std::vector<Effect>& effects) {
	const bool most = plainDamage(effects);
	int best = -1;
	int bestMissing = 0;
	for (int foe : livingFoes(user)) {
		const int missing = combatants_[foe].stats.maxHp - combatants_[foe].hp;
		if (best < 0 || (most ? missing > bestMissing : missing < bestMissing)) {
			best = foe;
			bestMissing = missing;
		} else if (missing == bestMissing && rng_.range(0, 1) == 1) {
			best = foe;
		}
	}
	return best;
}

// A one-ally action in a macro round (BAT-053): a revive takes the first fallen member, a cure the first member
// with a status it cures (or the actor), a heal the member missing the most hit points (ties to the later member).
// Each passes by members whose kind the effect does not reach. Anything else keeps the command's target. With
// rules.json's macro_ally_pick would_change, every pick also passes by members the effect would not change, and keeps
// the command's target when nobody is left.
int Battle::macroAllyTarget(int actor, const std::vector<Effect>& effects) const {
	const int requested = commands_[actor].target;
	if (effects.empty()) {
		return requested;
	}
	const bool byChange = db_->battleRules.macroAllyWouldChange;
	const std::vector<int> party = allies(actor);
	switch (effects.front().kind) {
	case EffectKind::Revive:
		for (int ally : party) {
			if (!combatants_[ally].alive() && wouldChange(ally, effects)) {
				return ally;
			}
		}
		if (byChange) {
			return requested;
		}
		break;
	case EffectKind::CureStatus:
		for (int ally : party) {
			for (const Effect& effect : effects) {
				if (combatants_[ally].alive() && effect.kind == EffectKind::CureStatus && effect.reaches(combatants_[ally].kind) && combatants_[ally].hasStatus(effect.status)) {
					return ally;
				}
			}
		}
		return byChange ? requested : actor;
	case EffectKind::Heal:
		break;
	// The original's pick for a one-ally raise never moves off the first member in line, whatever that member's state.
	case EffectKind::ModifyStat:
		for (int ally : party) {
			if (byChange ? wouldChange(ally, effects) : effects.front().reaches(combatants_[ally].kind)) {
				return ally;
			}
		}
		return requested;
	case EffectKind::PhysicalDamage:
	case EffectKind::ElementalDamage:
	case EffectKind::ApplyStatus:
	case EffectKind::Travel:
	case EffectKind::InstantDefeat:
	case EffectKind::RestoreTp:
	case EffectKind::StripModifiers:
	case EffectKind::Summon:
	case EffectKind::Merge:
	case EffectKind::Teach:
	case EffectKind::Transform:
	case EffectKind::Split:
	case EffectKind::SelfDestruct:
	case EffectKind::RemoveBeside:
	case EffectKind::SetHpPercent:
	case EffectKind::GrantAffinity:
	case EffectKind::ResetStat:
		return requested;
	}
	int best = -1;
	for (int ally : party) {
		const Combatant& c = combatants_[ally];
		if (c.alive() && effects.front().reaches(c.kind) && (!byChange || wouldChange(ally, effects)) &&
			(best < 0 || c.stats.maxHp - c.hp >= combatants_[best].stats.maxHp - combatants_[best].hp)) {
			best = ally;
		}
	}
	return best < 0 ? requested : best;
}

// The single target an action aims at when it runs, after a Retargeted event if it changed. A single foe that is
// gone is replaced (an enemy draws again by party order); a party member's -1 aims at the first enemy. In a macro
// round foes are picked afresh every time and one-ally actions use the macro's picks. A single ally is never replaced.
int Battle::aimAt(int user, Targeting targeting, const std::vector<Effect>& effects, int requested, std::vector<Event>& out) {
	const bool macroTurn = std::find(forcedOrder_.begin(), forcedOrder_.end(), user) != forcedOrder_.end();
	int aim = requested;
	switch (targeting) {
	case Targeting::OneEnemy:
	case Targeting::EnemyGroup: {
		if (aim < 0 && isParty(user)) {
			aim = partyCount_;
		}
		const std::vector<int> foes = livingFoes(user);
		if (macroTurn || std::find(foes.begin(), foes.end(), aim) == foes.end()) {
			aim = isParty(user) ? retargetFoe(user, effects) : drawFoeByPartyOrder(user);
		}
		break;
	}
	case Targeting::OneAlly:
	case Targeting::OneFallenAlly:
		if (macroTurn) {
			aim = macroAllyTarget(user, effects);
		}
		break;
	case Targeting::AllEnemies:
	case Targeting::AllAllies:
	case Targeting::Self:
	case Targeting::RandomEnemy:
		return requested;
	}
	if (aim >= 0 && aim != requested && requested >= 0 && requested < static_cast<int>(combatants_.size())) {
		Event& e = emit(out, EventType::Retargeted);
		e.actor = user;
		e.previousTarget = requested;
		e.target = aim;
	}
	return aim;
}

// A forced command keeps its random target, whichever side it is on, while that target stands.
std::vector<int> Battle::executionTargets(int user, Targeting targeting, const std::vector<Effect>& effects, int requested, std::vector<Event>& out) {
	if (forced_[user] && requested >= 0 && combatants_[requested].alive()) {
		return {requested};
	}
	const std::vector<int> targets = resolveTargets(user, targeting, aimAt(user, targeting, effects, requested, out), effectsRevive(effects), effectsTakeLiving(effects), &effects);
	if (targeting == Targeting::RandomEnemy && !targets.empty()) {
		return {targets[rng_.range(0, static_cast<int>(targets.size()) - 1)]};
	}
	return targets;
}

// A single ally target stays as named, fallen or not; -1 is the user. A fallen-ally target that is back up has none.
bool Battle::canAffect(int target, const std::vector<Effect>& effects) const {
	const Combatant& t = combatants_[target];
	if (t.gone) {
		return false;
	}
	return std::any_of(effects.begin(), effects.end(), [&](const Effect& e) {
		if (!e.reaches(t.kind) || (e.kind == EffectKind::Revive) == t.alive()) {
			return false;
		}
		return e.kind != EffectKind::ApplyStatus || t.resistance(e.status) < 100;
	});
}

std::vector<int> Battle::resolveTargets(int user, Targeting targeting, int requested, bool revives, bool living, const std::vector<Effect>* effects) const {
	const bool validIndex = requested >= 0 && requested < static_cast<int>(combatants_.size());
	const bool requestedAlly = validIndex && combatants_[requested].side == combatants_[user].side && !combatants_[requested].aboard;
	std::vector<int> result;
	switch (targeting) {
	case Targeting::Self:
		result.push_back(user);
		break;
	case Targeting::OneEnemy:
	case Targeting::EnemyGroup: {
		const int primary = primaryFoe(user, requested);
		if (primary < 0) {
			break;
		}
		if (targeting == Targeting::OneEnemy) {
			result.push_back(primary);
			break;
		}
		for (int foe : livingFoes(user)) {
			if (combatants_[foe].group == combatants_[primary].group) {
				result.push_back(foe);
			}
		}
		break;
	}
	case Targeting::AllEnemies:
	case Targeting::RandomEnemy:
		result = livingFoes(user);
		break;
	case Targeting::OneAlly:
		if (!requestedAlly && effects && !canAffect(user, *effects)) {
			for (int ally : allies(user)) {
				if (canAffect(ally, *effects)) {
					result.push_back(ally);
					break;
				}
			}
			break;
		}
		result.push_back(requestedAlly ? requested : user);
		break;
	case Targeting::OneFallenAlly:
		if (requestedAlly) {
			if (!combatants_[requested].alive() || living) {
				result.push_back(requested);
			}
			break;
		}
		for (int ally : allies(user)) {
			if (!combatants_[ally].alive() && (!effects || canAffect(ally, *effects))) {
				result.push_back(ally);
				break;
			}
		}
		break;
	case Targeting::AllAllies:
		for (int ally : allies(user)) {
			if (revives || combatants_[ally].alive()) {
				result.push_back(ally);
			}
		}
		break;
	}
	return result;
}

}
