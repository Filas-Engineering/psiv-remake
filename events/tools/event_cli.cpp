#include "events/compiler.h"
#include "events/interpreter.h"
#include "events/recording_host.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace events;

namespace {

const int max_steps = 100000;

int usage() {
	std::fprintf(stderr,
		"usage:\n"
		"  event_cli check <file>...\n"
		"  event_cli run <file> <event> [--choose 1,0] [--battle win,lose,flee] [--skip-at STEP] [--dt SECONDS]\n");
	return 2;
}

bool read_file(const std::string& path, Source& source) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return false;
	}
	std::ostringstream text;
	text << in.rdbuf();
	source.file = path;
	source.text = text.str();
	return true;
}

std::vector<std::string> split(const std::string& list) {
	std::vector<std::string> parts;
	std::stringstream in(list);
	std::string part;
	while (std::getline(in, part, ',')) {
		parts.push_back(part);
	}
	return parts;
}

bool compile_files(const std::vector<std::string>& paths, CompileResult& result) {
	std::vector<Source> sources;
	for (const std::string& path : paths) {
		Source source;
		if (!read_file(path, source)) {
			std::fprintf(stderr, "%s: cannot read file\n", path.c_str());
			return false;
		}
		sources.push_back(source);
	}
	result = compile(sources);
	for (const Diagnostic& d : result.diagnostics) {
		std::fprintf(stderr, "%s\n", format_diagnostic(d).c_str());
	}
	return result.ok();
}

int check(const std::vector<std::string>& paths) {
	CompileResult result;
	if (!compile_files(paths, result)) {
		return 1;
	}
	std::printf("ok: %zu file(s), %zu event(s)\n", paths.size(), result.program.events.size());
	return 0;
}

int run(int argc, char** argv) {
	if (argc < 4) {
		return usage();
	}
	std::string path = argv[2];
	std::string event = argv[3];
	std::vector<int> choices;
	std::vector<BattleResult> battles;
	int skip_at = -1;
	double dt = 0.1;
	for (int i = 4; i < argc; ++i) {
		std::string arg = argv[i];
		if (i + 1 >= argc) {
			return usage();
		}
		std::string value = argv[++i];
		if (arg == "--choose") {
			for (const std::string& p : split(value)) {
				choices.push_back(std::atoi(p.c_str()));
			}
		} else if (arg == "--battle") {
			for (const std::string& p : split(value)) {
				if (p == "win") {
					battles.push_back(BattleResult::win);
				} else if (p == "lose") {
					battles.push_back(BattleResult::lose);
				} else if (p == "flee") {
					battles.push_back(BattleResult::flee);
				} else {
					std::fprintf(stderr, "--battle: unknown result '%s'\n", p.c_str());
					return 2;
				}
			}
		} else if (arg == "--skip-at") {
			skip_at = std::atoi(value.c_str());
		} else if (arg == "--dt") {
			dt = std::atof(value.c_str());
			if (dt <= 0) {
				std::fprintf(stderr, "--dt must be positive\n");
				return 2;
			}
		} else {
			return usage();
		}
	}

	CompileResult result;
	if (!compile_files({path}, result)) {
		return 1;
	}
	RecordingHost host;
	for (ActionType type : {ActionType::move, ActionType::face, ActionType::animate, ActionType::warp, ActionType::battle,
		     ActionType::panel_show, ActionType::panel_hide}) {
		host.set_duration(type, 0.5);
	}
	host.queue_choices(choices);
	host.queue_battle_results(battles);

	Interpreter interpreter(result.program, host);
	if (!interpreter.start(event)) {
		std::fprintf(stderr, "%s\n", interpreter.error().c_str());
		return 1;
	}
	std::size_t printed = 0;
	int step = 0;
	for (; step < max_steps && interpreter.status() == Status::running; ++step) {
		if (step == skip_at) {
			bool accepted = interpreter.skip();
			std::printf("[step %d] skip %s\n", step, accepted ? "accepted" : "refused");
		}
		host.advance(dt);
		interpreter.step(dt);
		for (; printed < host.trace.size(); ++printed) {
			std::printf("[step %d] %s\n", step, host.trace[printed].c_str());
		}
	}
	if (interpreter.status() == Status::error) {
		std::fprintf(stderr, "runtime error: %s\n", interpreter.error().c_str());
		return 1;
	}
	if (interpreter.status() != Status::finished) {
		std::fprintf(stderr, "the event did not finish within %d steps\n", max_steps);
		return 1;
	}
	std::printf("finished after %d step(s)\n", step);
	for (const auto& f : host.flags) {
		std::printf("flag %s = %s\n", f.first.c_str(), f.second ? "true" : "false");
	}
	for (const auto& v : host.vars) {
		std::printf("var %s = %lld\n", v.first.c_str(), static_cast<long long>(v.second));
	}
	for (const auto& it : host.items) {
		std::printf("item %s x%d\n", it.first.c_str(), it.second);
	}
	for (const std::string& member : host.party) {
		std::printf("party %s\n", member.c_str());
	}
	std::printf("player control %s\n", host.player_control ? "on" : "off");
	return 0;
}

}

int main(int argc, char** argv) {
	if (argc < 2) {
		return usage();
	}
	std::string command = argv[1];
	if (command == "check" && argc >= 3) {
		return check(std::vector<std::string>(argv + 2, argv + argc));
	}
	if (command == "run") {
		return run(argc, argv);
	}
	return usage();
}
