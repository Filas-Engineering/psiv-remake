#include "support.h"

#include "events/strings.h"

using namespace events;

namespace {

bool compiles(const std::string& text) {
	return compile({{"probe.ev", text}}).ok();
}

}

ACC_CHECK(acc_evt_01, "ACC-EVT-01", "EVT-003, EVT-004") {
	// Each probe uses a name no data file declares. A compiler that knows the game's registries rejects it.
	const std::vector<std::pair<std::string, std::string>> registryProbes = {
		{"actor", "event probe unskippable\n\tmove acc_nobody to pier_end\nend\n"},
		{"camera", "event probe unskippable\n\tcamera cut acc_no_camera\nend\n"},
		{"panel layout", "event probe unskippable\n\tpanel show p1 camera pier_wide layout acc_no_slot\nend\n"},
		{"audio cue", "event probe unskippable\n\tsound acc_no_cue\n\tmusic play acc_no_track\nend\n"},
		{"enemy group", "event probe unskippable\n\tbattle acc_no_group\n\twin\n\tend\nend\n"},
		{"undeclared flag", "event probe unskippable\n\tset acc.never_declared\n\tif acc.never_read\n\t\tsay guide \"Odd.\"\n\tend\nend\n"},
	};
	std::string accepted;
	for (const auto& [kind, text] : registryProbes) {
		if (compiles(text)) {
			accepted += (accepted.empty() ? "" : ", ") + kind;
		}
	}

	// A battle with no Victory branch fails to compile.
	t.expect(!compiles("event probe unskippable\n\tbattle lamp_room_moths\n\tlose\n\t\tsay guide \"Again.\"\n\tend\nend\n"), "a battle with no win section compiled");

	// An unknown string ID fails the ahead-of-time string check against the shipped tables.
	{
		CompileResult result = compile({{"probe.ev", "event probe unskippable\n\tsay guide @acc.no_such_line\nend\n"}});
		std::vector<StringTable> tables;
		for (const char* file : {"events/sample_text/en.strings", "events/sample_text/ja.strings"}) {
			StringTable table;
			std::vector<Diagnostic> diagnostics;
			t.expect(load_string_table(file, support::readFile(t.path(file)), table, diagnostics), std::string("setup: cannot load ") + file);
			tables.push_back(table);
		}
		t.expect(result.ok() && has_errors(check_strings(result.program, tables)), "an unknown string ID passed the string check");
	}

	if (!accepted.empty()) {
		t.gap("the compiler takes no registry of actors, cameras, panel layouts, audio cues or enemy groups, so unknown ones compile (accepted: " + accepted + ") (EVT-003)");
	}
	t.gap("there is no flag registry with type, scope and default; any flag name compiles and reads as false (EVT-004)");
	t.note("a battle with no win section is a compile error; an unknown string ID is an error in the ahead-of-time string check");
}
