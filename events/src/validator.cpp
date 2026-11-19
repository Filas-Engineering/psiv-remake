#include "validator.h"

#include <map>

namespace events {

namespace {

struct Walk {
	std::map<std::string, int> labels;
	std::vector<const Stmt*> gotos;
	std::vector<const Stmt*> calls;
	std::vector<std::pair<std::string, int>> duplicate_labels;
};

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
		} else if (s.kind == StmtKind::simple && s.ins.op == Op::call) {
			w.calls.push_back(&s);
		}
		for (const Branch& b : s.branches) {
			walk(b.body, w);
		}
	}
}

class RegistryCheck {
public:
	RegistryCheck(const Registry& registry, const std::string& file, std::vector<Diagnostic>& diagnostics)
		: registry_(registry), file_(file), diagnostics_(diagnostics) {}

	void block(const std::vector<Stmt>& body) {
		for (const Stmt& s : body) {
			statement(s);
			for (const Branch& b : s.branches) {
				if (b.has_condition) {
					condition(b.condition, b.line);
				}
				block(b.body);
			}
		}
	}

private:
	void error(int line, const std::string& message) {
		diagnostics_.push_back({Severity::error, file_, line, message});
	}

	std::string where() const {
		return registry_.file.empty() ? std::string(" in the registry") : " in " + registry_.file;
	}

	void name(NameKind kind, const std::string& value, int line) {
		if (!value.empty() && !registry_.has(kind, value)) {
			error(line, std::string("unknown ") + name_kind_name(kind) + " '" + value + "': it is not listed" + where());
		}
	}

	void declared(const std::string& value, bool is_flag, int line) {
		const Declaration* d = registry_.find(value);
		if (!d) {
			error(line, std::string(is_flag ? "flag" : "variable") + " '" + value + "' is not declared" + where());
		} else if (d->is_flag != is_flag) {
			error(line, "'" + value + "' is declared as a " + (d->is_flag ? "flag" : "variable") + ", not a " + (is_flag ? "flag" : "variable"));
		}
	}

	void condition(const Condition& c, int line) {
		for (const Term& t : c.terms) {
			declared(t.name, t.is_flag, line);
		}
	}

	void action(const Action& a, int line) {
		switch (a.type) {
		case ActionType::move:
			name(NameKind::actor, a.subject, line);
			if (!a.has_position) {
				name(NameKind::marker, a.target, line);
			}
			break;
		case ActionType::face:
			name(NameKind::actor, a.subject, line);
			if (!registry_.has(NameKind::actor, a.target) && !registry_.has(NameKind::marker, a.target)) {
				error(line, "unknown actor or marker '" + a.target + "': it is not listed" + where());
			}
			break;
		case ActionType::animate:
			name(NameKind::actor, a.subject, line);
			name(NameKind::animation, a.target, line);
			break;
		case ActionType::show_actor:
		case ActionType::hide_actor:
			name(NameKind::actor, a.subject, line);
			break;
		case ActionType::panel_show:
			name(NameKind::camera, a.target, line);
			name(NameKind::layout, a.slot, line);
			break;
		case ActionType::camera_cut:
		case ActionType::camera_move:
			name(NameKind::camera, a.subject, line);
			break;
		case ActionType::music_play:
		case ActionType::ambient_play:
		case ActionType::sound:
			name(NameKind::audio, a.subject, line);
			break;
		case ActionType::warp:
			name(NameKind::map, a.subject, line);
			name(NameKind::marker, a.target, line);
			break;
		case ActionType::panel_hide:
		case ActionType::panel_clear:
		case ActionType::fade_in:
		case ActionType::fade_out:
		case ActionType::shake:
		case ActionType::music_stop:
		case ActionType::music_fade:
		case ActionType::ambient_stop:
			break;
		}
	}

	void statement(const Stmt& s) {
		const Instruction& ins = s.ins;
		if (s.kind == StmtKind::while_loop) {
			condition(ins.condition, s.line);
			return;
		}
		if (s.kind != StmtKind::simple && s.kind != StmtKind::choice && s.kind != StmtKind::battle) {
			return;
		}
		switch (ins.op) {
		case Op::say:
			name(NameKind::actor, ins.dialogue.speaker, s.line);
			name(NameKind::portrait, ins.dialogue.portrait, s.line);
			break;
		case Op::choice:
			if (!ins.name.empty()) {
				declared(ins.name, ins.choice.yes_no, s.line);
			}
			break;
		case Op::set_flag:
		case Op::clear_flag:
			declared(ins.name, true, s.line);
			break;
		case Op::var_set:
		case Op::var_add:
			declared(ins.name, false, s.line);
			break;
		case Op::give:
		case Op::take:
			name(NameKind::item, ins.name, s.line);
			break;
		case Op::party_add:
		case Op::party_remove:
		case Op::name_entry:
			name(NameKind::member, ins.name, s.line);
			break;
		case Op::shop:
			name(NameKind::shop, ins.name, s.line);
			break;
		case Op::rest:
			name(NameKind::rest_point, ins.name, s.line);
			break;
		case Op::battle:
			name(NameKind::encounter, ins.battle.encounter, s.line);
			break;
		case Op::action:
			action(ins.action, s.line);
			break;
		default:
			break;
		}
	}

	const Registry& registry_;
	const std::string& file_;
	std::vector<Diagnostic>& diagnostics_;
};

}

void validate(const std::vector<EventDecl>& events, const std::vector<std::string>& files, const Registry* registry, std::vector<Diagnostic>& diagnostics) {
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
		if (registry) {
			RegistryCheck(*registry, file, diagnostics).block(ev.body);
		}
	}
}

}
