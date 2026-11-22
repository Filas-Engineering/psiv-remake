#pragma once

#include "events/program.h"

#include <string>
#include <vector>

namespace events {

enum class StmtKind { simple, if_chain, while_loop, choice, battle, together, sequence, label, go_to };

struct Stmt;

struct Branch {
	int line = 0;
	bool present = false;
	bool has_condition = false;
	Condition condition;
	std::vector<Stmt> body;
};

struct Stmt {
	StmtKind kind = StmtKind::simple;
	int line = 0;
	// For simple statements the finished instruction; for blocks the header
	// payload (choice text, battle action, loop condition, label name).
	Instruction ins;
	// if_chain: one per if / else if / else. while_loop, sequence: one body.
	// choice: one per option (ask: yes, no). battle: win, lose, flee.
	// together and race: one per parallel branch.
	std::vector<Branch> branches;
};

struct EventDecl {
	std::string name;
	int file = 0;
	int line = 0;
	bool skippable = false;
	bool background = false;
	std::vector<Stmt> body;
};

}
