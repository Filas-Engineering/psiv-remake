#include "support.h"

#include "rules/names.h"

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
	t.note(std::to_string(strings) + " string fields across 500 battles are all ability, item or status ids; no display name appears in any serialized stream; every other field is a number or enum");
}
