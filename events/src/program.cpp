#include "events/diagnostics.h"
#include "events/program.h"

namespace events {

int Program::find_event(const std::string& name) const {
	for (std::size_t i = 0; i < events.size(); ++i) {
		if (events[i].name == name) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

std::string describe(const Condition& condition) {
	static const char* const compares[] = {"==", "!=", "<", "<=", ">", ">="};
	std::string out;
	for (std::size_t i = 0; i < condition.terms.size(); ++i) {
		const Term& t = condition.terms[i];
		if (i > 0) {
			out += condition.join == Join::all ? " and " : " or ";
		}
		if (t.negate) {
			out += "not ";
		}
		out += t.name;
		if (!t.is_flag) {
			out += std::string(" ") + compares[static_cast<int>(t.compare)] + " " + std::to_string(t.value);
		}
	}
	return out;
}

const char* op_name(Op op) {
	switch (op) {
	case Op::say:
		return "say";
	case Op::choice:
		return "choice";
	case Op::set_flag:
		return "set_flag";
	case Op::clear_flag:
		return "clear_flag";
	case Op::var_set:
		return "var_set";
	case Op::var_add:
		return "var_add";
	case Op::jump:
		return "jump";
	case Op::jump_unless:
		return "jump_unless";
	case Op::action:
		return "action";
	case Op::battle:
		return "battle";
	case Op::wait:
		return "wait";
	case Op::sync:
		return "sync";
	case Op::call:
		return "call";
	case Op::ret:
		return "return";
	case Op::give:
		return "give";
	case Op::take:
		return "take";
	case Op::party_add:
		return "party_add";
	case Op::party_remove:
		return "party_remove";
	case Op::lock_player:
		return "lock_player";
	case Op::unlock_player:
		return "unlock_player";
	case Op::fork:
		return "fork";
	case Op::end_branch:
		return "end_branch";
	}
	return "unknown";
}

std::string format_diagnostic(const Diagnostic& diagnostic) {
	return diagnostic.file + ":" + std::to_string(diagnostic.line) + ": " +
		(diagnostic.severity == Severity::error ? "error" : "warning") + ": " + diagnostic.message;
}

bool has_errors(const std::vector<Diagnostic>& diagnostics) {
	for (const Diagnostic& d : diagnostics) {
		if (d.severity == Severity::error) {
			return true;
		}
	}
	return false;
}

}
