#include "loader_reader.h"

#include <cmath>

namespace rules::loader {

using Type = JsonValue::Type;

void Reader::fail(const JsonValue& at, const char* key, std::vector<std::string> params) {
	problems_.push_back({file_, at.line, context_, {key, std::move(params)}});
}

const JsonValue* Reader::get(const JsonValue& object, const char* key, Type type, bool required) {
	const JsonValue* value = object.find(key);
	if (!value) {
		if (required) {
			fail(object, "load.missing_field", {key});
		}
		return nullptr;
	}
	if (value->type != type) {
		fail(*value, "load.field_wrong_type", {key, std::string(jsonTypeName(type)), std::string(jsonTypeName(value->type))});
		return nullptr;
	}
	return value;
}

bool Reader::string(const JsonValue& object, const char* key, std::string& out, bool required) {
	const JsonValue* value = get(object, key, Type::String, required);
	if (!value) {
		return false;
	}
	if (value->string.empty()) {
		fail(*value, "load.field_cannot_empty", {key});
		return false;
	}
	out = value->string;
	return true;
}

bool Reader::integer(const JsonValue& object, const char* key, int& out, bool required, int min, int max) {
	const JsonValue* value = get(object, key, Type::Number, required);
	if (!value) {
		return false;
	}
	const double n = value->number;
	if (n != std::floor(n)) {
		fail(*value, "load.field_whole_number", {key});
		return false;
	}
	if (n < min || n > max) {
		fail(*value, "load.field_out_of_range", {key, std::to_string(min), std::to_string(max)});
		return false;
	}
	out = static_cast<int>(n);
	return true;
}

bool Reader::boolean(const JsonValue& object, const char* key, bool& out) {
	const JsonValue* value = get(object, key, Type::Bool, false);
	if (!value) {
		return false;
	}
	out = value->boolean;
	return true;
}

std::vector<const JsonValue*> Reader::objects(const JsonValue& object, const char* key, bool required) {
	std::vector<const JsonValue*> result;
	const JsonValue* array = get(object, key, Type::Array, required);
	if (!array) {
		return result;
	}
	for (const JsonValue& item : array->items) {
		if (item.type != Type::Object) {
			fail(item, "load.entries_objects", {key});
			continue;
		}
		result.push_back(&item);
	}
	return result;
}

std::vector<std::string> Reader::strings(const JsonValue& object, const char* key, bool required) {
	std::vector<std::string> result;
	const JsonValue* array = get(object, key, Type::Array, required);
	if (!array) {
		return result;
	}
	for (const JsonValue& item : array->items) {
		if (item.type != Type::String || item.string.empty()) {
			fail(item, "load.entries_non_empty_strings", {key});
			continue;
		}
		result.push_back(item.string);
	}
	return result;
}

void Reader::rejectUnknownKeys(const JsonValue& object, const DataKeySet& allowed) {
	for (std::size_t i = 0; i < object.keys.size(); ++i) {
		bool known = false;
		for (const std::string& key : allowed.keys) {
			if (object.keys[i] == key) {
				known = true;
				break;
			}
		}
		if (!known) {
			fail(object.values[i], "load.unknown_field", {object.keys[i]});
		}
	}
}

}
