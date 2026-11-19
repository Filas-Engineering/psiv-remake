#pragma once

#include <string>
#include <utility>

namespace rules {

enum class CommandKind { None, Attack, Ability, Item, Defend, Flee };

struct Command {
	CommandKind kind = CommandKind::None;
	// Ability id for Ability, item id for Item.
	std::string id;
	// Combatant index, or -1 to let the battle pick.
	int target = -1;

	static Command attack(int target) { return {CommandKind::Attack, "", target}; }
	static Command ability(std::string id, int target) { return {CommandKind::Ability, std::move(id), target}; }
	static Command item(std::string id, int target) { return {CommandKind::Item, std::move(id), target}; }
	static Command defend() { return {CommandKind::Defend, "", -1}; }
	static Command flee() { return {CommandKind::Flee, "", -1}; }
};

}
