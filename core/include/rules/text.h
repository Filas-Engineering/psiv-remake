#pragma once

#include "rules/result.h"

#include <map>
#include <string>
#include <vector>

namespace rules {

struct Database;

// Message key to text. A template names its parameters by position: {0}, {1} and so on.
using TextTable = std::map<std::string, std::string>;

// Reads {"text": {"key": "template", ...}}.
Result<TextTable> parseTextTable(const std::string& json);
Result<TextTable> loadTextTable(const std::string& path);

// The template with each {N} replaced by params[N]. A missing key gives the key itself.
std::string formatText(const TextTable& table, const std::string& key, const std::vector<std::string>& params);

// The English template of every key core puts in a load Problem (load.*, json.*, text.*).
const TextTable& problemText();
// "file:line: entry: text", leaving out what the problem does not have.
std::string formatProblem(const Problem& problem, const TextTable& table);
// One formatted problem per line.
std::string formatProblems(const std::vector<Problem>& problems, const TextTable& table);

// Every message key core can return or emit with this data, sorted.
std::vector<std::string> messageKeys(const Database& db);

// Status events carry the key for their status and kind. The templates take {0} the target and {1} the status.
std::string statusMessageKey(const std::string& statusId, const char* kind);

}
