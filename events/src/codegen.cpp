#include "codegen.h"

#include <map>
#include <string>

namespace events {

namespace {

class Generator {
public:
	explicit Generator(Program& program) : program_(program) {}

	void event(const EventDecl& ev) {
		labels_.clear();
		gotos_.clear();
		EventInfo info;
		info.name = ev.name;
		info.file = ev.file;
		info.line = ev.line;
		info.skippable = ev.skippable;
		info.entry = here();
		block(ev.body);
		emit_ret(ev.file, ev.line);
		if (ev.skippable) {
			info.skip_entry = here();
			block(ev.on_skip);
			emit_ret(ev.file, ev.has_skip ? ev.skip_line : ev.line);
		}
		for (int pc : gotos_) {
			code()[pc].target = labels_[code()[pc].name];
		}
		program_.events.push_back(info);
	}

	void link_calls() {
		for (Instruction& ins : code()) {
			if (ins.op == Op::call) {
				ins.target = program_.find_event(ins.name);
			}
		}
	}

private:
	std::vector<Instruction>& code() { return program_.code; }
	int here() const { return static_cast<int>(program_.code.size()); }

	int emit(const Instruction& ins) {
		code().push_back(ins);
		return here() - 1;
	}

	int emit_jump(const Instruction& like, Op op) {
		Instruction j;
		j.op = op;
		j.file = like.file;
		j.source_line = like.source_line;
		return emit(j);
	}

	void emit_ret(int file, int line) {
		Instruction r;
		r.op = Op::ret;
		r.file = file;
		r.source_line = line;
		emit(r);
	}

	// Each branch body jumps to the shared end, recorded in `ends` for patching.
	void branch_body(const Stmt& s, const Branch& b, std::vector<int>& ends) {
		block(b.body);
		ends.push_back(emit_jump(s.ins, Op::jump));
	}

	void block(const std::vector<Stmt>& body) {
		for (const Stmt& s : body) {
			statement(s);
		}
	}

	void statement(const Stmt& s) {
		switch (s.kind) {
		case StmtKind::simple:
			emit(s.ins);
			break;
		case StmtKind::label:
			labels_[s.ins.name] = here();
			break;
		case StmtKind::go_to: {
			int pc = emit_jump(s.ins, Op::jump);
			code()[pc].name = s.ins.name;
			gotos_.push_back(pc);
			break;
		}
		case StmtKind::if_chain: {
			std::vector<int> ends;
			for (std::size_t i = 0; i < s.branches.size(); ++i) {
				const Branch& b = s.branches[i];
				int test = -1;
				if (b.has_condition) {
					test = emit_jump(s.ins, Op::jump_unless);
					code()[test].condition = b.condition;
				}
				block(b.body);
				if (i + 1 < s.branches.size()) {
					ends.push_back(emit_jump(s.ins, Op::jump));
				}
				if (test >= 0) {
					code()[test].target = here();
				}
			}
			for (int pc : ends) {
				code()[pc].target = here();
			}
			break;
		}
		case StmtKind::while_loop: {
			int start = here();
			int test = emit_jump(s.ins, Op::jump_unless);
			code()[test].condition = s.ins.condition;
			block(s.branches[0].body);
			int back = emit_jump(s.ins, Op::jump);
			code()[back].target = start;
			code()[test].target = here();
			break;
		}
		case StmtKind::choice:
		case StmtKind::battle: {
			int head = emit(s.ins);
			std::vector<int> ends;
			std::vector<int> targets(s.branches.size(), -1);
			for (std::size_t i = 0; i < s.branches.size(); ++i) {
				if (s.branches[i].present) {
					targets[i] = here();
					branch_body(s, s.branches[i], ends);
				}
			}
			for (int& t : targets) {
				if (t < 0) {
					t = here();
				}
			}
			code()[head].targets = targets;
			for (int pc : ends) {
				code()[pc].target = here();
			}
			break;
		}
		case StmtKind::together: {
			int head = emit(s.ins);
			std::vector<int> targets;
			for (const Branch& b : s.branches) {
				targets.push_back(here());
				block(b.body);
				emit_jump(s.ins, Op::end_branch);
			}
			code()[head].targets = targets;
			code()[head].target = here();
			break;
		}
		case StmtKind::sequence:
			block(s.branches[0].body);
			break;
		}
	}

	Program& program_;
	std::map<std::string, int> labels_;
	std::vector<int> gotos_;
};

}

void generate(const std::vector<EventDecl>& events, Program& program) {
	Generator g(program);
	for (const EventDecl& ev : events) {
		g.event(ev);
	}
	g.link_calls();
}

}
