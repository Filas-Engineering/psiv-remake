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
		return Result<TextTable>::failure(parsed.error);
	}
	const JsonValue* entries = parsed.value->is(JsonValue::Type::Object) ? parsed.value->find("text") : nullptr;
	if (!entries || !entries->is(JsonValue::Type::Object) || parsed.value->keys.size() != 1) {
		return Result<TextTable>::failure("a text table is {\"text\": {\"key\": \"template\"}}");
	}
	TextTable table;
	for (std::size_t i = 0; i < entries->keys.size(); ++i) {
		if (!entries->values[i].is(JsonValue::Type::String)) {
			return Result<TextTable>::failure("line " + std::to_string(entries->values[i].line) + ": text for '" + entries->keys[i] + "' must be a string");
		}
		table[entries->keys[i]] = entries->values[i].string;
	}
	return Result<TextTable>::success(std::move(table));
}

Result<TextTable> loadTextTable(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return Result<TextTable>::failure(path + ": cannot open file");
	}
	std::ostringstream text;
	text << in.rdbuf();
	Result<TextTable> table = parseTextTable(text.str());
	if (!table.ok()) {
		table.error = path + ": " + table.error;
	}
	return table;
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
