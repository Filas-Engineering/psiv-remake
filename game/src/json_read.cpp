#include "json_read.h"

#include <algorithm>
#include <cmath>

namespace game {

using rules::JsonValue;

void JsonReader::fail(const JsonValue& at, const std::string& message) {
	errors_.push_back(file_ + ":" + std::to_string(at.line) + ": " + message);
}

std::string JsonReader::errors() const {
	std::string out;
	for (const std::string& e : errors_) {
		out += (out.empty() ? "" : "\n") + e;
	}
	return out;
}

void JsonReader::only(const JsonValue& object, const std::vector<std::string>& known) {
	for (std::size_t i = 0; i < object.keys.size(); ++i) {
		if (std::find(known.begin(), known.end(), object.keys[i]) == known.end()) {
			fail(object.values[i], "unknown field '" + object.keys[i] + "'");
		}
	}
}

const JsonValue* JsonReader::field(const JsonValue& object, const char* key, JsonValue::Type type, bool required) {
	const JsonValue* value = object.find(key);
	if (!value) {
		if (required) {
			fail(object, std::string("missing field '") + key + "'");
		}
		return nullptr;
	}
	if (!value->is(type)) {
		fail(*value, std::string("field '") + key + "' must be a " + rules::jsonTypeName(type));
		return nullptr;
	}
	return value;
}

void JsonReader::text(const JsonValue& object, const char* key, std::string& out, bool required) {
	if (const JsonValue* v = field(object, key, JsonValue::Type::String, required)) {
		if (v->string.empty()) {
			fail(*v, std::string("field '") + key + "' cannot be empty");
			return;
		}
		out = v->string;
	}
}

void JsonReader::integer(const JsonValue& object, const char* key, int& out, bool required, int min, int max) {
	if (const JsonValue* v = field(object, key, JsonValue::Type::Number, required)) {
		if (std::floor(v->number) != v->number || v->number < min || v->number > max) {
			fail(*v, std::string("field '") + key + "' must be a whole number from " + std::to_string(min) + " to " + std::to_string(max));
			return;
		}
		out = static_cast<int>(v->number);
	}
}

void JsonReader::boolean(const JsonValue& object, const char* key, bool& out, bool required) {
	if (const JsonValue* v = field(object, key, JsonValue::Type::Bool, required)) {
		out = v->boolean;
	}
}

void JsonReader::texts(const JsonValue& object, const char* key, std::vector<std::string>& out, bool required) {
	if (const JsonValue* v = field(object, key, JsonValue::Type::Array, required)) {
		out.clear();
		for (const JsonValue& item : v->items) {
			if (!item.is(JsonValue::Type::String) || item.string.empty()) {
				fail(item, std::string("'") + key + "' holds text only");
				continue;
			}
			out.push_back(item.string);
		}
	}
}

void JsonReader::counts(const JsonValue& object, const char* key, std::map<std::string, int>& out, bool required, int min, int max) {
	if (const JsonValue* v = field(object, key, JsonValue::Type::Object, required)) {
		out.clear();
		for (std::size_t i = 0; i < v->keys.size(); ++i) {
			const JsonValue& n = v->values[i];
			if (!n.is(JsonValue::Type::Number) || std::floor(n.number) != n.number || n.number < min || n.number > max) {
				fail(n, std::string("'") + key + "." + v->keys[i] + "' must be a whole number from " + std::to_string(min) + " to " + std::to_string(max));
				continue;
			}
			out[v->keys[i]] = static_cast<int>(n.number);
		}
	}
}

bool JsonReader::cell(const JsonValue& value, Cell& out) {
	if (!value.is(JsonValue::Type::Array) || value.items.size() != 2 || !value.items[0].is(JsonValue::Type::Number) ||
		!value.items[1].is(JsonValue::Type::Number)) {
		fail(value, "a cell is written [x, y]");
		return false;
	}
	out = {static_cast<int>(value.items[0].number), static_cast<int>(value.items[1].number)};
	return true;
}

void JsonReader::cellField(const JsonValue& object, const char* key, Cell& out, bool required) {
	if (const JsonValue* v = field(object, key, JsonValue::Type::Array, required)) {
		cell(*v, out);
	}
}

}
