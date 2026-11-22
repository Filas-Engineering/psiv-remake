#pragma once

#include "events/program.h"

#include <string>
#include <vector>

namespace events {

// One command word of the script language with every form it takes (FORMAT.md). The parser takes the words it knows
// and where each may stand from this table, which the game's capabilities report also lists.
struct CommandInfo {
	std::string name;
	// Syntax of each form, in FORMAT.md's notation: <...> an argument, [...] optional, a|b one of.
	std::vector<std::string> forms;
	// Compiles in a background event.
	bool background = false;
	// Runs inside `together` and `race` (`music area` excepted).
	bool parallel = false;
};

// One test word of a condition besides a flag and a variable comparison.
struct ConditionInfo {
	std::string word;
	std::string form;
	TermTest test = TermTest::none;
};

// Every command, sorted by name.
const std::vector<CommandInfo>& script_commands();
// Null for a word that starts no command.
const CommandInfo* script_command(const std::string& name);
// The condition tests: `vehicle`, `aboard`, `party`, `item` and `money`, then the plain forms.
const std::vector<ConditionInfo>& condition_tests();

}
