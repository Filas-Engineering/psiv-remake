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

CompileResult compile(const std::vector<Source>& sources) {
	CompileResult result;
	std::vector<EventDecl> events;
	std::uint64_t hash = 14695981039346656037ull;
	for (std::size_t i = 0; i < sources.size(); ++i) {
		const Source& source = sources[i];
		result.program.files.push_back(source.file);
		hash = fnv1a(fnv1a(hash, source.file), source.text);
		std::vector<SourceLine> lines = tokenize(source.file, source.text, result.diagnostics);
		std::vector<EventDecl> parsed = parse(source.file, static_cast<int>(i), lines, result.diagnostics);
		for (EventDecl& ev : parsed) {
			events.push_back(std::move(ev));
		}
	}
	validate(events, result.program.files, result.diagnostics);
	result.program.fingerprint = hash;
	if (result.ok()) {
		generate(events, result.program);
	}
	return result;
}

}
