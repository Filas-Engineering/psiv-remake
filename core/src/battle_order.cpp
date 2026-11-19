#include "rules/battle.h"

#include "rules/formulas.h"

#include <algorithm>

namespace rules {

namespace {

struct Slot {
	int actor;
	int initiative;
};

}

std::vector<int> Battle::computeTurnOrder() {
	std::vector<Slot> slots;
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (!combatants_[i].alive() || commands_[i].kind == CommandKind::None) {
			continue;
		}
		const int agility = combatants_[i].effective(Stat::Agility);
		const int roll = rng_.range(0, formulas::initiativeSpread(agility));
		slots.push_back({i, formulas::initiative(agility, roll)});
	}
	std::sort(slots.begin(), slots.end(), [this](const Slot& a, const Slot& b) {
		if (a.initiative != b.initiative) {
			return a.initiative > b.initiative;
		}
		if (isParty(a.actor) != isParty(b.actor)) {
			return isParty(a.actor);
		}
		return a.actor < b.actor;
	});

	std::vector<int> block;
	int blockTotal = 0;
	for (int actor : forcedOrder_) {
		for (const Slot& slot : slots) {
			if (slot.actor == actor) {
				block.push_back(actor);
				blockTotal += slot.initiative;
			}
		}
	}

	std::vector<int> order;
	if (block.empty()) {
		for (const Slot& slot : slots) {
			order.push_back(slot.actor);
		}
		return order;
	}

	// A macro acts as one block, at its members' average initiative, so its steps stay adjacent.
	const int blockInitiative = blockTotal / static_cast<int>(block.size());
	bool placed = false;
	for (const Slot& slot : slots) {
		if (std::find(block.begin(), block.end(), slot.actor) != block.end()) {
			continue;
		}
		if (!placed && slot.initiative <= blockInitiative) {
			order.insert(order.end(), block.begin(), block.end());
			placed = true;
		}
		order.push_back(slot.actor);
	}
	if (!placed) {
		order.insert(order.end(), block.begin(), block.end());
	}
	return order;
}

}
