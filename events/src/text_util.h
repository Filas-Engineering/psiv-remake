#pragma once

#include "events/program.h"

#include <string>
#include <vector>

namespace events {

// The script rule for names: letters, digits, '_' and '.', not starting
// with a digit.
bool is_name(const std::string& text);
std::string trim(const std::string& text);
std::vector<std::string> split_lines(const std::string& text);
// The event that owns each pc, relying on codegen laying events out in order.
std::vector<int> event_of_pc(const Program& program);

}
