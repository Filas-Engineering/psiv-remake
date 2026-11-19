#include "rules/text.h"

#include "rules/database.h"
#include "rules/json.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace rules {

namespace {

// Keys core returns from setCommand, canEquip, equip and the out-of-battle functions, or emits in Message events.
const char* const kFixedKeys[] = {
	"battle.opening.enemy_first",
	"battle.opening.party_first",
	"command.aboard",
	"command.actor_defeated",
	"command.caught_off_guard",
	"command.combo_not_selectable",
	"command.enemy_cannot_escape",
	"command.escape_forbidden",
	"command.forced_by_status",
	"command.no_combatant",
	"command.unknown_ability",
	"command.unknown_item",
	"equip.not_in_bag",
	"equip.unknown_character",
	"equip.unknown_equipment",
	"equip.wrong_category",
	"field.no_stock",
	"field.no_target",
	"field.no_uses_left",
	"field.not_enough_tp",
	"field.not_known",
	"field.not_usable_here",
	"field.silenced",
	"field.unknown_ability",
	"field.unknown_item",
	"field.unknown_member",
	"field.user_down",
};

}

Result<TextTable> parseTextTable(const std::string& json) {
	const Result<JsonValue> parsed = parseJson(json);
	if (!parsed.ok()) {
		return Result<TextTable>::failure(parsed.problems, parsed.error);
	}
	auto fail = [](int line, const char* key, std::vector<std::string> params) {
		Problem problem{"", line, "", {key, std::move(params)}};
		return Result<TextTable>::failure({problem}, formatProblem(problem, problemText()));
	};
	const JsonValue* entries = parsed.value->is(JsonValue::Type::Object) ? parsed.value->find("text") : nullptr;
	if (!entries || !entries->is(JsonValue::Type::Object) || parsed.value->keys.size() != 1) {
		return fail(parsed.value->line, "text.table_shape", {});
	}
	TextTable table;
	for (std::size_t i = 0; i < entries->keys.size(); ++i) {
		if (!entries->values[i].is(JsonValue::Type::String)) {
			return fail(entries->values[i].line, "text.not_string", {entries->keys[i]});
		}
		table[entries->keys[i]] = entries->values[i].string;
	}
	return Result<TextTable>::success(std::move(table));
}

Result<TextTable> loadTextTable(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		Problem problem{path, 0, "", {"load.cannot_open", {}}};
		return Result<TextTable>::failure({problem}, formatProblem(problem, problemText()));
	}
	std::ostringstream text;
	text << in.rdbuf();
	Result<TextTable> table = parseTextTable(text.str());
	for (Problem& problem : table.problems) {
		problem.file = path;
	}
	if (!table.ok()) {
		table.error = formatProblems(table.problems, problemText());
	}
	return table;
}

std::string formatProblem(const Problem& problem, const TextTable& table) {
	std::string out = problem.file;
	if (problem.line > 0) {
		out += (out.empty() ? "line " : ":") + std::to_string(problem.line);
	}
	if (!out.empty()) {
		out += ": ";
	}
	if (!problem.entry.empty()) {
		out += problem.entry + ": ";
	}
	return out + formatText(table, problem.message.key, problem.message.params);
}

std::string formatProblems(const std::vector<Problem>& problems, const TextTable& table) {
	std::string out;
	for (const Problem& problem : problems) {
		out += (out.empty() ? "" : "\n") + formatProblem(problem, table);
	}
	return out;
}

std::string formatText(const TextTable& table, const std::string& key, const std::vector<std::string>& params) {
	const auto it = table.find(key);
	if (it == table.end()) {
		return key;
	}
	const std::string& pattern = it->second;
	std::string out;
	for (std::size_t i = 0; i < pattern.size(); ++i) {
		const std::size_t close = pattern[i] == '{' ? pattern.find('}', i) : std::string::npos;
		if (close != std::string::npos && close > i + 1 && close - i <= 3) {
			const std::string digits = pattern.substr(i + 1, close - i - 1);
			if (std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
				std::size_t index = 0;
				for (char c : digits) {
					index = index * 10 + static_cast<std::size_t>(c - '0');
				}
				out += index < params.size() ? params[index] : "";
				i = close;
				continue;
			}
		}
		out += pattern[i];
	}
	return out;
}

std::string statusMessageKey(const std::string& statusId, const char* kind) {
	return "status." + statusId + "." + kind;
}

std::vector<std::string> messageKeys(const Database& db) {
	std::vector<std::string> keys(std::begin(kFixedKeys), std::end(kFixedKeys));
	for (const auto& [id, status] : db.statuses) {
		if (id == kDefeatedStatus) {
			continue;
		}
		for (const char* kind : {"applied", "immune", "removed", "resisted"}) {
			keys.push_back(statusMessageKey(id, kind));
		}
		if (status.stacking == StatusStacking::Keep) {
			keys.push_back(statusMessageKey(id, "unchanged"));
		}
	}
	std::sort(keys.begin(), keys.end());
	return keys;
}

}
