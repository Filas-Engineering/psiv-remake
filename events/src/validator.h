#pragma once

#include "ast.h"
#include "events/diagnostics.h"
#include "events/registry.h"

#include <string>
#include <vector>

namespace events {

// Checks that need more than one line: labels, call targets and duplicate
// events. With a registry, also every name a script uses (EVT-003, EVT-004).
void validate(const std::vector<EventDecl>& events, const std::vector<std::string>& files, const Registry* registry, std::vector<Diagnostic>& diagnostics);

}
