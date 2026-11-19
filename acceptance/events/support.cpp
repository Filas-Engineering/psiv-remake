#include "support.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>

using namespace events;

namespace support {

std::string readFile(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

std::vector<std::string> listFiles(const std::string& directory, const std::string& suffix) {
	std::vector<std::string> files;
	if (DIR* dir = opendir(directory.c_str())) {
		while (dirent* entry = readdir(dir)) {
			const std::string name = entry->d_name;
			if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
				files.push_back(directory + "/" + name);
			}
		}
		closedir(dir);
	}
	std::sort(files.begin(), files.end());
	return files;
}

std::vector<Source> sampleSources(const acc::Context& t) {
	std::vector<Source> sources;
	for (const std::string& file : listFiles(t.path("events/samples"), ".ev")) {
		sources.push_back({file.substr(file.rfind('/') + 1), readFile(file)});
	}
	return sources;
}

Program compileSamples(acc::Context& t) {
	CompileResult result = compile(sampleSources(t));
	if (!result.ok()) {
		std::string all;
		for (const Diagnostic& d : result.diagnostics) {
			all += format_diagnostic(d) + " ";
		}
		std::printf("FAIL\tthe sample scripts do not compile: %s\n", all.c_str());
		std::exit(0);
	}
	return result.program;
}

int DecisionHost::next(int count, const std::string& label) {
	const std::size_t index = optionCounts.size();
	optionCounts.push_back(count);
	labels.push_back(label);
	int pick = index < picks.size() ? picks[index] : 0;
	if (pickByLabel) {
		const auto found = byLabel.find(label);
		pick = found == byLabel.end() ? 0 : found->second;
	}
	given.push_back(pick);
	return pick;
}

int DecisionHost::pick_choice(const ChoiceRequest& request) {
	return next(request.yes_no ? 2 : static_cast<int>(request.options.size()), describe(request));
}

BattleResult DecisionHost::pick_battle(const BattleRequest& request) {
	std::vector<BattleResult> allowed = {BattleResult::win};
	if (request.defeat_handled) {
		allowed.push_back(BattleResult::lose);
	}
	if (request.escape_allowed) {
		allowed.push_back(BattleResult::flee);
	}
	const int pick = next(static_cast<int>(allowed.size()), describe(request));
	return allowed[static_cast<std::size_t>(pick) < allowed.size() ? pick : 0];
}

void configure(RecordingHost& host) {
	host.set_duration(ActionType::move, 0.4);
	host.set_duration(ActionType::face, 0.2);
	host.set_duration(ActionType::animate, 0.5);
	host.set_duration(ActionType::panel_show, 0.3);
	host.set_duration(ActionType::panel_hide, 0.3);
	host.set_duration(ActionType::warp, 0.5);
	host.set_line_duration(0.3);
	host.set_choice_duration(0.2);
	host.set_battle_duration(0.6);
}

void seed(RecordingHost& host, const World& world) {
	for (const std::string& flag : world) {
		host.flags[flag] = true;
	}
}

int run(Interpreter& interpreter, RecordingHost& host, int maxSteps) {
	int steps = 0;
	while (interpreter.status() == Status::running && steps < maxSteps) {
		host.advance(kStep);
		interpreter.step(kStep);
		++steps;
	}
	return steps;
}

namespace {

void explore(const Program& program, const std::string& event, const World& world, const std::vector<int>& prefix, std::vector<Path>& out) {
	DecisionHost host;
	configure(host);
	seed(host, world);
	host.picks = prefix;
	Interpreter interpreter(program, host);
	interpreter.start(event);
	run(interpreter, host);
	if (host.optionCounts.size() <= prefix.size()) {
		out.push_back({event, world, prefix});
		return;
	}
	for (int option = 0; option < host.optionCounts[prefix.size()]; ++option) {
		std::vector<int> longer = prefix;
		longer.push_back(option);
		explore(program, event, world, longer, out);
	}
}

}

std::vector<Path> allPaths(const Program& program, const std::vector<World>& worlds) {
	std::vector<Path> paths;
	for (const std::string& event : entryEvents(program)) {
		for (const World& world : worlds) {
			explore(program, event, world, {}, paths);
		}
	}
	return paths;
}

std::vector<std::string> entryEvents(const Program& program) {
	std::vector<bool> called(program.events.size(), false);
	for (const Instruction& ins : program.code) {
		if (ins.op == Op::call && ins.target >= 0) {
			called[static_cast<std::size_t>(ins.target)] = true;
		}
	}
	std::vector<std::string> entries;
	for (std::size_t i = 0; i < program.events.size(); ++i) {
		if (!called[i]) {
			entries.push_back(program.events[i].name);
		}
	}
	return entries;
}

std::string describe(const Path& path) {
	std::string out = path.event + " [";
	for (std::size_t i = 0; i < path.picks.size(); ++i) {
		out += (i ? "," : "") + std::to_string(path.picks[i]);
	}
	out += "]";
	for (const std::string& flag : path.world) {
		out += " +" + flag;
	}
	return out;
}

}
