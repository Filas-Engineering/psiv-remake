#include "rules/battle.h"

#include <algorithm>

namespace rules {

// Returns how many queued turns the combo consumed, or 0 when none fires at this position.
int Battle::tryCombo(const std::vector<int>& order, std::size_t index, std::vector<Event>& out) {
	for (const ComboRecipe* recipe : combosLongestFirst_) {
		const std::size_t size = recipe->abilities.size();
		if (index + size > order.size()) {
			continue;
		}
		std::vector<int> participants;
		std::vector<std::string> used;
		for (std::size_t k = 0; k < size; ++k) {
			const int actor = order[index + k];
			const Command& command = commands_[actor];
			if (!isParty(actor) || !combatants_[actor].alive() || command.kind != CommandKind::Ability || skipsTurn(actor)) {
				break;
			}
			const AbilityDef* ability = db_->ability(command.id);
			if (!ability || checkUsable(actor, *ability) != FailReason::None) {
				break;
			}
			participants.push_back(actor);
			used.push_back(command.id);
		}
		if (participants.size() != size) {
			continue;
		}
		std::vector<std::string> wanted = recipe->abilities;
		std::sort(wanted.begin(), wanted.end());
		std::sort(used.begin(), used.end());
		if (used != wanted) {
			continue;
		}
		const AbilityDef* combo = db_->ability(recipe->result);
		const int lead = participants.front();
		const std::vector<int> targets = combo ? resolveTargets(lead, combo->targeting, commands_[lead].target, combo->revives()) : std::vector<int>();
		if (targets.empty()) {
			continue;
		}
		Event& e = emit(out, EventType::ComboTriggered);
		e.actor = lead;
		e.ability = combo->id;
		e.target = targets.size() == 1 ? targets.front() : -1;
		e.participants = participants;
		e.firstDiscovery = comboLog_ && comboLog_->discover(recipe->id);
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
		applyEffects(lead, team, combo->effects, targets, false, out);
		return static_cast<int>(size);
	}
	return 0;
}

}
