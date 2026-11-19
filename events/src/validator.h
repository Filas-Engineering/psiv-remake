#pragma once

#include "ast.h"
#include "events/diagnostics.h"

#include <string>
#include <vector>

namespace events {

// Checks that need more than one line: labels, call targets, duplicate
// events, and skippable events that change state without an `on skip`.
void validate(const std::vector<EventDecl>& events, const std::vector<std::string>& files, std::vector<Diagnostic>& diagnostics);

}
