#pragma once

#include "rules/json.h"

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace rules::loader {

// Field access with error collection. Every message carries the file, line and the entry being read.
class Reader {
public:
	Reader(std::string file, std::vector<std::string>& errors) : file_(std::move(file)), errors_(errors) {}

	void setContext(std::string context) { context_ = std::move(context); }
	void fail(const JsonValue& at, const std::string& message);

	const JsonValue* get(const JsonValue& object, const char* key, JsonValue::Type type, bool required);
	bool string(const JsonValue& object, const char* key, std::string& out, bool required = true);
	bool integer(const JsonValue& object, const char* key, int& out, bool required, int min, int max);
	bool boolean(const JsonValue& object, const char* key, bool& out);
	std::vector<const JsonValue*> objects(const JsonValue& object, const char* key, bool required);
	std::vector<std::string> strings(const JsonValue& object, const char* key, bool required);
	void rejectUnknownKeys(const JsonValue& object, std::initializer_list<const char*> allowed);

private:
	std::string file_;
	std::string context_;
	std::vector<std::string>& errors_;
};

}
