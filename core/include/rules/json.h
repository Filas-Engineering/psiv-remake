#pragma once

#include "rules/result.h"

#include <string>
#include <vector>

namespace rules {

class JsonValue {
public:
	enum class Type { Null, Bool, Number, String, Array, Object };

	Type type = Type::Null;
	bool boolean = false;
	double number = 0;
	std::string string;
	std::vector<JsonValue> items;
	// Object members, in file order.
	std::vector<std::string> keys;
	std::vector<JsonValue> values;
	int line = 0;

	const JsonValue* find(const std::string& key) const;
	bool is(Type t) const { return type == t; }
};

const char* jsonTypeName(JsonValue::Type type);
bool parseJsonType(const std::string& text, JsonValue::Type& out);

Result<JsonValue> parseJson(const std::string& text);

}
