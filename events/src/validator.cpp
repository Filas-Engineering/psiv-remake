#include "validator.h"

#include <map>

namespace events {

namespace {

struct Walk {
	std::map<std::string, int> labels;
	std::vector<const Stmt*> gotos;
	std::vector<const Stmt*> calls;
	std::vector<std::pair<std::string, int>> duplicate_labels;
	bool changes_state = false;
};

bool is_state_change(const Instruction& ins) {
	switch (ins.op) {
	case Op::set_flag:
	case Op::clear_flag:
	case Op::var_set:
	case Op::var_add:
	case Op::give:
	case Op::take:
	case Op::party_add:
	case Op::party_remove:
	case Op::call:
		return true;
	case Op::action:
		return ins.action.type == ActionType::warp;
	default:
		return false;
	}
}

void walk(const std::vector<Stmt>& body, Walk& w) {
	for (const Stmt& s : body) {
		if (s.kind == StmtKind::label) {
			auto found = w.labels.find(s.ins.name);
			if (found != w.labels.end()) {
				w.duplicate_labels.push_back({s.ins.name, s.line});
			} else {
				w.labels[s.ins.name] = s.line;
			}
		} else if (s.kind == StmtKind::go_to) {
			w.gotos.push_back(&s);
		} else if (s.kind == StmtKind::simple) {
			if (s.ins.op == Op::call) {
				w.calls.push_back(&s);
			}
			if (is_state_change(s.ins)) {
				w.changes_state = true;
			}
		}
		for (const Branch& b : s.branches) {
			walk(b.body, w);
		}
	}
}

}

void validate(const std::vector<EventDecl>& events, const std::vector<std::string>& files, std::vector<Diagnostic>& diagnostics) {
	std::map<std::string, const EventDecl*> by_name;
	for (const EventDecl& ev : events) {
		auto found = by_name.find(ev.name);
		if (found != by_name.end()) {
			const EventDecl* first = found->second;
			diagnostics.push_back({Severity::error, files[ev.file], ev.line,
				"duplicate event '" + ev.name + "'; it is already defined at " + files[first->file] + ":" + std::to_string(first->line)});
		} else {
			by_name[ev.name] = &ev;
		}
	}

	for (const EventDecl& ev : events) {
		const std::string& file = files[ev.file];
		Walk w;
		walk(ev.body, w);
		for (const auto& dup : w.duplicate_labels) {
			diagnostics.push_back({Severity::error, file, dup.second,
				"duplicate label '" + dup.first + "'; it is already defined at line " + std::to_string(w.labels[dup.first])});
		}
		for (const Stmt* g : w.gotos) {
			if (!w.labels.count(g->ins.name)) {
				diagnostics.push_back({Severity::error, file, g->line, "goto: unknown label '" + g->ins.name + "' in event '" + ev.name + "'"});
			}
		}
		for (const Stmt* c : w.calls) {
			if (!by_name.count(c->ins.name)) {
				diagnostics.push_back({Severity::error, file, c->line, "call: unknown event '" + c->ins.name + "'"});
			}
		}
		if (ev.skippable && !ev.has_skip && w.changes_state) {
			diagnostics.push_back({Severity::warning, file, ev.line,
				"event '" + ev.name + "' is skippable and changes game state but has no 'on skip' block; skipping it would lose those changes"});
		}
	}
}

}
