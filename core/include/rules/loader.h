#pragma once

#include "rules/database.h"
#include "rules/result.h"

#include <map>
#include <string>

namespace rules {

// Keys are file names: abilities.json, statuses.json, items.json, party.json, enemies.json, combos.json.
Result<Database> loadDatabaseFromTexts(const std::map<std::string, std::string>& files);

Result<Database> loadDatabaseFromDirectory(const std::string& directory);

}
