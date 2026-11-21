#include "events/lint.h"

#include "lexer.h"
#include "parser.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace events {

namespace {

struct Location {
	int file = 0;
	int line = 0;
};

std::string number(double value) {
	char buffer[40];
	std::snprintf(buffer, sizeof buffer, "%.17g", value);
	return buffer;
}

// A text form that two statements share exactly when they do the same thing.
std::string canonical(const std::vector<Stmt>& body) {
	std::string out;
	for (const Stmt& s : body) {
		const Instruction& ins = s.ins;
		out += "{" + std::to_string(static_cast<int>(s.kind)) + " " + op_name(ins.op) + " " + ins.name + " " + std::to_string(ins.number) + " " +
			number(ins.seconds) + " " + describe(ins.dialogue) + " " + describe(ins.choice) + " " + describe(ins.condition) + " " +
			describe(ins.action) + " " + describe(ins.battle) + (ins.wait ? " wait" : " nowait");
		for (const Branch& b : s.branches) {
			out += " [" + std::string(b.present ? "1" : "0") + (b.has_condition ? describe(b.condition) : std::string()) + " " + canonical(b.body) + "]";
		}
		out += "}";
	}
	return out;
}

class Linter {
public:
	Linter(const std::vector<std::string>& files, std::vector<Diagnostic>& out) : files_(files), out_(out) {}

	void event(const EventDecl& ev) {
		block(ev.body);
	}

	void finish(const std::vector<EventDecl>& events, const LintOptions& options) {
		pair_up(flag_writes_, flag_reads_, "lint.flag_never_read", "lint.flag_never_set");
		pair_up(var_writes_, var_reads_, "lint.variable_never_read", "lint.variable_never_written");
		std::set<std::string> entries(options.entry_events.begin(), options.entry_events.end());
		for (const EventDecl& ev : events) {
			if (!calls_.count(ev.name) && !entries.count(ev.name)) {
				warn(ev.file, ev.line, "lint.event_never_called_another_event", {ev.name});
			}
		}
	}

private:
	void warn(int file, int line, const char* key, std::vector<std::string> params) {
		out_.push_back(diagnostic(Severity::warning, files_[file], line, key, std::move(params)));
	}

	void pair_up(const std::map<std::string, Location>& writes, const std::map<std::string, Location>& reads, const char* unread, const char* unwritten) {
		for (const auto& w : writes) {
			if (!reads.count(w.first)) {
				warn(w.second.file, w.second.line, unread, {w.first});
			}
		}
		for (const auto& r : reads) {
			if (!writes.count(r.first)) {
				warn(r.second.file, r.second.line, unwritten, {r.first});
			}
		}
	}

	static void note(std::map<std::string, Location>& where, const std::string& name, const Instruction& ins) {
		if (!where.count(name)) {
			where[name] = {ins.file, ins.source_line};
		}
	}

	void read(const Condition& condition, const Instruction& at) {
		for (const Term& t : condition.terms) {
			if (t.test == TermTest::none) {
				note(t.is_flag ? flag_reads_ : var_reads_, t.name, at);
			}
		}
	}

	void block(const std::vector<Stmt>& body) {
		const Stmt* ender = nullptr;
		for (const Stmt& s : body) {
			if (s.kind == StmtKind::label) {
				ender = nullptr;
			} else if (ender) {
				warn(s.ins.file, s.line, "lint.unreachable_nothing_jumps_here_after", {std::string((ender->kind == StmtKind::go_to ? "goto" : "return")), std::to_string(ender->line)});
				ender = nullptr;
			}
			statement(s);
			if (s.kind == StmtKind::go_to || (s.kind == StmtKind::simple && s.ins.op == Op::ret)) {
				ender = &s;
			}
		}
	}

	void statement(const Stmt& s) {
		const Instruction& ins = s.ins;
		switch (ins.op) {
		case Op::set_flag:
		case Op::clear_flag:
			note(flag_writes_, ins.name, ins);
			break;
		case Op::var_set:
		case Op::var_add:
			note(var_writes_, ins.name, ins);
			break;
		case Op::call:
			calls_.insert(ins.name);
			break;
		case Op::choice:
			if (!ins.name.empty()) {
				note(ins.choice.yes_no ? flag_writes_ : var_writes_, ins.name, ins);
			}
			break;
		default:
			break;
		}
		if (s.kind == StmtKind::while_loop) {
			read(ins.condition, ins);
		}
		for (const Branch& b : s.branches) {
			if (s.kind == StmtKind::if_chain && b.has_condition) {
				read(b.condition, ins);
			}
			block(b.body);
		}
		if (s.kind == StmtKind::choice) {
			identical_branches(s);
		}
	}

	void identical_branches(const Stmt& s) {
		// The recorded answer is itself an effect.
		if (!s.ins.name.empty()) {
			return;
		}
		std::vector<std::string> forms;
		for (const Branch& b : s.branches) {
			forms.push_back(canonical(b.body));
		}
		if (s.ins.choice.yes_no) {
			if (forms.size() == 2 && forms[0] == forms[1]) {
				warn(s.ins.file, s.line, "lint.yes_branches_this_ask_are", {});
			}
			return;
		}
		for (std::size_t i = 1; i < forms.size(); ++i) {
			for (std::size_t j = 0; j < i; ++j) {
				if (forms[i] == forms[j]) {
					warn(s.ins.file, s.branches[i].line, "lint.option_this_choice_has_same", {std::to_string(i + 1), std::to_string(j + 1)});
					break;
				}
			}
		}
	}

	const std::vector<std::string>& files_;
	std::vector<Diagnostic>& out_;
	std::map<std::string, Location> flag_writes_;
	std::map<std::string, Location> flag_reads_;
	std::map<std::string, Location> var_writes_;
	std::map<std::string, Location> var_reads_;
	std::set<std::string> calls_;
};

}

std::vector<Diagnostic> lint(const std::vector<Source>& sources, const LintOptions& options) {
	CompileResult compiled = compile(sources);
	if (!compiled.ok()) {
		return compiled.diagnostics;
	}
	std::vector<Diagnostic> ignored;
	std::vector<EventDecl> events;
	for (std::size_t i = 0; i < sources.size(); ++i) {
		std::vector<SourceLine> lines = tokenize(sources[i].file, sources[i].text, ignored);
		for (EventDecl& ev : parse(sources[i].file, static_cast<int>(i), lines, ignored)) {
			events.push_back(std::move(ev));
		}
	}
	std::vector<Diagnostic> out;
	Linter linter(compiled.program.files, out);
	for (const EventDecl& ev : events) {
		linter.event(ev);
	}
	linter.finish(events, options);
	std::stable_sort(out.begin(), out.end(), [](const Diagnostic& a, const Diagnostic& b) {
		return a.file != b.file ? a.file < b.file : a.line < b.line;
	});
	return out;
}

}
