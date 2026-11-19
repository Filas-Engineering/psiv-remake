#include "events/strings.h"

#include "events/glossary.h"
#include "text_util.h"

#include <algorithm>
#include <set>

namespace events {

namespace {

bool unescape(const std::string& text, std::string& out, std::string& error) {
	out.clear();
	for (std::size_t i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (c != '\\') {
			out += c;
			continue;
		}
		if (i + 1 >= text.size()) {
			error = "a '\\' at the end of the line";
			return false;
		}
		char e = text[++i];
		if (e == 'n') {
			out += '\n';
		} else if (e == '\\') {
			out += '\\';
		} else {
			error = std::string("unknown escape '\\") + e + "'";
			return false;
		}
	}
	return true;
}

std::string escape(const std::string& text) {
	std::string out;
	for (char c : text) {
		if (c == '\\') {
			out += "\\\\";
		} else if (c == '\n') {
			out += "\\n";
		} else {
			out += c;
		}
	}
	return out;
}

std::vector<std::string> token_ids(const std::string& text) {
	std::vector<std::string> ids;
	for (const NameToken& t : find_name_tokens(text)) {
		ids.push_back(t.id.empty() ? "(broken)" : t.id);
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

std::string join(const std::vector<std::string>& parts) {
	std::string out;
	for (const std::string& p : parts) {
		out += (out.empty() ? "" : ", ") + p;
	}
	return out.empty() ? "none" : out;
}

}

const std::string* StringTable::find(const std::string& id) const {
	auto found = entries.find(id);
	return found == entries.end() ? nullptr : &found->second.text;
}

bool load_string_table(const std::string& file, const std::string& text, StringTable& table, std::vector<Diagnostic>& diagnostics) {
	StringTable result;
	result.file = file;
	bool ok = true;
	auto error = [&](int line, const std::string& message) {
		diagnostics.push_back({Severity::error, file, line, message});
		ok = false;
	};
	std::vector<std::string> lines = split_lines(text);
	for (std::size_t i = 0; i < lines.size(); ++i) {
		int number = static_cast<int>(i) + 1;
		std::string line = trim(lines[i]);
		if (line.empty() || line[0] == '#') {
			continue;
		}
		if (result.language.empty()) {
			std::string head = line.substr(0, line.find_first_of(" \t"));
			std::string code = trim(line.substr(head.size()));
			if (head != "language" || !is_name(code)) {
				error(number, "the table must start with 'language <code>'");
				return false;
			}
			result.language = code;
			continue;
		}
		std::size_t eq = line.find('=');
		if (eq == std::string::npos) {
			error(number, "expected '<id> = <text>'");
			continue;
		}
		std::string id = trim(line.substr(0, eq));
		if (!is_name(id)) {
			error(number, "'" + id + "' is not a valid string ID");
			continue;
		}
		StringEntry entry;
		entry.line = number;
		std::string problem;
		if (!unescape(trim(line.substr(eq + 1)), entry.text, problem)) {
			error(number, problem);
			continue;
		}
		auto found = result.entries.find(id);
		if (found != result.entries.end()) {
			error(number, "duplicate string ID '" + id + "'; it is already defined at line " + std::to_string(found->second.line));
			continue;
		}
		result.entries[id] = entry;
	}
	if (result.language.empty()) {
		error(1, "the table must start with 'language <code>'");
	}
	if (ok) {
		table = result;
	}
	return ok;
}

bool resolve_text(const Text& text, const StringTable& table, std::string& out) {
	if (!text.is_id) {
		out = text.value;
		return true;
	}
	const std::string* found = table.find(text.value);
	if (!found) {
		out = "@" + text.value;
		return false;
	}
	out = *found;
	return true;
}

std::vector<TextUse> collect_texts(const Program& program) {
	std::vector<TextUse> uses;
	std::vector<int> owner = event_of_pc(program);
	for (std::size_t pc = 0; pc < program.code.size(); ++pc) {
		const Instruction& ins = program.code[pc];
		TextUse use;
		use.file = ins.file;
		use.line = ins.source_line;
		use.event = owner[pc] >= 0 ? program.events[owner[pc]].name : std::string();
		if (ins.op == Op::say) {
			use.role = ins.dialogue.speaker.empty() ? "narrate" : "say " + ins.dialogue.speaker;
			use.text = ins.dialogue.text;
			uses.push_back(use);
		} else if (ins.op == Op::choice) {
			if (!ins.choice.prompt.value.empty()) {
				use.role = ins.choice.yes_no ? "ask" : "choice prompt";
				use.text = ins.choice.prompt;
				uses.push_back(use);
			}
			for (std::size_t i = 0; i < ins.choice.options.size(); ++i) {
				use.role = "option";
				use.text = ins.choice.options[i];
				use.line = i < ins.option_lines.size() ? ins.option_lines[i] : ins.source_line;
				uses.push_back(use);
			}
		}
	}
	return uses;
}

std::vector<std::string> find_control_tags(const std::string& text) {
	std::vector<std::string> tags;
	std::size_t at = 0;
	while ((at = text.find('{', at)) != std::string::npos) {
		std::size_t close = text.find('}', at);
		if (close == std::string::npos) {
			break;
		}
		std::string inside = text.substr(at + 1, close - at - 1);
		std::string tag = inside.substr(0, inside.find(':'));
		bool name_token = tag == name_case_word(NameCase::as_written) || tag == name_case_word(NameCase::upper) || tag == name_case_word(NameCase::lower);
		if (is_name(tag) && !name_token && inside.find_first_of("{ ") == std::string::npos) {
			tags.push_back(text.substr(at, close - at + 1));
			at = close + 1;
		} else {
			++at;
		}
	}
	return tags;
}

std::vector<Diagnostic> check_inline_text(const Program& program) {
	std::vector<Diagnostic> out;
	for (const TextUse& use : collect_texts(program)) {
		if (!use.text.is_id) {
			out.push_back({Severity::error, program.files[use.file], use.line,
				"inline text in " + use.role + " in event '" + use.event + "': player-facing text must be a string ID (@id) from the string tables"});
		}
	}
	return out;
}

bool GameStrings::covers(const std::string& id) const {
	if (ids.count(id)) {
		return true;
	}
	for (const std::string& prefix : prefixes) {
		if (id.compare(0, prefix.size(), prefix) == 0) {
			return true;
		}
	}
	return false;
}

bool load_game_strings(const std::string& file, const std::string& text, GameStrings& out, std::vector<Diagnostic>& diagnostics) {
	GameStrings result;
	result.file = file;
	bool ok = true;
	const std::vector<std::string> lines = split_lines(text);
	for (std::size_t i = 0; i < lines.size(); ++i) {
		const int number = static_cast<int>(i) + 1;
		const std::string entry = trim(lines[i].substr(0, lines[i].find('#')));
		if (entry.empty()) {
			continue;
		}
		const bool prefix = entry.back() == '*';
		const std::string name = prefix ? entry.substr(0, entry.size() - 1) : entry;
		// A prefix may end in '.' ("area.*"), which is not a valid name on its own.
		const std::string checked = prefix && !name.empty() && name.back() == '.' ? name.substr(0, name.size() - 1) : name;
		if (!is_name(checked)) {
			diagnostics.push_back({Severity::error, file, number, "'" + entry + "' is not a string ID or a prefix ending in '*'"});
			ok = false;
		} else if (prefix) {
			result.prefixes.push_back(name);
		} else if (!result.ids.emplace(name, number).second) {
			diagnostics.push_back({Severity::error, file, number, "'" + name + "' is already listed at line " + std::to_string(result.ids[name])});
			ok = false;
		}
	}
	if (ok) {
		out = result;
	}
	return ok;
}

std::vector<Diagnostic> check_strings(const Program& program, const std::vector<StringTable>& tables, const GameStrings& game) {
	std::vector<Diagnostic> out;
	std::set<std::string> referenced;
	for (const auto& [id, line] : game.ids) {
		referenced.insert(id);
		for (const StringTable& table : tables) {
			if (!table.find(id)) {
				out.push_back({Severity::error, game.file, line, "string ID '" + id + "' is missing from the '" + table.language + "' table (" + table.file + ")"});
			}
		}
	}
	for (const TextUse& use : collect_texts(program)) {
		if (!use.text.is_id) {
			continue;
		}
		referenced.insert(use.text.value);
		for (const StringTable& table : tables) {
			if (!table.find(use.text.value)) {
				out.push_back({Severity::error, program.files[use.file], use.line,
					"string ID '" + use.text.value + "' is missing from the '" + table.language + "' table (" + table.file + ")"});
			}
		}
	}
	if (!program.events.empty()) {
		for (const StringTable& table : tables) {
			for (const auto& entry : table.entries) {
				if (!referenced.count(entry.first) && !game.covers(entry.first)) {
					out.push_back({Severity::warning, table.file, entry.second.line, "string ID '" + entry.first + "' is not used by any script or by the game"});
				}
			}
		}
	}
	std::set<std::string> all;
	for (const StringTable& table : tables) {
		for (const auto& entry : table.entries) {
			all.insert(entry.first);
		}
	}
	for (const std::string& id : all) {
		const StringTable* first = nullptr;
		for (const StringTable& table : tables) {
			if (table.find(id)) {
				first = &table;
				break;
			}
		}
		const StringEntry& reference = first->entries.at(id);
		for (const StringTable& table : tables) {
			auto found = table.entries.find(id);
			if (found == table.entries.end()) {
				if (!referenced.count(id)) {
					out.push_back({Severity::warning, first->file, reference.line,
						"string ID '" + id + "' is in the '" + first->language + "' table but missing from '" + table.language + "' (" + table.file + ")"});
				}
				continue;
			}
			if (&table == first) {
				continue;
			}
			std::vector<std::string> expected = token_ids(reference.text);
			std::vector<std::string> got = token_ids(found->second.text);
			if (expected != got) {
				out.push_back({Severity::error, table.file, found->second.line,
					"name tokens in '" + id + "' (" + join(got) + ") differ from the '" + first->language + "' table (" + join(expected) + ")"});
			}
			std::vector<std::string> expected_tags = find_control_tags(reference.text);
			std::vector<std::string> got_tags = find_control_tags(found->second.text);
			if (expected_tags != got_tags) {
				out.push_back({Severity::error, table.file, found->second.line,
					"control tags in '" + id + "' (" + join(got_tags) + ") differ from the '" + first->language + "' table (" + join(expected_tags) +
						"); a translation keeps the same tags in the same order"});
			}
		}
	}
	return out;
}

std::string extract_strings(const Program& program, const std::string& language) {
	std::vector<TextUse> uses = collect_texts(program);
	std::string out = "# Proposed string table: every inline text in the scripts. Nothing was rewritten.\n"
		"# To adopt a line, replace its quoted text in the script with @<id>.\n"
		"language " + language + "\n";
	std::map<std::string, int> counts;
	for (const TextUse& use : uses) {
		if (use.text.is_id) {
			continue;
		}
		std::string id = use.event + "." + std::to_string(++counts[use.event]);
		out += "\n# " + program.files[use.file] + ":" + std::to_string(use.line) + " " + use.event + ": " + use.role + "\n";
		if (trim(use.text.value) != use.text.value) {
			out += "# note: the script text has leading or trailing spaces, which a table entry drops\n";
		}
		out += id + " = " + escape(use.text.value) + "\n";
	}
	return out;
}

}
