#pragma once

#include "rules/character.h"
#include "rules/event.h"
#include "rules/message.h"
#include "rules/types.h"

#include <string>
#include <vector>

namespace rules {

struct Database;

struct FieldUse {
	// Empty when the use went ahead.
	Message rejection;
	// The same event types a battle emits for these effects, with round 0.
	std::vector<Event> events;
};

// Out-of-battle use (BAT-044) runs the battle's own effect code on the party. `target` is a party index, or
// -1 for an item or ability that hits every member. Only items and abilities marked "field" in data qualify.
// An item that changes nothing is not used up, as in battle.
FieldUse useItemOutsideBattle(const Database& db, std::vector<PartyMember>& party, Inventory& inventory, const std::string& itemId, int target);
FieldUse useAbilityOutsideBattle(const Database& db, std::vector<PartyMember>& party, int user, const std::string& abilityId, int target);

}
