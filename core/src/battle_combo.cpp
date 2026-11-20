#include "rules/battle.h"

#include <algorithm>
#include <numeric>

namespace rules {

namespace {

bool componentMatches(const ComboRecipe& recipe, std::size_t component, const std::string& ability, const std::string& character) {
	const bool tied = component < recipe.characters.size() && !recipe.characters[component].empty();
	return recipe.abilities[component] == ability && (!tied || recipe.characters[component] == character);
}

// Participant k used abilities[k] and is characters[k]. An ordered recipe, and every four-member one, maps
// participant k to component k; an unordered one accepts any one-to-one mapping.
bool recipeMatches(const ComboRecipe& recipe, const std::vector<std::string>& abilities, const std::vector<std::string>& characters) {
	const bool ordered = recipe.ordered || recipe.abilities.size() == static_cast<std::size_t>(kMaxComboParticipants);
	std::vector<std::size_t> assignment(recipe.abilities.size());
	std::iota(assignment.begin(), assignment.end(), std::size_t(0));
	do {
		bool all = true;
		for (std::size_t k = 0; k < assignment.size() && all; ++k) {
			all = componentMatches(recipe, assignment[k], abilities[k], characters[k]);
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

// At the start of each actor's turn (BAT-061): up to four queued turns from this actor, stopping at an enemy, a
// member who cannot act, a command that is not a technique or skill, or one that cannot be paid for. The first
// recipe in data order that matches the first turns fires, and the other participants' turns are spent on it.
// Returns how many queued turns the combo consumed, or 0 when none fires.
int Battle::tryCombo(const std::vector<int>& order, std::size_t index, std::vector<Event>& out) {
	std::vector<int> participants;
	std::vector<std::string> used;
	std::vector<std::string> characters;
	for (std::size_t k = index; k < order.size() && participants.size() < static_cast<std::size_t>(kMaxComboParticipants); ++k) {
		const int actor = order[k];
		const Command& command = commands_[actor];
		if (!isParty(actor) || !combatants_[actor].alive() || command.kind != CommandKind::Ability || statusWith(actor, StatusAction::CannotAct)) {
			break;
		}
		const AbilityDef* ability = db_->ability(command.id);
		if (!ability || (ability->kind != AbilityKind::Technique && ability->kind != AbilityKind::Skill) || checkUsable(actor, *ability) != CancelReason::None) {
			break;
		}
		participants.push_back(actor);
		used.push_back(command.id);
		characters.push_back(combatants_[actor].defId);
	}
	for (const ComboRecipe& recipe : db_->combos) {
		const std::size_t size = recipe.abilities.size();
		if (size > participants.size() || !recipeMatches(recipe, used, characters)) {
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
		// Damage adds up each participant's stat for its component and each component's damage power. Other effects
		// read the participants' average stats.
		Attacker team;
		team.combo = true;
		for (int actor : participants) {
			const AbilityDef& component = *db_->ability(commands_[actor].id);
			payCost(actor, component, out);
			const Attacker member = attackerOf(actor);
			team.strength += member.strength;
			team.mental += member.mental;
			team.dexterity += member.dexterity;
			team.critical += member.critical;
			team.comboAttack += component.kind == AbilityKind::Technique ? member.mental : member.strength;
			team.comboBonus += damagePower(component);
		}
		const int count = static_cast<int>(participants.size());
		team.strength /= count;
		team.mental /= count;
		team.dexterity /= count;
		team.critical /= count;
		team.changeAmount = team.comboAttack;
		applyEffects(lead, team, combo.id, combo.effects, targets, false, out);
		return static_cast<int>(size);
	}
	return 0;
}

}
