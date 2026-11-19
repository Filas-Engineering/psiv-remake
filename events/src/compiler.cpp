#include "events/compiler.h"

#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "validator.h"

namespace events {

namespace {

std::uint64_t fnv1a(std::uint64_t hash, const std::string& data) {
	for (unsigned char c : data) {
		hash ^= c;
		hash *= 1099511628211ull;
	}
	// A separator so ("ab", "c") and ("a", "bc") hash differently.
	hash ^= 0xff;
	hash *= 1099511628211ull;
	return hash;
}

}

// Bumped whenever codegen lays out code differently, so saves made by an
// older compiler restart their event instead of resuming at a wrong pc.
const char* const code_layout = "events-code 2";

CompileResult compile(const std::vector<Source>& sources, const CompileOptions& options) {
	CompileResult result;
	std::vector<EventDecl> events;
	std::uint64_t hash = fnv1a(14695981039346656037ull, code_layout);
	for (std::size_t i = 0; i < sources.size(); ++i) {
		const Source& source = sources[i];
		result.program.files.push_back(source.file);
		hash = fnv1a(hash, source.file);
		std::vector<SourceLine> lines = tokenize(source.file, source.text, result.diagnostics);
		// Tokens rather than text, so edits to comments, blank lines and
		// indentation keep old saves resumable.
		for (const SourceLine& line : lines) {
			for (const Token& token : line.tokens) {
				hash = fnv1a(hash, std::string(1, static_cast<char>('0' + static_cast<int>(token.kind))) + token.text);
			}
			hash = fnv1a(hash, "\n");
		}
		std::vector<EventDecl> parsed = parse(source.file, static_cast<int>(i), lines, result.diagnostics);
		for (EventDecl& ev : parsed) {
			events.push_back(std::move(ev));
		}
	}
	validate(events, result.program.files, options.registry, result.diagnostics);
	if (options.registry) {
		result.program.declarations = options.registry->declarations;
		// Local values are saved by the interpreter, so a save means
		// something else once one changes.
		for (const Declaration& d : options.registry->declarations) {
			if (d.local) {
				hash = fnv1a(hash, (d.is_flag ? "local flag " : "local var ") + d.name + " " + std::to_string(d.initial));
			}
		}
	}
	result.program.fingerprint = hash;
	if (result.ok()) {
		generate(events, result.program);
	}
	return result;
}

}
