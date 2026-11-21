#include "validator.h"

#include <map>
#include <set>

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

	// The party's map is followed through the event so a move or warp to a
	// marker on another map is an error. Returns the map after the body, or
	// "" where it cannot be known.
	std::string block(const std::vector<Stmt>& body, std::string map) {
		for (const Stmt& s : body) {
			if (s.kind == StmtKind::label) {
				map.clear();
			}
			map = statement(s, map);
			if (s.branches.empty()) {
				continue;
			}
			const std::string start = s.kind == StmtKind::while_loop && changes_map(s.branches[0].body) ? std::string() : map;
			std::set<std::string> ends;
			for (const Branch& b : s.branches) {
				if (b.has_condition) {
					condition(b.condition, b.line);
				}
				if (s.kind == StmtKind::battle && !b.present) {
					continue;
				}
				ends.insert(block(b.body, start));
			}
			const bool may_skip = s.kind == StmtKind::while_loop || (s.kind == StmtKind::if_chain && s.branches.back().has_condition);
			if (may_skip) {
				ends.insert(map);
			}
			map = ends.size() == 1 ? *ends.begin() : std::string();
		}
		return map;
	}

private:
	void error(int line, const char* key, std::vector<std::string> params) {
		diagnostics_.push_back(diagnostic(Severity::error, file_, line, key, std::move(params)));
	}

	std::string where() const {
		return registry_.file.empty() ? std::string(" in the registry") : " in " + registry_.file;
	}

	void name(NameKind kind, const std::string& value, int line) {
		if (!value.empty() && !registry_.has(kind, value)) {
			error(line, "script.unknown_name", {std::string(name_kind_name(kind)), value, where()});
		}
	}

	void declared(const std::string& value, bool is_flag, int line) {
		const Declaration* d = registry_.find(value);
		if (!d) {
			error(line, is_flag ? "script.flag_not_declared" : "script.variable_not_declared", {value, where()});
		} else if (d->is_flag != is_flag) {
			error(line, d->is_flag ? "script.declared_as_flag" : "script.declared_as_variable", {value});
		}
	}

	void condition(const Condition& c, int line) {
		for (const Term& t : c.terms) {
			if (t.vehicle != VehicleTest::none) {
				name(NameKind::vehicle, t.name, line);
			} else {
				declared(t.name, t.is_flag, line);
			}
		}
	}

	static bool changes_map(const std::vector<Stmt>& body) {
		for (const Stmt& s : body) {
			const bool warp = s.kind == StmtKind::simple && s.ins.op == Op::action && s.ins.action.type == ActionType::warp;
			if (warp || s.kind == StmtKind::label || (s.kind == StmtKind::simple && s.ins.op == Op::call)) {
				return true;
			}
			for (const Branch& b : s.branches) {
				if (changes_map(b.body)) {
					return true;
				}
			}
		}
		return false;
	}

	void on_map(const std::string& marker, const std::string& map, int line, const char* why) {
		if (map.empty() || !registry_.has(NameKind::marker, marker) || registry_.marker_on(marker, map)) {
			return;
		}
		std::string maps;
		for (const std::string& m : registry_.marker_maps.at(marker)) {
			maps += (maps.empty() ? "" : ", ") + m;
		}
		error(line, "script.marker_not_on_map", {marker, map, why, maps});
	}

	void action(const Action& a, int line, std::string& map) {
		switch (a.type) {
		case ActionType::move:
			name(NameKind::actor, a.subject, line);
			if (!a.has_position) {
				name(NameKind::marker, a.target, line);
				on_map(a.target, map, line, ", where the party is at this point");
			}
			break;
		case ActionType::face:
			name(NameKind::actor, a.subject, line);
			if (!registry_.has(NameKind::actor, a.target) && !registry_.has(NameKind::marker, a.target)) {
				error(line, "script.unknown_actor_or_marker", {a.target, where()});
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
			on_map(a.target, a.subject, line, "");
			map = a.subject;
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

	std::string statement(const Stmt& s, std::string map) {
		const Instruction& ins = s.ins;
		if (s.kind == StmtKind::while_loop) {
			condition(ins.condition, s.line);
			return map;
		}
		if (s.kind != StmtKind::simple && s.kind != StmtKind::choice && s.kind != StmtKind::battle) {
			return map;
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
		case Op::vehicle_give:
		case Op::vehicle_place:
			name(NameKind::vehicle, ins.name, s.line);
			name(NameKind::map, ins.action.subject, s.line);
			name(NameKind::marker, ins.action.target, s.line);
			on_map(ins.action.target, ins.action.subject, s.line, "");
			break;
		case Op::vehicle_remove:
			name(NameKind::vehicle, ins.name, s.line);
			break;
		case Op::learn:
			name(NameKind::member, ins.name, s.line);
			name(NameKind::ability, ins.action.subject, s.line);
			break;
		case Op::follower:
			if (!ins.name.empty()) {
				name(NameKind::actor, ins.name, s.line);
			}
			break;
		case Op::rest:
			name(NameKind::rest_point, ins.name, s.line);
			break;
		case Op::battle:
			name(NameKind::encounter, ins.battle.encounter, s.line);
			if (!ins.battle.vehicle.empty()) {
				name(NameKind::vehicle, ins.battle.vehicle, s.line);
			}
			if (ins.battle.end == BattleEndKind::enemy_defeated) {
				name(NameKind::enemy, ins.battle.end_enemy, s.line);
			}
			break;
		case Op::action:
			action(ins.action, s.line, map);
			break;
		case Op::call:
			map.clear();
			break;
		default:
			break;
		}
		return map;
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
			diagnostics.push_back(diagnostic(Severity::error, files[ev.file], ev.line, "script.duplicate_event_already_defined_at", {ev.name, files[first->file], std::to_string(first->line)}));
		} else {
			by_name[ev.name] = &ev;
		}
	}

	for (const EventDecl& ev : events) {
		const std::string& file = files[ev.file];
		Walk w;
		walk(ev.body, w);
		for (const auto& dup : w.duplicate_labels) {
			diagnostics.push_back(diagnostic(Severity::error, file, dup.second, "script.duplicate_label_already_defined_at", {dup.first, std::to_string(w.labels[dup.first])}));
		}
		for (const Stmt* g : w.gotos) {
			if (!w.labels.count(g->ins.name)) {
				diagnostics.push_back(diagnostic(Severity::error, file, g->line, "script.goto_unknown_label_event", {g->ins.name, ev.name}));
			}
		}
		for (const Stmt* c : w.calls) {
			if (!by_name.count(c->ins.name)) {
				diagnostics.push_back(diagnostic(Severity::error, file, c->line, "script.call_unknown_event", {c->ins.name}));
			}
		}
		if (registry) {
			auto starts = registry->event_maps.find(ev.name);
			const bool one_start = starts != registry->event_maps.end() && starts->second.size() == 1;
			RegistryCheck(*registry, file, diagnostics).block(ev.body, one_start ? *starts->second.begin() : std::string());
		}
	}
}

}
