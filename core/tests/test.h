#pragma once

#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace test {

struct Case {
	const char* name;
	void (*run)();
};

std::vector<Case>& registry();
void fail(const char* file, int line, const std::string& message);

struct Registrar {
	Registrar(const char* name, void (*run)()) { registry().push_back({name, run}); }
};

template <class T>
std::string text(const T& value) {
	std::ostringstream s;
	if constexpr (std::is_enum_v<T>) {
		s << static_cast<int>(value);
	} else {
		s << value;
	}
	return s.str();
}

template <class T>
std::string text(const std::vector<T>& values) {
	std::string s = "{";
	for (std::size_t i = 0; i < values.size(); ++i) {
		s += (i ? ", " : "") + text(values[i]);
	}
	return s + "}";
}

template <class A, class B>
void checkEqual(const A& a, const B& b, const char* textA, const char* textB, const char* file, int line) {
	if (!(a == b)) {
		fail(file, line, std::string(textA) + " == " + textB + "  (" + text(a) + " vs " + text(b) + ")");
	}
}

}

#define TEST(name) \
	static void name(); \
	static ::test::Registrar name##Registrar(#name, name); \
	static void name()

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			::test::fail(__FILE__, __LINE__, #condition); \
		} \
	} while (0)

#define CHECK_EQ(a, b) ::test::checkEqual((a), (b), #a, #b, __FILE__, __LINE__)
