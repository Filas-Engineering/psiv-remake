#pragma once

#include <string>
#include <vector>

namespace rules {

// A stable text key and its parameters (ids or numbers). The host looks the key up in its string table.
struct Message {
	std::string key;
	std::vector<std::string> params;

	bool empty() const { return key.empty(); }
};

}
