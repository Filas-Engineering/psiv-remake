#pragma once

#include <string>
#include <utility>
#include <vector>

namespace rules {

// The keys one kind of JSON object in the data accepts. The loaders check objects against these sets only, and the
// capabilities report (game_cli --capabilities) lists them, so the two cannot disagree. `file` is the data file, or
// "shared" for objects several files hold (stats, effects, variance); `object` is the path inside it, empty for the
// root. The constructor is explicit so that a key list cannot be passed where a set is expected.
class DataKeySet {
public:
	explicit DataKeySet(std::string file, std::string object, std::vector<std::string> keys)
		: file(std::move(file)), object(std::move(object)), keys(std::move(keys)) {}

	std::string file;
	std::string object;
	std::vector<std::string> keys;
};

// A fixed set of words a data key takes, from the same tables the parsers read (names.h).
struct DataEnum {
	std::string name;
	std::vector<std::string> values;
};

// Every object of core's data files, sorted by file and object.
const std::vector<DataKeySet>& coreDataKeys();
// The set for one object; a name that is not in coreDataKeys() is a programming error and aborts.
const DataKeySet& dataKeys(const std::string& file, const std::string& object);
// The enumerations of core's data, by name.
const std::vector<DataEnum>& coreDataEnums();

}
