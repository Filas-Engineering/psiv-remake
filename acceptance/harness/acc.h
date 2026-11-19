#pragma once

#include <string>
#include <utility>
#include <vector>

namespace acc {

// One run of one acceptance check. A failed expectation means the library
// contradicts the spec (FAIL). A gap means the library does not implement
// part of the behavior yet (NOT IMPLEMENTED). Notes travel with a PASS.
class Context {
public:
	explicit Context(std::string root) : root_(std::move(root)) {}

	const std::string& root() const { return root_; }
	std::string path(const std::string& relative) const { return root_ + "/" + relative; }

	bool expect(bool ok, const std::string& what);
	void gap(const std::string& what);
	void note(const std::string& what);

	int failureCount() const { return failureCount_; }
	const std::vector<std::string>& failures() const { return failures_; }
	const std::vector<std::string>& gaps() const { return gaps_; }
	const std::vector<std::string>& notes() const { return notes_; }

private:
	std::string root_;
	int failureCount_ = 0;
	std::vector<std::string> failures_;
	std::vector<std::string> gaps_;
	std::vector<std::string> notes_;
};

struct Check {
	const char* id;
	// Comma-separated requirement IDs, exactly as the check's Verifies column expands.
	const char* verifies;
	void (*run)(Context&);
};

std::vector<Check>& registry();

struct Register {
	Register(const char* id, const char* verifies, void (*run)(Context&)) { registry().push_back({id, verifies, run}); }
};

}

#define ACC_CHECK(function, id, verifies) \
	static void function(acc::Context&); \
	static acc::Register function##Register(id, verifies, function); \
	static void function(acc::Context& t)
