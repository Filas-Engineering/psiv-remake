#include "support.h"

#include "game/runner.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace support {

namespace {

std::uint64_t fnv(const std::string& text) {
	std::uint64_t hash = 1469598103934665603ULL;
	for (unsigned char c : text) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	return hash;
}

}

const game::GameData& data(const acc::Context& t) {
	static const rules::Result<game::GameData> loaded = game::loadGameData(t.path("game/data"));
	if (!loaded.ok()) {
		std::string reason = "game/data does not load: " + loaded.error;
		for (char& c : reason) {
			c = c == '\n' ? ' ' : c;
		}
		std::printf("FAIL\t%s\n", reason.c_str());
		std::exit(0);
	}
	return *loaded.value;
}

std::string tempDirectory(const std::string& name) {
	const std::filesystem::path path = std::filesystem::temp_directory_path() / ("acceptance-game-" + name);
	std::error_code ignored;
	std::filesystem::remove_all(path, ignored);
	std::filesystem::create_directories(path, ignored);
	return path.string();
}

game::SessionOptions options(const std::string& saveDirectory) {
	game::SessionOptions o;
	o.saveDirectory = saveDirectory;
	o.seed = 7;
	return o;
}

void playThrough(const acc::Context& t, game::Session& session, const std::function<void(const std::string& line)>& after) {
	std::string script;
	game::readFile(t.path("game/data/scripts/playthrough.txt"), script);
	std::ostringstream transcript;
	game::RunnerOptions r;
	r.autoBattle = true;
	r.echo = false;
	game::Runner runner(session, transcript, r);
	runner.settle();
	std::istringstream in(script);
	std::string line;
	while (std::getline(in, line)) {
		const std::string command = line.substr(0, line.find('#'));
		if (command.find_first_not_of(" \t") == std::string::npos) {
			continue;
		}
		const int errors = runner.errors();
		if (!runner.execute(line)) {
			break;
		}
		if (runner.errors() == errors) {
			after(line);
		}
	}
}

std::string resign(const std::string& text) {
	const std::string body = text.substr(0, text.rfind("\nend ") + 1);
	char sum[17];
	std::snprintf(sum, sizeof sum, "%016llx", static_cast<unsigned long long>(fnv(body)));
	return body + "end " + sum + "\n";
}

std::string withoutWriteTime(const std::string& text) {
	std::istringstream in(text.substr(0, text.rfind("\nend ") + 1));
	std::string out;
	std::string line;
	bool header = false;
	while (std::getline(in, line)) {
		if (line.compare(0, 13, "chunk header ") == 0) {
			header = true;
		} else if (line.compare(0, 6, "chunk ") == 0) {
			header = false;
		}
		if (header && line.compare(0, 8, "written ") == 0) {
			continue;
		}
		// The header chunk's byte count changes with the write time's digits.
		out += line.compare(0, 13, "chunk header ") == 0 ? "chunk header\n" : line + "\n";
	}
	return out;
}

}
