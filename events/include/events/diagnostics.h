#pragma once

#include <map>
#include <string>
#include <vector>

namespace events {

enum class Severity { error, warning };

// A text key and its parameters. diagnostic_text() has the English template of every key events uses.
struct DiagnosticMessage {
	std::string key;
	std::vector<std::string> params;

	bool empty() const { return key.empty(); }
};

struct Diagnostic {
	Severity severity = Severity::error;
	std::string file;
	int line = 0;
	// English, rendered from key and params.
	std::string message;
	std::string key;
	std::vector<std::string> params;
};

// "file:line: error: message", the shape most editors can jump to.
std::string format_diagnostic(const Diagnostic& diagnostic);
bool has_errors(const std::vector<Diagnostic>& diagnostics);

// Key to English template; a template names its parameters by position: {0}, {1} and so on.
const std::map<std::string, std::string>& diagnostic_text();
// The template of `key` from `table` with each {N} replaced by params[N]; a missing key gives the key itself.
std::string format_message(const std::map<std::string, std::string>& table, const std::string& key, const std::vector<std::string>& params);
Diagnostic diagnostic(Severity severity, std::string file, int line, std::string key, std::vector<std::string> params);
Diagnostic diagnostic(Severity severity, std::string file, int line, const DiagnosticMessage& message);

}
