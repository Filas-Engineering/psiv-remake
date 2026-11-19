#pragma once

#include "events/compiler.h"
#include "events/diagnostics.h"

#include <string>
#include <vector>

namespace events {

struct LintOptions {
	// Events the game starts itself (map triggers, menus, NPC talk). Any
	// other event that no script calls is reported as unused.
	std::vector<std::string> entry_events;
};

// Static checks on scripts that compile: flags and variables written but
// never read or read but never written, events never called, choices with
// identical branches, and commands after a `return` or `goto` that nothing
// jumps to. Findings are warnings. When the sources do not compile, the
// compile diagnostics come back instead.
std::vector<Diagnostic> lint(const std::vector<Source>& sources, const LintOptions& options);

}
