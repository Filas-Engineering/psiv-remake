#include "test.h"

#include "actor_data.h"

#include "events/compiler.h"
#include "game/save.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace test {

namespace {
int failures = 0;
}

std::vector<Case>& registry() {
	static std::vector<Case> cases;
	return cases;
}

void fail(const char* file, int line, const std::string& message) {
	++failures;
	std::cout << "  FAIL " << file << ":" << line << ": " << message << "\n";
}

const game::GameData& data() {
	static const rules::Result<game::GameData> loaded = game::loadGameData("data");
	if (!loaded.ok()) {
		std::cout << "cannot load data/: " << loaded.error << "\n";
		std::exit(1);
	}
	return *loaded.value;
}

std::string freshDirectory(const std::string& name) {
	const std::string path = "build/test_saves/" + name;
	std::error_code ignored;
	std::filesystem::remove_all(path, ignored);
	std::filesystem::create_directories(path, ignored);
	return path;
}

void confirmLines(game::Session& session) {
	while (session.linePending()) {
		session.confirmLine();
	}
}

void placeParty(game::Session& session, const std::string& area, const std::string& marker, game::Facing facing) {
	if (session.mode() == game::Mode::Title) {
		session.newGame();
		confirmLines(session);
	}
	game::World& w = session.world();
	w.area = area;
	w.position = session.data().area(area)->markers.at(marker);
	w.facing = facing;
	w.enemies.clear();
	session.drainOutput();
}

bool outputHas(const std::vector<game::Output>& output, const std::string& fragment) {
	for (const game::Output& o : output) {
		if (o.text.find(fragment) != std::string::npos) {
			return true;
		}
	}
	return false;
}

std::vector<rules::Event> autoRound(game::Session& session) {
	if (!session.fillFromAuto()) {
		session.fillFromPolicy();
	}
	return session.resolveRound();
}

void fightToTheEnd(game::Session& session) {
	for (int i = 0; i < 80 && session.mode() == game::Mode::Battle; ++i) {
		autoRound(session);
	}
}

namespace {

game::GameData replaceScript(const game::GameData& base, const std::string& directory, const std::string& file, const std::string& text) {
	game::GameData d = base;
	std::vector<events::Source> sources;
	bool replaced = false;
	for (const std::string& f : d.config.eventFiles) {
		std::string source;
		game::readFile(directory + "/" + f, source);
		if (f == file) {
			source = text;
			replaced = true;
		}
		sources.push_back({f, source});
	}
	if (!replaced) {
		sources.push_back({file, text});
	}
	events::CompileResult compiled = events::compile(sources);
	if (!compiled.ok()) {
		fail(__FILE__, __LINE__, "test scripts do not compile: " + events::format_diagnostic(compiled.diagnostics[0]));
	}
	d.program = compiled.program;
	return d;
}

}

game::GameData withScript(const std::string& file, const std::string& text) {
	return replaceScript(data(), "data", file, text);
}

const std::string& actorDataDirectory() {
	static const std::string directory = [] {
		const std::string root = freshDirectory("actor_data");
		const std::string problem = actor_data::make("data", "../core/data", "tests/fixtures/actors", root);
		if (!problem.empty()) {
			std::cout << "cannot make the actor data: " << problem << "\n";
			std::exit(1);
		}
		return root + "/game/data";
	}();
	return directory;
}

const game::GameData& actorData() {
	static const rules::Result<game::GameData> loaded = game::loadGameData(actorDataDirectory());
	if (!loaded.ok()) {
		std::cout << "cannot load the actor data: " << loaded.error << "\n";
		std::exit(1);
	}
	return *loaded.value;
}

game::GameData actorDataWithScript(const std::string& file, const std::string& text) {
	return replaceScript(actorData(), actorDataDirectory(), file, text);
}

const game::GameData& interactData() {
	static const rules::Result<game::GameData> loaded = [] {
		const std::string root = freshDirectory("interact_data");
		const std::string problem = actor_data::makeInteract("data", "../core/data", "tests/fixtures/actors", root);
		if (!problem.empty()) {
			std::cout << "cannot make the interaction data: " << problem << "\n";
			std::exit(1);
		}
		return game::loadGameData(root + "/game/data");
	}();
	if (!loaded.ok()) {
		std::cout << "cannot load the interaction data: " << loaded.error << "\n";
		std::exit(1);
	}
	return *loaded.value;
}

const std::string& backgroundDataDirectory() {
	static const std::string directory = [] {
		const std::string root = freshDirectory("background_data");
		const std::string problem = actor_data::makeBackground("data", "../core/data", "tests/fixtures/actors", root);
		if (!problem.empty()) {
			std::cout << "cannot make the background data: " << problem << "\n";
			std::exit(1);
		}
		return root + "/game/data";
	}();
	return directory;
}

const game::GameData& backgroundData() {
	static const rules::Result<game::GameData> loaded = game::loadGameData(backgroundDataDirectory());
	if (!loaded.ok()) {
		std::cout << "cannot load the background data: " << loaded.error << "\n";
		std::exit(1);
	}
	return *loaded.value;
}

game::GameData backgroundDataWithScript(const std::string& file, const std::string& text) {
	return replaceScript(backgroundData(), backgroundDataDirectory(), file, text);
}

const std::string& sceneDataDirectory() {
	static const std::string directory = [] {
		const std::string root = freshDirectory("scene_data");
		const std::string problem = actor_data::makeScene("data", "../core/data", "tests/fixtures/actors", root);
		if (!problem.empty()) {
			std::cout << "cannot make the scene data: " << problem << "\n";
			std::exit(1);
		}
		return root + "/game/data";
	}();
	return directory;
}

const game::GameData& sceneData() {
	static const rules::Result<game::GameData> loaded = game::loadGameData(sceneDataDirectory());
	if (!loaded.ok()) {
		std::cout << "cannot load the scene data: " << loaded.error << "\n";
		std::exit(1);
	}
	return *loaded.value;
}

const std::string& motionDataDirectory() {
	static const std::string directory = [] {
		const std::string root = freshDirectory("motion_data");
		const std::string problem = actor_data::makeMotion("data", "../core/data", "tests/fixtures/actors", root);
		if (!problem.empty()) {
			std::cout << "cannot make the motion data: " << problem << "\n";
			std::exit(1);
		}
		return root + "/game/data";
	}();
	return directory;
}

const game::GameData& motionData() {
	static const rules::Result<game::GameData> loaded = game::loadGameData(motionDataDirectory());
	if (!loaded.ok()) {
		std::cout << "cannot load the motion data: " << loaded.error << "\n";
		std::exit(1);
	}
	return *loaded.value;
}

}

int main(int argc, char** argv) {
	const std::string only = argc > 1 ? argv[1] : "";
	int ran = 0;
	for (const test::Case& c : test::registry()) {
		if (!only.empty() && std::string(c.name).find(only) == std::string::npos) {
			continue;
		}
		const int before = test::failures;
		c.run();
		++ran;
		std::cout << (test::failures == before ? "ok   " : "FAIL ") << c.name << "\n";
	}
	std::cout << ran << " tests, " << test::failures << " failures\n";
	return test::failures == 0 ? 0 : 1;
}
