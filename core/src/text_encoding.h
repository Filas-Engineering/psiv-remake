#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rules::text {

// Ids and other strings go into the line formats as single tokens: characters other than letters, digits
// and '_' become %XX, and the empty string becomes "-".
std::string encode(const std::string& value);
bool decode(const std::string& token, std::string& out);

// A list of strings as one token: each encoded, joined by ',' ("-" for an empty list).
std::string encodeList(const std::vector<std::string>& values);
bool decodeList(const std::string& token, std::vector<std::string>& out);

std::vector<std::string> split(const std::string& line);
bool parseInt(const std::string& text, int& out);
bool parseInt64(const std::string& text, std::int64_t& out);

// FNV-1a 64 over the bytes, as 16 lowercase hex digits.
std::string checksum(const std::string& bytes);

}
