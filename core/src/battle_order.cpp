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
		slots.push_back({i, combatants_[i].effective(Stat::Agility)});
	}
	int highest = 0;
	for (const Slot& slot : slots) {
		highest = std::max(highest, slot.initiative);
	}
	for (Slot& slot : slots) {
		slot.initiative = formulas::initiative(slot.initiative, rng_.range(0, formulas::initiativeSpread(highest)));
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
	for (int actor : forcedOrder_) {
		for (const Slot& slot : slots) {
			if (slot.actor == actor) {
				block.push_back(actor);
			}
		}
	}

	std::vector<int> order;
	for (const Slot& slot : slots) {
		order.push_back(slot.actor);
	}
	// A macro member who would act before the member listed ahead of it waits and acts right
	// after that member. One already acting later stays put, with whoever is between them.
	for (std::size_t k = 1; k < block.size(); ++k) {
		const auto earlier = std::find(order.begin(), order.end(), block[k - 1]);
		const auto later = std::find(order.begin(), order.end(), block[k]);
		if (later < earlier) {
			std::rotate(later, later + 1, earlier + 1);
		}
	}
	return order;
}

}
