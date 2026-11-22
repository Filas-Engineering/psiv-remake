#include "rules/battle.h"

#include <algorithm>
#include <cstddef>
#include <numeric>

namespace rules {

namespace {

// An ability fills a component it names, or one whose ability is of the same family (B3).
bool componentMatches(const Database& db, const ComboRecipe& recipe, std::size_t component, const std::string& ability, const std::string& character) {
	const bool tied = component < recipe.characters.size() && !recipe.characters[component].empty();
	if (tied && recipe.characters[component] != character) {
		return false;
	}
	if (recipe.abilities[component] == ability) {
		return true;
	}
	const AbilityDef* used = db.ability(ability);
	const AbilityDef* named = db.ability(recipe.abilities[component]);
	return used && named && !used->family.empty() && used->family == named->family;
}

// Participant k used abilities[k] and is characters[k]. An ordered recipe, and every four-member one, maps
// participant k to component k; an unordered one accepts any one-to-one mapping.
bool recipeMatches(const Database& db, const ComboRecipe& recipe, const std::vector<std::string>& abilities, const std::vector<std::string>& characters) {
	if (!recipe.orders.empty()) {
		std::vector<std::vector<int>> orders = {{}};
		for (std::size_t k = 0; k < recipe.abilities.size(); ++k) {
			orders.front().push_back(static_cast<int>(k));
		}
		orders.insert(orders.end(), recipe.orders.begin(), recipe.orders.end());
		return std::any_of(orders.begin(), orders.end(), [&](const std::vector<int>& order) {
			for (std::size_t k = 0; k < order.size(); ++k) {
				if (!componentMatches(db, recipe, static_cast<std::size_t>(order[k]), abilities[k], characters[k])) {
					return false;
				}
			}
			return true;
		});
	}
	const bool ordered = recipe.ordered || recipe.abilities.size() == static_cast<std::size_t>(kMaxComboParticipants);
	std::vector<std::size_t> assignment(recipe.abilities.size());
	std::iota(assignment.begin(), assignment.end(), std::size_t(0));
	do {
		bool all = true;
		for (std::size_t k = 0; k < assignment.size() && all; ++k) {
			all = componentMatches(db, recipe, assignment[k], abilities[k], characters[k]);
		}
		if (all) {
			return true;
		}
	} while (!ordered && std::next_permutation(assignment.begin(), assignment.end()));
	return false;
}

int damagePower(const AbilityDef& ability) {
	for (const Effect& effect : ability.effects) {
		if (effect.kind == EffectKind::PhysicalDamage || effect.kind == EffectKind::ElementalDamage) {
			return effect.power;
		}
	}
	return 0;
}

}

// At the start of each actor's turn (BAT-061): up to four queued turns from this actor, stopping at a living enemy, a
// member who cannot act, a command that is not a technique or skill, or one that cannot be paid for. An enemy that
// fell earlier in the round no longer stands between two parts. The first recipe in data order that matches the
// first turns fires, and the other participants' turns are spent on it. Returns how many queued turns the combo
// consumed, fallen enemies between its parts included, or 0 when none fires.
int Battle::tryCombo(const std::vector<int>& order, std::size_t index, std::vector<Event>& out) {
	std::vector<int> participants;
	std::vector<std::size_t> queuePlaces;
	std::vector<std::string> used;
	std::vector<std::string> characters;
	for (std::size_t k = index; k < order.size() && participants.size() < static_cast<std::size_t>(kMaxComboParticipants); ++k) {
		const int actor = order[k];
		const Command& command = commands_[actor];
		if (!isParty(actor) && !combatants_[actor].alive() && k > index) {
			continue;
		}
		if (!isParty(actor) || !combatants_[actor].alive() || command.kind != CommandKind::Ability || statusWith(actor, StatusAction::CannotAct)) {
			break;
		}
		const AbilityDef* ability = db_->ability(command.id);
		if (!ability || (ability->kind != AbilityKind::Technique && ability->kind != AbilityKind::Skill) || checkUsable(actor, *ability) != CancelReason::None) {
			break;
		}
		participants.push_back(actor);
		queuePlaces.push_back(k);
		used.push_back(command.id);
		characters.push_back(combatants_[actor].defId);
	}
	for (const ComboRecipe& recipe : db_->combos) {
		const std::size_t size = recipe.abilities.size();
		if (size > participants.size() || !recipeMatches(*db_, recipe, used, characters)) {
			continue;
		}
		participants.resize(size);
		characters.resize(size);
		const AbilityDef& combo = *db_->ability(recipe.result);
		const int lead = participants.front();
		const AbilityDef& leading = *db_->ability(commands_[lead].id);
		const int aim = aimAt(lead, leading.targeting, leading.effects, commands_[lead].target, out);
		const std::vector<int> targets = resolveTargets(lead, combo.targeting, aim, combo.revives());
		Event& e = emit(out, EventType::ComboTriggered);
		e.actor = lead;
		e.ability = combo.id;
		e.combo = recipe.id;
		e.target = targets.size() == 1 ? targets.front() : -1;
		e.participants = participants;
		if (comboLog_ && comboLog_->discover(recipe.id, playTimeMs_, characters)) {
			emit(out, EventType::ComboDiscovered).combo = recipe.id;
		}
		std::vector<std::string> components;
		for (int actor : participants) {
			components.push_back(commands_[actor].id);
		}
		applyEffects(lead, comboTeam(participants, components, combo, out), combo.id, combo.effects, targets, false, out);
		return static_cast<int>(queuePlaces[size - 1] - index + 1);
	}
	return 0;
}

// Every participant pays for its component. Damage adds up each participant's stat for its component (its attack_stat,
// else mental for a technique and strength otherwise) and each component's damage power; other effects read the
// average stats.
Battle::Attacker Battle::comboTeam(const std::vector<int>& participants, const std::vector<std::string>& components, const AbilityDef& combo, std::vector<Event>& out) {
	Attacker team;
	team.combo = true;
	for (std::size_t k = 0; k < participants.size(); ++k) {
		const int actor = participants[k];
		const AbilityDef& component = *db_->ability(components[k]);
		payCost(actor, component, out);
		const Attacker member = attackerOf(actor);
		team.strength += member.strength;
		team.mental += member.mental;
		team.dexterity += member.dexterity;
		team.critical += member.critical;
		team.comboAttack += component.attackStat ? combatants_[actor].effective(*component.attackStat) : component.kind == AbilityKind::Technique ? member.mental : member.strength;
		team.comboBonus += damagePower(component);
	}
	const int count = static_cast<int>(participants.size());
	team.strength /= count;
	team.mental /= count;
	team.dexterity /= count;
	team.critical /= count;
	team.changeAmount = team.comboAttack;
	team.groupTarget = combo.targeting == Targeting::EnemyGroup || combo.targeting == Targeting::AllEnemies;
	team.variance = combo.variance;
	return team;
}

// Right after an enemy its AI drives drew its action (B4): its part is the first one (any one for an unordered
// recipe) of its id with the action it drew. The next queued turns, passing over party members who fell earlier in
// the round, must be living enemies its AI drives that can act and pay for a part not yet taken, of that part's id
// (the next part's in an ordered recipe). The first recipe in data order that fills every part fires on the lead's
// foe; the others' turns are spent on it, so none of them draws an action. A participant that has fallen is not in
// the queue alive, so a recipe needs all its parts standing. Returns the queued turns consumed, or 0.
int Battle::tryEnemyCombo(const std::vector<int>& order, std::size_t index, std::vector<Event>& out) {
	const int lead = order[index];
	const Command& leading = commands_[lead];
	const std::string drawn = leading.kind == CommandKind::Attack ? combatants_[lead].basicAttack : leading.kind == CommandKind::Ability ? leading.id : std::string();
	for (const EnemyComboRecipe& recipe : db_->enemyCombos) {
		const std::size_t size = recipe.parts.size();
		std::vector<bool> taken(size, false);
		const auto fits = [&](std::size_t p, int actor) {
			return !taken[p] && recipe.parts[p].enemy == combatants_[actor].defId && (!recipe.ordered || std::count(taken.begin(), taken.end(), true) == static_cast<std::ptrdiff_t>(p));
		};
		std::size_t first = 0;
		while (first < size && !(fits(first, lead) && recipe.parts[first].ability == drawn)) {
			++first;
		}
		if (first == size) {
			continue;
		}
		taken[first] = true;
		std::vector<int> participants = {lead};
		std::vector<std::string> components = {drawn};
		std::size_t k = index + 1;
		for (; k < order.size() && participants.size() < size; ++k) {
			const int actor = order[k];
			if (isParty(actor) && !combatants_[actor].alive()) {
				continue;
			}
			if (isParty(actor) || !aiTurn_[actor] || !combatants_[actor].alive() || commands_[actor].kind == CommandKind::None || statusWith(actor, StatusAction::CannotAct)) {
				break;
			}
			std::size_t p = 0;
			while (p < size && !(fits(p, actor) && db_->ability(recipe.parts[p].ability) && checkUsable(actor, *db_->ability(recipe.parts[p].ability)) == CancelReason::None)) {
				++p;
			}
			if (p == size) {
				break;
			}
			taken[p] = true;
			participants.push_back(actor);
			components.push_back(recipe.parts[p].ability);
		}
		if (participants.size() != size) {
			continue;
		}
		const AbilityDef& combo = *db_->ability(recipe.result);
		const std::vector<int> targets = resolveTargets(lead, combo.targeting, leading.target, combo.revives());
		Event& e = emit(out, EventType::ComboTriggered);
		e.actor = lead;
		e.ability = combo.id;
		e.combo = recipe.id;
		e.target = targets.size() == 1 ? targets.front() : -1;
		e.participants = participants;
		applyEffects(lead, comboTeam(participants, components, combo, out), combo.id, combo.effects, targets, false, out);
		return static_cast<int>(k - index);
	}
	return 0;
}

}
