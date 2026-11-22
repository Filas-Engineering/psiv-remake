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
	// Subtracted from elemental damage in place of mental when rules.json `magic_defense` is set.
	int magicDefense = 0;
	// Enemies' and vehicles' `stats.attack`: what their basic attacks and `attack_stat: "attack"` hit with, in place of
	// strength; -1 when left out, which keeps attack equal to strength.
	int attack = -1;

	static Stats filled(int value) {
		return {value, value, value, value, value, value, value, value, value, value, value};
	}
};

}
