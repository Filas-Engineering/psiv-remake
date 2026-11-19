#pragma once

#include <string>

namespace rules {

// The loader requires a status with this id. The battle adds it when hit points reach zero.
inline constexpr const char* kDefeatedStatus = "defeated";

struct StatusDef {
	std::string id;
	std::string name;
	bool skipsTurn = false;
	bool blocksTechniques = false;
	bool wakesOnDamage = false;
	int tickDamagePercent = 0;
	// Rounds before it wears off; 0 lasts until cured.
	int duration = 0;
	// Rolled at the start of each prevented turn.
	int recoverChance = 0;
};

}
