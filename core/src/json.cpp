#include "rules/json.h"

#include <cstdlib>

namespace rules {

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
	explicit Parser(const std::string& text) : text_(text) {}

	Result<JsonValue> run() {
		JsonValue root;
		skipWhitespace();
		if (!parseValue(root, 0)) {
			return Result<JsonValue>::failure(error_);
		}
		skipWhitespace();
		if (pos_ < text_.size()) {
			fail("unexpected content after the top-level value");
			return Result<JsonValue>::failure(error_);
		}
		return Result<JsonValue>::success(std::move(root));
	}

private:
	bool fail(const std::string& message) {
		if (error_.empty()) {
			error_ = "line " + std::to_string(line_) + ": " + message;
		}
		return false;
	}

	bool atEnd() const { return pos_ >= text_.size(); }
	char peek() const { return atEnd() ? '\0' : text_[pos_]; }

	void skipWhitespace() {
		while (!atEnd()) {
			const char c = text_[pos_];
			if (c == '\n') {
				++line_;
			} else if (c != ' ' && c != '\t' && c != '\r') {
				return;
			}
			++pos_;
		}
	}

	bool parseValue(JsonValue& out, int depth) {
		if (depth > kMaxDepth) {
			return fail("nesting is too deep");
		}
		out.line = line_;
		switch (peek()) {
		case '{': return parseObject(out, depth);
		case '[': return parseArray(out, depth);
		case '"':
			out.type = JsonValue::Type::String;
			return parseString(out.string);
		case 't': return parseLiteral("true", out, JsonValue::Type::Bool, true);
		case 'f': return parseLiteral("false", out, JsonValue::Type::Bool, false);
		case 'n': return parseLiteral("null", out, JsonValue::Type::Null, false);
		case '\0':
			if (atEnd()) {
				return fail("unexpected end of input");
			}
			return fail("unexpected character");
		default:
			if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
				return parseNumber(out);
			}
			return fail(std::string("unexpected character '") + peek() + "'");
		}
	}

	bool parseLiteral(const char* word, JsonValue& out, JsonValue::Type type, bool value) {
		const std::string w(word);
		if (text_.compare(pos_, w.size(), w) != 0) {
			return fail("invalid literal, expected '" + w + "'");
		}
		pos_ += w.size();
		out.type = type;
		out.boolean = value;
		return true;
	}

	bool digits() {
		const std::size_t start = pos_;
		while (!atEnd() && text_[pos_] >= '0' && text_[pos_] <= '9') {
			++pos_;
		}
		return pos_ > start;
	}

	bool parseNumber(JsonValue& out) {
		const std::size_t start = pos_;
		if (peek() == '-') {
			++pos_;
		}
		if (peek() == '0') {
			++pos_;
		} else if (!digits()) {
			return fail("invalid number");
		}
		if (peek() == '.') {
			++pos_;
			if (!digits()) {
				return fail("invalid number: expected digits after '.'");
			}
		}
		if (peek() == 'e' || peek() == 'E') {
			++pos_;
			if (peek() == '+' || peek() == '-') {
				++pos_;
			}
			if (!digits()) {
				return fail("invalid number: expected exponent digits");
			}
		}
		const std::string literal = text_.substr(start, pos_ - start);
		out.type = JsonValue::Type::Number;
		out.number = std::strtod(literal.c_str(), nullptr);
		return true;
	}

	static void appendUtf8(std::string& out, unsigned code) {
		if (code < 0x80) {
			out += static_cast<char>(code);
		} else if (code < 0x800) {
			out += static_cast<char>(0xC0 | (code >> 6));
			out += static_cast<char>(0x80 | (code & 0x3F));
		} else if (code < 0x10000) {
			out += static_cast<char>(0xE0 | (code >> 12));
			out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (code & 0x3F));
		} else {
			out += static_cast<char>(0xF0 | (code >> 18));
			out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (code & 0x3F));
		}
	}

	bool parseHex4(unsigned& out) {
		if (pos_ + 4 > text_.size()) {
			return fail("truncated \\u escape");
		}
		out = 0;
		for (int i = 0; i < 4; ++i) {
			const char c = text_[pos_++];
			out <<= 4;
			if (c >= '0' && c <= '9') {
				out |= static_cast<unsigned>(c - '0');
			} else if (c >= 'a' && c <= 'f') {
				out |= static_cast<unsigned>(c - 'a' + 10);
			} else if (c >= 'A' && c <= 'F') {
				out |= static_cast<unsigned>(c - 'A' + 10);
			} else {
				return fail("invalid \\u escape");
			}
		}
		return true;
	}

	bool parseString(std::string& out) {
		++pos_;
		while (true) {
			if (atEnd()) {
				return fail("unterminated string");
			}
			const char c = text_[pos_++];
			if (c == '"') {
				return true;
			}
			if (static_cast<unsigned char>(c) < 0x20) {
				return fail("control character in string (strings cannot span lines)");
			}
			if (c != '\\') {
				out += c;
				continue;
			}
			if (atEnd()) {
				return fail("unterminated string");
			}
			const char e = text_[pos_++];
			switch (e) {
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u': {
				unsigned code = 0;
				if (!parseHex4(code)) {
					return false;
				}
				if (code >= 0xD800 && code <= 0xDBFF) {
					unsigned low = 0;
					if (text_.compare(pos_, 2, "\\u") != 0) {
						return fail("unpaired surrogate in \\u escape");
					}
					pos_ += 2;
					if (!parseHex4(low)) {
						return false;
					}
					if (low < 0xDC00 || low > 0xDFFF) {
						return fail("invalid surrogate pair in \\u escape");
					}
					code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
				}
				appendUtf8(out, code);
				break;
			}
			default: return fail(std::string("invalid escape '\\") + e + "'");
			}
		}
	}

	bool parseArray(JsonValue& out, int depth) {
		out.type = JsonValue::Type::Array;
		++pos_;
		skipWhitespace();
		if (peek() == ']') {
			++pos_;
			return true;
		}
		while (true) {
			JsonValue item;
			skipWhitespace();
			if (!parseValue(item, depth + 1)) {
				return false;
			}
			out.items.push_back(std::move(item));
			skipWhitespace();
			if (peek() == ',') {
				++pos_;
				continue;
			}
			if (peek() == ']') {
				++pos_;
				return true;
			}
			return fail("expected ',' or ']' in array");
		}
	}

	bool parseObject(JsonValue& out, int depth) {
		out.type = JsonValue::Type::Object;
		++pos_;
		skipWhitespace();
		if (peek() == '}') {
			++pos_;
			return true;
		}
		while (true) {
			skipWhitespace();
			if (peek() != '"') {
				return fail("expected a quoted key in object");
			}
			std::string key;
			if (!parseString(key)) {
				return false;
			}
			if (out.find(key)) {
				return fail("duplicate key '" + key + "'");
			}
			skipWhitespace();
			if (peek() != ':') {
				return fail("expected ':' after key '" + key + "'");
			}
			++pos_;
			skipWhitespace();
			JsonValue value;
			if (!parseValue(value, depth + 1)) {
				return false;
			}
			out.keys.push_back(std::move(key));
			out.values.push_back(std::move(value));
			skipWhitespace();
			if (peek() == ',') {
				++pos_;
				continue;
			}
			if (peek() == '}') {
				++pos_;
				return true;
			}
			return fail("expected ',' or '}' in object");
		}
	}

	const std::string& text_;
	std::size_t pos_ = 0;
	int line_ = 1;
	std::string error_;
};

}

const JsonValue* JsonValue::find(const std::string& key) const {
	for (std::size_t i = 0; i < keys.size(); ++i) {
		if (keys[i] == key) {
			return &values[i];
		}
	}
	return nullptr;
}

const char* jsonTypeName(JsonValue::Type type) {
	switch (type) {
	case JsonValue::Type::Null: return "null";
	case JsonValue::Type::Bool: return "boolean";
	case JsonValue::Type::Number: return "number";
	case JsonValue::Type::String: return "string";
	case JsonValue::Type::Array: return "array";
	case JsonValue::Type::Object: return "object";
	}
	return "unknown";
}

bool parseJsonType(const std::string& text, JsonValue::Type& out) {
	for (JsonValue::Type type : {JsonValue::Type::Null, JsonValue::Type::Bool, JsonValue::Type::Number, JsonValue::Type::String, JsonValue::Type::Array, JsonValue::Type::Object}) {
		if (text == jsonTypeName(type)) {
			out = type;
			return true;
		}
	}
	return false;
}

Result<JsonValue> parseJson(const std::string& text) {
	return Parser(text).run();
}

}
