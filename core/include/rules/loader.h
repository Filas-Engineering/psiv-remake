#pragma once

#include "rules/database.h"
#include "rules/result.h"

#include <map>
#include <string>
#include <vector>

namespace rules {

// The data files, in load order. Each file may reference only the files before it.
const std::vector<std::string>& dataFileNames();

// Keys are the file names from dataFileNames().
Result<Database> loadDatabaseFromTexts(const std::map<std::string, std::string>& files);

Result<Database> loadDatabaseFromDirectory(const std::string& directory);

}
