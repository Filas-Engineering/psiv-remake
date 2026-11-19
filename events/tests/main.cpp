#include "check.h"

#include <cstdio>

namespace test {

namespace {

int failures = 0;
bool current_failed = false;

}

std::vector<Case>& registry() {
	static std::vector<Case> cases;
	return cases;
}

void fail(const char* file, int line, const std::string& message) {
	std::printf("  %s:%d: check failed: %s\n", file, line, message.c_str());
	current_failed = true;
}

std::string join(const std::vector<std::string>& lines) {
	std::string out;
	for (const std::string& l : lines) {
		out += "      " + l + "\n";
	}
	return out;
}

events::Program compile_ok(const std::string& text) {
	events::CompileResult result = events::compile({{"test.ev", text}});
	for (const events::Diagnostic& d : result.diagnostics) {
		fail(__FILE__, __LINE__, "unexpected diagnostic: " + events::format_diagnostic(d));
	}
	return result.program;
}

std::vector<events::Diagnostic> compile_errors(const std::string& text) {
	events::CompileResult result = events::compile({{"test.ev", text}});
	if (result.ok()) {
		fail(__FILE__, __LINE__, "expected the script to fail to compile");
	}
	return result.diagnostics;
}

bool has_diagnostic(const std::vector<events::Diagnostic>& diagnostics, int line, const std::string& fragment) {
	for (const events::Diagnostic& d : diagnostics) {
		if (d.line == line && d.message.find(fragment) != std::string::npos) {
			return true;
		}
	}
	std::string all;
	for (const events::Diagnostic& d : diagnostics) {
		all += "\n    " + events::format_diagnostic(d);
	}
	std::printf("  no diagnostic at line %d containing \"%s\"; got:%s\n", line, fragment.c_str(), all.c_str());
	return false;
}

int run(events::Interpreter& interpreter, events::RecordingHost& host, double dt, int max_steps) {
	int steps = 0;
	while (interpreter.status() == events::Status::running && steps < max_steps) {
		host.advance(dt);
		interpreter.step(dt);
		++steps;
	}
	return steps;
}

}

int main() {
	for (const test::Case& c : test::registry()) {
		test::current_failed = false;
		c.run();
		if (test::current_failed) {
			++test::failures;
			std::printf("FAIL %s\n", c.name);
		} else {
			std::printf("ok   %s\n", c.name);
		}
	}
	std::printf("\n%zu tests, %d failed\n", test::registry().size(), test::failures);
	return test::failures == 0 ? 0 : 1;
}
