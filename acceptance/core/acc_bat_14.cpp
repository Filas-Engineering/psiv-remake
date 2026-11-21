#include "support.h"

#include "rules/names.h"
#include "rules/text.h"

#include <set>

using namespace rules;

ACC_CHECK(acc_bat_14, "ACC-BAT-14", "BAT-112") {
	const Database& db = support::data(t);
	std::set<std::string> ids;
	std::set<std::string> names;
	for (const auto& [id, a] : db.abilities) {
		ids.insert(id);
		names.insert(a.name);
	}
	for (const auto& [id, item] : db.items) {
		ids.insert(id);
		names.insert(item.name);
	}
	std::set<std::string> statusIds;
	for (const auto& [id, s] : db.statuses) {
		statusIds.insert(id);
		names.insert(s.name);
	}
	for (const auto& [id, e] : db.enemies) {
		names.insert(e.name);
	}
	for (const auto& [id, c] : db.characters) {
		names.insert(c.name);
	}

	std::set<std::string> definitions;
	for (const auto& [id, c] : db.characters) {
		definitions.insert(id);
	}
	for (const auto& [id, e] : db.enemies) {
		definitions.insert(id);
	}
	std::set<std::string> combos;
	for (const ComboRecipe& recipe : db.combos) {
		combos.insert(recipe.id);
	}
	for (const EnemyComboRecipe& recipe : db.enemyCombos) {
		combos.insert(recipe.id);
	}
	std::set<std::string> macros;
	for (const Macro& macro : db.macros) {
		macros.insert(macro.name);
	}
	Result<TextTable> table = loadTextTable(t.path("core/data/text_en.json"));
	if (!t.expect(table.ok(), "core/data/text_en.json does not load: " + table.error)) {
		return;
	}
	auto isNumber = [](const std::string& value) {
		return !value.empty() && value.find_first_not_of("-0123456789") == std::string::npos;
	};

	ComboLog log;
	int strings = 0;
	for (std::uint64_t seed = 1; seed <= 500; ++seed) {
		const support::PlayedBattle played = support::playRandomBattle(db, seed, log);
		for (const Event& e : played.events) {
			if (!e.ability.empty()) {
				++strings;
				t.expect(ids.count(e.ability) == 1, std::string(toString(e.type)) + " ability field holds '" + e.ability + "', which is not an ability or item id");
			}
			if (!e.status.empty()) {
				++strings;
				t.expect(statusIds.count(e.status) == 1, std::string(toString(e.type)) + " status field holds '" + e.status + "', which is not a status id");
			}
			if (!e.combo.empty()) {
				++strings;
				t.expect(combos.count(e.combo) == 1, std::string(toString(e.type)) + " combo field holds '" + e.combo + "', which is not a combination id");
			}
			if (!e.message.empty()) {
				++strings;
				t.expect(table.value->count(e.message) == 1, std::string(toString(e.type)) + " message field holds '" + e.message + "', which is not a key in the string table");
			}
			for (const std::string& param : e.params) {
				++strings;
				t.expect(isNumber(param) || ids.count(param) == 1 || statusIds.count(param) == 1 || definitions.count(param) == 1, std::string(toString(e.type)) + " parameter '" + param + "' is neither a number nor a content id");
			}
			if (!e.macro.empty()) {
				++strings;
				t.expect(macros.count(e.macro) == 1, std::string(toString(e.type)) + " macro field holds '" + e.macro + "', which is not a macro name from the data");
			}
			for (const std::string& id : e.ids) {
				++strings;
				t.expect(definitions.count(id) == 1, std::string(toString(e.type)) + " ids field holds '" + id + "', which is not a character or enemy id");
			}
		}
		// The stable text form is what crosses to the renderer and into bug reports: no field value is a display name.
		const std::string text = serializeEvents(played.events);
		std::size_t at = 0;
		while ((at = text.find('=', at)) != std::string::npos) {
			const std::size_t end = text.find_first_of(" \n", at);
			const std::string value = text.substr(at + 1, end - at - 1);
			t.expect(names.count(value) == 0 || ids.count(value) == 1 || statusIds.count(value) == 1, "an event field holds the display name '" + value + "'");
			at = end;
		}
	}
	t.note(std::to_string(strings) + " string fields across 500 battles are all content ids (ability, item, status, combination, character, enemy, macro), string table keys or numeric parameters; no display name appears in any serialized stream; every other field is a number or enum");
}
