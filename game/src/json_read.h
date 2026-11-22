#pragma once

#include "game/world.h"

#include "rules/data_keys.h"
#include "rules/json.h"
#include "rules/result.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace game {

// Typed field access that collects every problem instead of stopping at the first.
class JsonReader {
public:
	explicit JsonReader(std::string file) : file_(std::move(file)) {}

	void fail(const rules::JsonValue& at, const char* key, std::vector<std::string> params);
	bool ok() const { return problems_.empty(); }
	const std::vector<rules::Problem>& problems() const { return problems_; }
	// One "file:line: message" per problem.
	std::string errors() const;

	// Rejects keys outside `known`, which catches typos in data.
	void only(const rules::JsonValue& object, const rules::DataKeySet& known);

	const rules::JsonValue* field(const rules::JsonValue& object, const char* key, rules::JsonValue::Type type, bool required);
	void text(const rules::JsonValue& object, const char* key, std::string& out, bool required);
	void integer(const rules::JsonValue& object, const char* key, int& out, bool required, int min, int max);
	void number(const rules::JsonValue& object, const char* key, double& out, bool required, double min, double max);
	void boolean(const rules::JsonValue& object, const char* key, bool& out, bool required);
	void texts(const rules::JsonValue& object, const char* key, std::vector<std::string>& out, bool required);
	void counts(const rules::JsonValue& object, const char* key, std::map<std::string, int>& out, bool required, int min, int max);
	// A cell is written [x, y].
	bool cell(const rules::JsonValue& value, Cell& out);
	void cellField(const rules::JsonValue& object, const char* key, Cell& out, bool required);

private:
	std::string file_;
	std::vector<rules::Problem> problems_;
};

}
