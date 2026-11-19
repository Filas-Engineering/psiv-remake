#pragma once

#include "events/compiler.h"
#include "events/interpreter.h"
#include "events/recording_host.h"

#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
	const char* name;
	void (*run)();
};

std::vector<Case>& registry();
void fail(const char* file, int line, const std::string& message);

struct Register {
	Register(const char* name, void (*run)()) { registry().push_back({name, run}); }
};

template <typename A, typename B>
void check_eq(const A& a, const B& b, const char* a_text, const char* b_text, const char* file, int line) {
	if (!(a == b)) {
		std::ostringstream out;
		out << a_text << " == " << b_text << "\n    left:  " << a << "\n    right: " << b;
		fail(file, line, out.str());
	}
}

std::string join(const std::vector<std::string>& lines);

// Compiles one source and fails the test on any diagnostic, warnings included.
events::Program compile_ok(const std::string& text);
// Compiles one source that must fail; returns the diagnostics.
std::vector<events::Diagnostic> compile_errors(const std::string& text);
bool has_diagnostic(const std::vector<events::Diagnostic>& diagnostics, int line, const std::string& fragment);

// Advances host and interpreter in fixed steps until the event stops
// running; returns the number of steps taken.
int run(events::Interpreter& interpreter, events::RecordingHost& host, double dt = 0.1, int max_steps = 10000);

}

#define TEST(name) \
	static void name(); \
	static test::Register name##_register(#name, name); \
	static void name()

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			test::fail(__FILE__, __LINE__, #cond); \
		} \
	} while (0)

#define CHECK_EQ(a, b) test::check_eq((a), (b), #a, #b, __FILE__, __LINE__)
