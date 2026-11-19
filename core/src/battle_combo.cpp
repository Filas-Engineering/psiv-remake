#include "rules/battle.h"

#include <algorithm>
#include <numeric>

namespace rules {

namespace {

bool componentMatches(const ComboRecipe& recipe, std::size_t component, const std::string& ability, const std::string& character) {
	const bool tied = component < recipe.characters.size() && !recipe.characters[component].empty();
	return recipe.abilities[component] == ability && (!tied || recipe.characters[component] == character);
}

// Participant k used abilities[k] and is characters[k]. An ordered recipe maps participant k to component k; an
// unordered one accepts any one-to-one mapping.
bool recipeMatches(const ComboRecipe& recipe, const std::vector<std::string>& abilities, const std::vector<std::string>& characters) {
	std::vector<std::size_t> assignment(abilities.size());
	std::iota(assignment.begin(), assignment.end(), std::size_t(0));
	do {
		bool all = true;
		for (std::size_t k = 0; k < abilities.size() && all; ++k) {
			all = componentMatches(recipe, assignment[k], abilities[k], characters[k]);
		}
		if (all) {
			return true;
		}
	} while (!recipe.ordered && std::next_permutation(assignment.begin(), assignment.end()));
	return false;
}

}

// Returns how many queued turns the combo consumed, or 0 when none fires at this position.
int Battle::tryCombo(const std::vector<int>& order, std::size_t index, std::vector<Event>& out) {
	for (const ComboRecipe* recipe : combosLongestFirst_) {
		const std::size_t size = recipe->abilities.size();
		if (index + size > order.size()) {
			continue;
		}
		std::vector<int> participants;
		std::vector<std::string> used;
		std::vector<std::string> characters;
		for (std::size_t k = 0; k < size; ++k) {
			const int actor = order[index + k];
			const Command& command = commands_[actor];
			if (!isParty(actor) || !combatants_[actor].alive() || command.kind != CommandKind::Ability || statusWith(actor, StatusAction::CannotAct)) {
				break;
			}
			const AbilityDef* ability = db_->ability(command.id);
			if (!ability || checkUsable(actor, *ability) != CancelReason::None) {
				break;
			}
			participants.push_back(actor);
			used.push_back(command.id);
			characters.push_back(combatants_[actor].defId);
		}
		if (participants.size() != size || !recipeMatches(*recipe, used, characters)) {
			continue;
		}
		const AbilityDef* combo = db_->ability(recipe->result);
		const int lead = participants.front();
		const int requested = commands_[lead].target;
		const std::vector<int> targets = combo ? executionTargets(lead, combo->targeting, requested, combo->revives()) : std::vector<int>();
		if (targets.empty()) {
			continue;
		}
		noteRetarget(lead, combo->targeting, requested, targets, out);
		Event& e = emit(out, EventType::ComboTriggered);
		e.actor = lead;
		e.ability = combo->id;
		e.combo = recipe->id;
		e.target = targets.size() == 1 ? targets.front() : -1;
		e.participants = participants;
		if (comboLog_ && comboLog_->discover(recipe->id, playTimeMs_, characters)) {
			emit(out, EventType::ComboDiscovered).combo = recipe->id;
		}
		Attacker team;
		for (int actor : participants) {
			payCost(actor, *db_->ability(commands_[actor].id), out);
			const Attacker member = attackerOf(actor);
			team.strength += member.strength;
			team.mental += member.mental;
			team.dexterity += member.dexterity;
			team.critical += member.critical;
		}
		const int count = static_cast<int>(participants.size());
		team.strength /= count;
		team.mental /= count;
		team.dexterity /= count;
		team.critical /= count;
		applyEffects(lead, team, combo->id, combo->effects, targets, false, out);
		return static_cast<int>(size);
	}
	return 0;
}

}
