#pragma once

#include "rules/message.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rules {

// One problem found while reading data: where it is, and a text key with its parameters. problemText() in
// text.h has the English template of every key core uses.
struct Problem {
	std::string file;
	// 0 when the problem is about the whole file.
	int line = 0;
	// The entry being read, such as "abilities[spark]", or empty.
	std::string entry;
	Message message;
};

template <class T>
struct Result {
	std::optional<T> value;
	// English, one problem per line.
	std::string error;
	// The same problems as keys and parameters. Filled by the data loaders; other failures leave it empty.
	std::vector<Problem> problems;

	bool ok() const { return value.has_value(); }

	static Result success(T v) {
		Result r;
		r.value.emplace(std::move(v));
		return r;
	}

	static Result failure(std::string message) {
		Result r;
		r.error = std::move(message);
		return r;
	}

	static Result failure(std::vector<Problem> problems, std::string text) {
		Result r;
		r.problems = std::move(problems);
		r.error = std::move(text);
		return r;
	}
};

}
