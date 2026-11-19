#pragma once

#include <optional>
#include <string>
#include <utility>

namespace rules {

template <class T>
struct Result {
	std::optional<T> value;
	std::string error;

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
};

}
