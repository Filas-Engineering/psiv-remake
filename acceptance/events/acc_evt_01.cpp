#include "support.h"

#include "events/registry.h"
#include "events/strings.h"

#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>

using namespace events;

namespace {

CompileResult compileWith(const Registry& registry, const std::string& text) {
	CompileOptions options;
	options.registry = &registry;
	return compile({{"probe.ev", text}}, options);
}

std::string event(const std::string& body) {
	return "event probe unskippable\n" + body + "end\n";
}

// Exit code of event_cli, run from events/ like a build step.
int eventCli(const acc::Context& t, const std::string& arguments) {
	const std::string command = "cd '" + t.path("events") + "' && ./build/event_cli " + arguments + " >/dev/null 2>&1";
	const int status = std::system(command.c_str());
	return status == -1 ? -1 : WEXITSTATUS(status);
}

}

ACC_CHECK(acc_evt_01, "ACC-EVT-01", "EVT-003, EVT-004") {
	const Registry& registry = support::sampleRegistry(t);

	// Every probe names content the registry lists, except one name per error class.
	const std::string known = "\tmove guide to pier_end\n\tcamera cut pier_wide\n\tpanel show p1 camera pier_wide layout top_left\n\tsound bell_chime\n\tset harbor.met_guide\n";
	t.expect(compileWith(registry, event(known + "\tbattle lamp_room_moths\n\twin\n\tend\n")).ok(), "setup: a probe that names only registered content does not compile");
	const std::vector<std::pair<std::string, std::string>> probes = {
		{"actor", "\tmove acc_nobody to pier_end\n"},
		{"speaker", "\tsay acc_nobody @harbor.welcome\n"},
		{"camera", "\tcamera cut acc_no_camera\n"},
		{"panel layout", "\tpanel show p1 camera pier_wide layout acc_no_slot\n"},
		{"audio cue", "\tsound acc_no_cue\n"},
		{"music track", "\tmusic play acc_no_track\n"},
		{"enemy group", "\tbattle acc_no_group\n\twin\n\tend\n"},
		{"undeclared flag (write)", "\tset acc.never_declared\n"},
		{"undeclared flag (read)", "\tif acc.never_read\n\t\tsay guide @harbor.welcome\n\tend\n"},
		{"battle with no Victory branch", "\tbattle lamp_room_moths\n\tlose\n\t\tsay guide @harbor.welcome\n\tend\n"},
	};
	std::string rejected;
	for (const auto& [kind, body] : probes) {
		const bool ok = compileWith(registry, event(known + body)).ok();
		t.expect(!ok, "a script with an unknown " + kind + " compiled against the registry (EVT-003)");
		if (!ok) {
			rejected += (rejected.empty() ? "" : ", ") + kind;
		}
	}

	// An unknown string ID: the build's compile step with the string tables fails, through the CLI and the library.
	{
		CompileResult result = compileWith(registry, event("\tsay guide @acc.no_such_line\n"));
		std::vector<StringTable> tables;
		for (const char* file : {"events/sample_text/en.strings", "events/sample_text/ja.strings"}) {
			StringTable table;
			std::vector<Diagnostic> diagnostics;
			t.expect(load_string_table(file, support::readFile(t.path(file)), table, diagnostics), std::string("setup: cannot load ") + file);
			tables.push_back(table);
		}
		t.expect(result.ok() && has_errors(check_strings(result.program, tables)), "an unknown string ID passed the string check");
		const std::string probe = t.path("acceptance/build/acc_evt_01_probe.ev");
		if (FILE* f = std::fopen(probe.c_str(), "w")) {
			std::fputs(event("\tsay guide @acc.no_such_line\n").c_str(), f);
			std::fclose(f);
		}
		const std::string gate = "check --strict --registry sample_text/registry.txt --table sample_text/en.strings --table sample_text/ja.strings ";
		t.expect(eventCli(t, gate + "'" + probe + "'") == 1, "event_cli check with the registry and string tables accepted an unknown string ID (EVT-003)");
		std::string samples;
		for (const std::string& file : support::listFiles(t.path("events/samples"), ".ev")) {
			samples += " '" + file + "'";
		}
		t.expect(eventCli(t, gate + samples) == 0, "event_cli check with the registry and string tables rejects the sample scripts");
		std::remove(probe.c_str());
	}

	// EVT-004: flags and variables carry a type, a scope and a default.
	{
		Registry declared = registry;
		declared.declare_flag("acc.door_open", true, true);
		declared.declare_var("acc.coins", true, 7);
		declared.declare_flag("acc.met_keeper", false, false);
		declared.declare_var("acc.visits", false, 2);
		t.expect(!compileWith(declared, event("\tvar acc.door_open = 1\n")).ok(), "a flag used as a variable compiled (EVT-004)");
		t.expect(!compileWith(declared, event("\tset acc.coins\n")).ok(), "a variable used as a flag compiled (EVT-004)");

		const CompileResult result = compileWith(declared, event("\tif acc.door_open\n\t\tvar acc.coins += 1\n\tend\n\tset acc.met_keeper\n\tvar acc.visits += 1\n\tclear acc.door_open\n"));
		if (t.expect(result.ok(), "setup: the declared-flag probe does not compile")) {
			const Declaration* global = nullptr;
			for (const Declaration& d : result.program.declarations) {
				if (d.name == "acc.visits") {
					global = &d;
				}
			}
			t.expect(global != nullptr, "Program::declarations does not hand the host the global variable's default");
			for (int run = 0; run < 2; ++run) {
				RecordingHost host;
				Interpreter it(result.program, host);
				it.start("probe");
				support::run(it, host);
				t.expect(it.status() == Status::finished, "the declared-flag probe did not finish");
				t.expect(host.flags.count("acc.met_keeper") == 1 && host.flags["acc.met_keeper"], "a global flag did not reach the host's saved state (EVT-004)");
				t.expect(host.flags.count("acc.door_open") == 0 && host.vars.count("acc.coins") == 0, "an event-local flag or variable reached the host's saved state (EVT-004)");
				t.expect(host.vars.count("acc.visits") == 1, "a global variable did not reach the host");
			}
			// A local starts at its default each run and is kept in a mid-event save.
			RecordingHost host;
			host.set_line_duration(1.0);
			const CompileResult saved = compileWith(declared, event("\tvar acc.coins += 5\n\tsay guide @harbor.welcome\n\tif acc.coins == 12\n\t\tset acc.met_keeper\n\tend\n"));
			Interpreter it(saved.program, host);
			it.start("probe");
			host.advance(support::kStep);
			it.step(support::kStep);
			const SaveState state = it.save();
			t.expect(state.locals.count("acc.coins") == 1 && state.locals.at("acc.coins") == 12, "a mid-event save does not keep the local variable's value");
			RecordingHost loaded = host.reloaded();
			Interpreter resumed(saved.program, loaded);
			std::string error;
			t.expect(resumed.restore(state, error), "setup: restore failed: " + error);
			support::run(resumed, loaded);
			t.expect(loaded.flags["acc.met_keeper"], "a local started from its declared default of 7 did not read 12 after adding 5, across a save");
		}
	}

	t.note("against events/sample_text/registry.txt the compiler rejects an unknown " + rejected + "; an unknown string ID fails the library string check and event_cli check --strict with the registry and both tables, which pass the samples");
	t.note("flags and variables are typed (one used as the other fails to compile); global ones reach the host and carry their default in Program::declarations; local ones start at their default each run, never reach the host, and are kept in mid-event saves");
}
