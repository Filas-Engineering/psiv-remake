#pragma once

#include "rules/data_keys.h"

#include "rules/json.h"

#include <string>
#include <utility>
#include <vector>

namespace rules::loader {

// Field access with problem collection. Every problem carries the file, line and the entry being read.
class Reader {
public:
	Reader(std::string file, std::vector<Problem>& problems) : file_(std::move(file)), problems_(problems) {}

	void setContext(std::string context) { context_ = std::move(context); }
	void fail(const JsonValue& at, const char* key, std::vector<std::string> params);

	const JsonValue* get(const JsonValue& object, const char* key, JsonValue::Type type, bool required);
	bool string(const JsonValue& object, const char* key, std::string& out, bool required = true);
	bool integer(const JsonValue& object, const char* key, int& out, bool required, int min, int max);
	bool boolean(const JsonValue& object, const char* key, bool& out);
	std::vector<const JsonValue*> objects(const JsonValue& object, const char* key, bool required);
	std::vector<std::string> strings(const JsonValue& object, const char* key, bool required);
	void rejectUnknownKeys(const JsonValue& object, const DataKeySet& allowed);

private:
	std::string file_;
	std::string context_;
	std::vector<Problem>& problems_;
};

}
