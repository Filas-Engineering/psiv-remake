#pragma once

#include <string>
#include <vector>

namespace events {

enum class Severity { error, warning };

struct Diagnostic {
	Severity severity = Severity::error;
	std::string file;
	int line = 0;
	std::string message;
};

// "file:line: error: message", the shape most editors can jump to.
std::string format_diagnostic(const Diagnostic& diagnostic);
bool has_errors(const std::vector<Diagnostic>& diagnostics);

}
