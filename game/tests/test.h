#pragma once

#include "game/data.h"
#include "game/session.h"

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

struct Registrar {
	Registrar(const char* name, void (*run)()) { registry().push_back({name, run}); }
};

template <class A, class B>
void checkEqual(const A& a, const B& b, const char* textA, const char* textB, const char* file, int line) {
	if (!(a == b)) {
		std::ostringstream s;
		s << textA << " == " << textB << "  (" << a << " vs " << b << ")";
		fail(file, line, s.str());
	}
}

// The adventure in data/, loaded once. Tests run from game/.
const game::GameData& data();
// An empty directory under build/ for one test's saves.
std::string freshDirectory(const std::string& name);
// Confirms every pending event line.
void confirmLines(game::Session& session);
// Starts a new game, then moves the leader to a marker with no visible enemies around.
void placeParty(game::Session& session, const std::string& area, const std::string& marker, game::Facing facing);
bool outputHas(const std::vector<game::Output>& output, const std::string& fragment);

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
