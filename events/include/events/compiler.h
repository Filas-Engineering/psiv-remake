#pragma once

#include "events/diagnostics.h"
#include "events/program.h"
#include "events/registry.h"

#include <string>
#include <vector>

namespace events {

struct Source {
	std::string file;
	std::string text;
};

struct CompileOptions {
	// When set, every actor, marker, camera, layout, audio cue, animation,
	// portrait, item, party member, encounter, map, shop and rest point a
	// script names must be in it, and every flag and variable must be
	// declared there with the matching type. Unknown names are errors.
	const Registry* registry = nullptr;
};

struct CompileResult {
	Program program;
	std::vector<Diagnostic> diagnostics;

	bool ok() const { return !has_errors(diagnostics); }
};

// All sources form one program, so `call` may reach events in other files.
// The program is only usable when ok() is true.
CompileResult compile(const std::vector<Source>& sources, const CompileOptions& options = CompileOptions());

}
