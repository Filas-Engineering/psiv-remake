#include "text_encoding.h"

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

}
