#pragma once

#include "rules/stats.h"
#include "rules/types.h"

#include <string>
#include <vector>

namespace rules {

// A vehicle that fights for the party in a vehicle battle (BAT-140). Its abilities are skills.
struct VehicleDef {
	std::string id;
	std::string name;
	Stats stats;
	std::string basicAttack;
	std::vector<std::string> abilities;
	Affinities affinities;
	StatusResists statusResist;
};

}
