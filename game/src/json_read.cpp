#include "json_read.h"

#include "game/data.h"

#include "rules/text.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace game {

using rules::JsonValue;

void JsonReader::fail(const JsonValue& at, const char* key, std::vector<std::string> params) {
	problems_.push_back({file_, at.line, "", {key, std::move(params)}});
}

std::string JsonReader::errors() const {
	return rules::formatProblems(problems_, problemText());
}

void JsonReader::only(const JsonValue& object, const rules::DataKeySet& known) {
	for (std::size_t i = 0; i < object.keys.size(); ++i) {
		if (std::find(known.keys.begin(), known.keys.end(), object.keys[i]) == known.keys.end()) {
			fail(object.values[i], "data.unknown_field", {object.keys[i]});
		}
	}
}

const JsonValue* JsonReader::field(const JsonValue& object, const char* key, JsonValue::Type type, bool required) {
	const JsonValue* value = object.find(key);
	if (!value) {
		if (required) {
			fail(object, "data.missing_field", {key});
		}
		return nullptr;
	}
	if (!value->is(type)) {
		fail(*value, "data.field_wrong_type", {key, std::string(rules::jsonTypeName(type))});
		return nullptr;
	}
	return value;
}

void JsonReader::text(const JsonValue& object, const char* key, std::string& out, bool required) {
	if (const JsonValue* v = field(object, key, JsonValue::Type::String, required)) {
		if (v->string.empty()) {
			fail(*v, "data.field_cannot_empty", {key});
			return;
		}
		out = v->string;
	}
}

void JsonReader::integer(const JsonValue& object, const char* key, int& out, bool required, int min, int max) {
	if (const JsonValue* v = field(object, key, JsonValue::Type::Number, required)) {
		if (std::floor(v->number) != v->number || v->number < min || v->number > max) {
			fail(*v, "data.field_out_of_range", {key, std::to_string(min), std::to_string(max)});
			return;
		}
		out = static_cast<int>(v->number);
	}
}

void JsonReader::number(const JsonValue& object, const char* key, double& out, bool required, double min, double max) {
	if (const JsonValue* v = field(object, key, JsonValue::Type::Number, required)) {
		if (v->number < min || v->number > max) {
			std::ostringstream low;
			std::ostringstream high;
			low << min;
			high << max;
			fail(*v, "data.number_out_of_range", {key, low.str(), high.str()});
			return;
		}
		out = v->number;
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
				fail(item, "data.holds_text_only", {key});
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
				fail(n, "data.count_out_of_range", {key, v->keys[i], std::to_string(min), std::to_string(max)});
				continue;
			}
			out[v->keys[i]] = static_cast<int>(n.number);
		}
	}
}

bool JsonReader::cell(const JsonValue& value, Cell& out) {
	if (!value.is(JsonValue::Type::Array) || value.items.size() != 2 || !value.items[0].is(JsonValue::Type::Number) ||
		!value.items[1].is(JsonValue::Type::Number)) {
		fail(value, "data.bad_cell", {});
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
