#pragma once

#include <string>

namespace rules::text {

// Ids and other strings go into the line formats as single tokens: characters other than letters, digits
// and '_' become %XX, and the empty string becomes "-".
std::string encode(const std::string& value);
bool decode(const std::string& token, std::string& out);

}
