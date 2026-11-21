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
		static const char* const tests[] = {"", "vehicle ", "aboard ", "party ", "item "};
		out += tests[static_cast<int>(t.test)];
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
	case Op::shop:
		return "shop";
	case Op::rest:
		return "rest";
	case Op::save_prompt:
		return "save prompt";
	case Op::name_entry:
		return "name entry";
	case Op::vehicle_give:
		return "vehicle give";
	case Op::vehicle_place:
		return "vehicle place";
	case Op::learn:
		return "learn";
	case Op::follower:
		return "follower";
	case Op::vehicle_remove:
		return "vehicle remove";
	}
	return "unknown";
}

std::string format_diagnostic(const Diagnostic& diagnostic) {
	return diagnostic.file + ":" + std::to_string(diagnostic.line) + ": " +
		(diagnostic.severity == Severity::error ? "error" : "warning") + ": " + diagnostic.message;
}

std::string format_message(const std::map<std::string, std::string>& table, const std::string& key, const std::vector<std::string>& params) {
	auto found = table.find(key);
	if (found == table.end()) {
		return key;
	}
	const std::string& pattern = found->second;
	std::string out;
	for (std::size_t i = 0; i < pattern.size(); ++i) {
		std::size_t close = pattern[i] == '{' ? pattern.find('}', i) : std::string::npos;
		if (close != std::string::npos && close > i + 1 && close - i <= 3 &&
			pattern.find_first_not_of("0123456789", i + 1) == close) {
			std::size_t index = static_cast<std::size_t>(std::stoi(pattern.substr(i + 1, close - i - 1)));
			out += index < params.size() ? params[index] : "";
			i = close;
			continue;
		}
		out += pattern[i];
	}
	return out;
}

Diagnostic diagnostic(Severity severity, std::string file, int line, std::string key, std::vector<std::string> params) {
	Diagnostic d;
	d.severity = severity;
	d.file = std::move(file);
	d.line = line;
	d.message = format_message(diagnostic_text(), key, params);
	d.key = std::move(key);
	d.params = std::move(params);
	return d;
}

Diagnostic diagnostic(Severity severity, std::string file, int line, const DiagnosticMessage& message) {
	return diagnostic(severity, std::move(file), line, message.key, message.params);
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
