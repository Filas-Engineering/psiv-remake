#pragma once

#include "events/host.h"

#include <cstdint>
#include <string>
#include <vector>

namespace events {

enum class Compare { eq, ne, lt, le, gt, ge };

// A term that tests the game instead of a flag or variable: `vehicle <id>` (the party has it), `aboard <id>` (the party
// rides it), `party <member>` (the member is in the active party) or `item <id>` (the party holds at least one).
enum class TermTest { none, owns, aboard, member, holds, money };

struct Term {
	bool negate = false;
	// A flag test when true, otherwise `name compare value` on a variable.
	bool is_flag = true;
	// Set for a test of the game; `name` is then the vehicle, member or item.
	TermTest test = TermTest::none;
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
	shop,
	rest,
	save_prompt,
	name_entry,
	vehicle_give,
	vehicle_place,
	vehicle_remove,
	learn,
	follower,
	money,
	equip,
	unequip,
	area_music,
	remove_actor,
	party_save,
	party_restore,
	party_swap,
	spawn,
	actor_look,
	party_order,
	credits,
	end_game,
};

const char* op_name(Op op);

struct Instruction {
	Op op = Op::ret;
	int file = 0;
	int source_line = 0;
	// choice: the variable (choice) or flag (ask) the answer is written to,
	// if any. shop, rest, name_entry: the shop, rest point or party member.
	// vehicle_*: the vehicle; vehicle_give and vehicle_place keep the map in
	// action.subject and the marker in action.target. learn: the member, with
	// the ability in action.subject. follower: the actor, empty for none.
	// money: the change in `number`. equip, unequip: the member, with the
	// piece in action.subject. area_music: the track, empty for the area's own.
	// remove_actor: the actor, with `number` 1 for `until reload`. party_save, party_restore: the snapshot
	// (`default` when the script names none). party_swap uses `swap` alone. spawn: the background event. actor_look: the
	// actor, with action.subject `enemy` and action.target the enemy, action.target alone the look, or both empty for the
	// game's own figure. party_add, party_remove: the member, with `number` 1 for `silent`. party_order: `names` alone. credits: the credits id.
	std::string name;
	// fork: 1 for a `race`, which ends when its first branch does.
	std::int64_t number = 0;
	double seconds = 0;
	Line dialogue;
	ChoiceRequest choice;
	Condition condition;
	Action action;
	BattleRequest battle;
	PartySwap swap;
	// party_order: the lineup members in their new order.
	std::vector<std::string> names;
	bool wait = true;
	// jump/jump_unless: destination pc. fork: join pc. call: callee event index.
	int target = -1;
	// choice: one pc per option. battle: win, lose, flee. fork: one pc per branch.
	std::vector<int> targets;
	// choice: the source line of each option.
	std::vector<int> option_lines;
};

struct EventInfo {
	std::string name;
	int file = 0;
	int line = 0;
	bool skippable = false;
	// Marked `background`: it runs beside the field without taking control, and only the commands FORMAT.md lists
	// under "Background events" compile in it.
	bool background = false;
	int entry = -1;
};

// A flag or variable declared in a registry (EVT-004). Local ones live in
// the interpreter for one run of an event and its calls, start at
// `initial` each time, and never reach the host. A host that seeds a new
// game can read the initial values of the global ones.
struct Declaration {
	std::string name;
	bool is_flag = true;
	bool local = false;
	std::int64_t initial = 0;
	int line = 0;
};

struct Program {
	std::vector<std::string> files;
	std::vector<EventInfo> events;
	std::vector<Instruction> code;
	// Hash of every source's file name and tokens, the compiler's code
	// layout version and the registry's local declarations, so comments,
	// blank lines and indentation do not change it. A save whose fingerprint differs
	// restarts its event instead of resuming (see Interpreter::restore).
	std::uint64_t fingerprint = 0;
	// Every flag and variable of the registry the program was compiled
	// with; empty without one.
	std::vector<Declaration> declarations;

	int find_event(const std::string& name) const;
};

}
