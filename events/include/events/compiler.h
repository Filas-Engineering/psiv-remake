#pragma once

#include "events/diagnostics.h"
#include "events/program.h"

#include <string>
#include <vector>

namespace events {

struct Source {
	std::string file;
	std::string text;
};

struct CompileResult {
	Program program;
	std::vector<Diagnostic> diagnostics;

	bool ok() const { return !has_errors(diagnostics); }
};

// All sources form one program, so `call` may reach events in other files.
// The program is only usable when ok() is true.
CompileResult compile(const std::vector<Source>& sources);

}
