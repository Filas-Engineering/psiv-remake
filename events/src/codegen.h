#pragma once

#include "ast.h"
#include "events/program.h"

#include <vector>

namespace events {

// Flattens validated events into program.code; only call it without errors.
void generate(const std::vector<EventDecl>& events, Program& program);

}
