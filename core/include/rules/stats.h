#pragma once

namespace rules {

struct Stats {
	int maxHp = 1;
	int maxTp = 0;
	int strength = 1;
	int mental = 1;
	int agility = 1;
	int dexterity = 1;
	int defense = 0;
	int level = 1;
	// Percent chance to land a critical hit with physical damage.
	int critical = 0;
	// Percent taken off the hit chance of physical attacks aimed at this combatant.
	int evasion = 0;

	static Stats filled(int value) {
		return {value, value, value, value, value, value, value, value, value, value};
	}
};

}
