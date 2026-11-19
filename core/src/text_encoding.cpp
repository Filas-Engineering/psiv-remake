#include "text_encoding.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <sstream>

namespace rules::text {

namespace {

bool plain(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

int hexValue(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

}

std::string encode(const std::string& value) {
	if (value.empty()) {
		return "-";
	}
	static const char digits[] = "0123456789ABCDEF";
	std::string out;
	for (char c : value) {
		if (plain(c)) {
			out += c;
		} else {
			const unsigned char byte = static_cast<unsigned char>(c);
			out += '%';
			out += digits[byte >> 4];
			out += digits[byte & 15];
		}
	}
	return out;
}

bool decode(const std::string& token, std::string& out) {
	out.clear();
	if (token == "-") {
		return true;
	}
	for (std::size_t i = 0; i < token.size(); ++i) {
		const char c = token[i];
		if (plain(c)) {
			out += c;
			continue;
		}
		if (c != '%' || i + 2 >= token.size()) {
			return false;
		}
		const int high = hexValue(token[i + 1]);
		const int low = hexValue(token[i + 2]);
		if (high < 0 || low < 0) {
			return false;
		}
		out += static_cast<char>(high * 16 + low);
		i += 2;
	}
	return !out.empty();
}

std::string encodeList(const std::vector<std::string>& values) {
	if (values.empty()) {
		return "-";
	}
	std::string out;
	for (std::size_t i = 0; i < values.size(); ++i) {
		out += (i ? "," : "") + encode(values[i]);
	}
	return out;
}

bool decodeList(const std::string& token, std::vector<std::string>& out) {
	out.clear();
	if (token == "-") {
		return true;
	}
	std::size_t start = 0;
	while (true) {
		const std::size_t comma = token.find(',', start);
		std::string value;
		if (!decode(token.substr(start, comma == std::string::npos ? std::string::npos : comma - start), value)) {
			return false;
		}
		out.push_back(value);
		if (comma == std::string::npos) {
			return true;
		}
		start = comma + 1;
	}
}

std::vector<std::string> split(const std::string& line) {
	std::vector<std::string> tokens;
	std::istringstream in(line);
	std::string token;
	while (in >> token) {
		tokens.push_back(token);
	}
	return tokens;
}

bool parseInt(const std::string& text, int& out) {
	std::int64_t value = 0;
	if (text.size() > 11 || !parseInt64(text, value) || value < INT_MIN || value > INT_MAX) {
		return false;
	}
	out = static_cast<int>(value);
	return true;
}

bool parseInt64(const std::string& text, std::int64_t& out) {
	if (text.empty() || text.size() > 20) {
		return false;
	}
	char* end = nullptr;
	errno = 0;
	const long long value = std::strtoll(text.c_str(), &end, 10);
	if (*end != '\0' || errno == ERANGE) {
		return false;
	}
	out = value;
	return true;
}

std::string checksum(const std::string& bytes) {
	std::uint64_t hash = 14695981039346656037ULL;
	for (char c : bytes) {
		hash ^= static_cast<unsigned char>(c);
		hash *= 1099511628211ULL;
	}
	static const char digits[] = "0123456789abcdef";
	std::string out(16, '0');
	for (int i = 15; i >= 0; --i) {
		out[static_cast<std::size_t>(i)] = digits[hash & 15];
		hash >>= 4;
	}
	return out;
}

}
