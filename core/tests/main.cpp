#include "test.h"

#include <cstdio>
#include <cstring>

namespace test {

namespace {

int failures = 0;
const char* current = "";

}

std::vector<Case>& registry() {
	static std::vector<Case> cases;
	return cases;
}

void fail(const char* file, int line, const std::string& message) {
	++failures;
	std::printf("FAIL %s (%s:%d): %s\n", current, file, line, message.c_str());
}

}

int main(int argc, char** argv) {
	const char* filter = argc > 1 ? argv[1] : nullptr;
	int ran = 0;
	for (const test::Case& c : test::registry()) {
		if (filter && !std::strstr(c.name, filter)) {
			continue;
		}
		test::current = c.name;
		c.run();
		++ran;
	}
	if (test::failures > 0) {
		std::printf("%d check(s) failed across %d tests\n", test::failures, ran);
		return 1;
	}
	std::printf("all %d tests passed\n", ran);
	return 0;
}
