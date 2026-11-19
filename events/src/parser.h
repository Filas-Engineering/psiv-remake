#pragma once

#include "ast.h"
#include "events/diagnostics.h"
#include "lexer.h"

#include <string>
#include <vector>

namespace events {

// Checks syntax, argument counts and types, block structure and where each
// command may appear. Cross-event checks live in validator.h.
std::vector<EventDecl> parse(const std::string& file, int file_index, const std::vector<SourceLine>& lines, std::vector<Diagnostic>& diagnostics);

}
