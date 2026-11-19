#pragma once

#include "events/host.h"

#include <cstdint>
#include <string>
#include <vector>

namespace events {

enum class Compare { eq, ne, lt, le, gt, ge };

struct Term {
	bool negate = false;
	// A flag test when true, otherwise `name compare value` on a variable.
	bool is_flag = true;
	std::string name;
	Compare compare = Compare::eq;
	std::int64_t value = 0;
};

enum class Join { all, any };

struct Condition {
	Join join = Join::all;
	std::vector<Term> terms;
};

// Canonical text of a condition; two conditions with equal text test the same thing.
std::string describe(const Condition& condition);

enum class Op {
	say,
	choice,
	set_flag,
	clear_flag,
	var_set,
	var_add,
	jump,
	jump_unless,
	action,
	battle,
	wait,
	sync,
	call,
	ret,
	give,
	take,
	party_add,
	party_remove,
	lock_player,
	unlock_player,
	fork,
	end_branch,
};

const char* op_name(Op op);

struct Instruction {
	Op op = Op::ret;
	int file = 0;
	int source_line = 0;
	std::string name;
	std::int64_t number = 0;
	double seconds = 0;
	Line dialogue;
	ChoiceRequest choice;
	Condition condition;
	Action action;
	bool wait = true;
	// jump/jump_unless: destination pc. fork: join pc. call: callee event index.
	int target = -1;
	// choice: one pc per option. battle: win, lose, flee. fork: one pc per branch.
	std::vector<int> targets;
};

struct EventInfo {
	std::string name;
	int file = 0;
	int line = 0;
	bool skippable = false;
	int entry = -1;
	int skip_entry = -1;
};

struct Program {
	std::vector<std::string> files;
	std::vector<EventInfo> events;
	std::vector<Instruction> code;
	// Hash of every source; saved states only restore into the same program.
	std::uint64_t fingerprint = 0;

	int find_event(const std::string& name) const;
};

}
