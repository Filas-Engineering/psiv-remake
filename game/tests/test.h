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
// One round the way the terminal plays it: the auto macro while it runs, otherwise core's policy.
std::vector<rules::Event> autoRound(game::Session& session);
void fightToTheEnd(game::Session& session);
// The adventure with one script file replaced; compiled without the registry, so tests can use any names.
game::GameData withScript(const std::string& file, const std::string& text);
// The adventure with Wren Hollow added (tests/actor_data.h), in build/test_saves/actor_data/game/data, loaded once.
const std::string& actorDataDirectory();
const game::GameData& actorData();
// The same with one script file replaced, as withScript does.
game::GameData actorDataWithScript(const std::string& file, const std::string& text);
// The adventure with Wren Hollow, Wren Loft and Wren Cellar and game.json's interaction rules turned on
// (actor_data::makeInteract), in build/test_saves/interact_data/game/data, loaded once.
const game::GameData& interactData();
// The actor data with Wren Yard added (actor_data::makeBackground), in build/test_saves/background_data/game/data, loaded
// once; and with one script file replaced, as withScript does.
const std::string& backgroundDataDirectory();
const game::GameData& backgroundData();
game::GameData backgroundDataWithScript(const std::string& file, const std::string& text);
// The actor data with Wren Gallery added (actor_data::makeScene), in build/test_saves/scene_data/game/data, loaded once.
const std::string& sceneDataDirectory();
const game::GameData& sceneData();
// The actor data with Wren Mill added (actor_data::makeMotion), in build/test_saves/motion_data/game/data, loaded once.
const std::string& motionDataDirectory();
const game::GameData& motionData();

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
