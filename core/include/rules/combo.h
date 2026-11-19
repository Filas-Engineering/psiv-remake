#pragma once

#include <set>
#include <string>
#include <vector>

namespace rules {

// Fires when party members use these abilities back to back in one round, in any order.
struct ComboRecipe {
	std::string id;
	std::vector<std::string> abilities;
	std::string result;
};

class ComboLog {
public:
	// Returns true the first time a recipe is discovered.
	bool discover(const std::string& recipeId);
	bool isDiscovered(const std::string& recipeId) const;
	const std::set<std::string>& discovered() const { return discovered_; }

private:
	std::set<std::string> discovered_;
};

}
