#include "support.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>

using namespace game;

namespace {

bool isNumber(const std::string& token) {
	return token.find_first_of("0123456789") != std::string::npos && token.find_first_not_of("-0123456789") == std::string::npos;
}

// Every key of the save format whose value names content, with the position of that value. Values elsewhere are
// counts, levels, hit points, cells, times or the encounter stream.
struct ContentField {
	const char* chunk;
	const char* key;
	std::size_t from;
	const char* kind;
};

const ContentField kContentFields[] = {
	{"header", "area", 1, "area"}, {"header", "difficulty", 1, "difficulty"},
	{"location", "area", 1, "area"},
	{"party", "lineup", 1, "character"}, {"party", "member", 1, "character"}, {"party", "uses", 1, "ability"}, {"party", "wear", 2, "equipment"},
	{"party", "status", 1, "status"}, {"party", "named", 1, "character"},
	{"inventory", "item", 1, "item"}, {"inventory", "bag", 1, "equipment"},
	{"world", "flag", 1, "flag"}, {"world", "visited", 1, "area"}, {"world", "enemy", 2, "zone"}, {"world", "enemy", 3, "encounter"},
	{"quests", "job", 1, "job"}, {"unlocks", "entry", 1, "entry"},
	{"difficulty", "difficulty", 1, "difficulty"},
	{"actors", "actor", 1, "actor"}, {"actors", "actor", 2, "actor_area"},
};

bool known(const GameData& d, const std::string& kind, const std::string& id) {
	if (kind == "area") return d.area(id) != nullptr;
	// An actor a script showed or hid before giving it a cell has no area, written as "-".
	if (kind == "actor_area") return id == "-" || d.area(id) != nullptr;
	if (kind == "actor") return std::find(d.config.actors.begin(), d.config.actors.end(), id) != d.config.actors.end();
	if (kind == "difficulty") return d.db.difficulty(id) != nullptr;
	if (kind == "character") return d.db.character(id) != nullptr;
	if (kind == "ability") return d.db.ability(id) != nullptr;
	if (kind == "equipment") return d.db.equipmentPiece(id) != nullptr;
	if (kind == "status") return d.db.status(id) != nullptr;
	if (kind == "item") return d.db.item(id) != nullptr;
	if (kind == "encounter") return d.db.encounter(id) != nullptr;
	if (kind == "zone") {
		for (const auto& [areaId, area] : d.areas) {
			if (area.zone(id)) return true;
		}
		return false;
	}
	if (kind == "flag") {
		const events::Declaration* decl = d.registry.find(id);
		return decl && decl->is_flag;
	}
	if (kind == "job") {
		for (const Job& j : d.journal.jobs) {
			if (j.id == id) return true;
		}
		return false;
	}
	for (const EncyclopediaEntry& e : d.journal.encyclopedia) {
		if (e.id == id) return true;
	}
	return false;
}

// The data with an extra entry sorted first into every content table and the areas in reverse order, so every
// table index a save could hold would now name something else.
GameData shifted(const GameData& d) {
	GameData out = d;
	out.db.items["aaa_decoy"] = out.db.items.begin()->second;
	out.db.items["aaa_decoy"].id = "aaa_decoy";
	out.db.equipment["aaa_decoy"] = out.db.equipment.begin()->second;
	out.db.equipment["aaa_decoy"].id = "aaa_decoy";
	out.db.characters["aaa_decoy"] = out.db.characters.begin()->second;
	out.db.characters["aaa_decoy"].id = "aaa_decoy";
	out.db.abilities["aaa_decoy"] = out.db.abilities.begin()->second;
	out.db.abilities["aaa_decoy"].id = "aaa_decoy";
	out.db.statuses["aaa_decoy"] = out.db.statuses.begin()->second;
	out.db.statuses["aaa_decoy"].id = "aaa_decoy";
	out.db.enemies["aaa_decoy"] = out.db.enemies.begin()->second;
	out.db.enemies["aaa_decoy"].id = "aaa_decoy";
	out.areas["aaa_decoy"] = out.areas.begin()->second;
	out.areas["aaa_decoy"].id = "aaa_decoy";
	std::reverse(out.config.areaFiles.begin(), out.config.areaFiles.end());
	out.journal.jobs.insert(out.journal.jobs.begin(), Job{"aaa_decoy", out.journal.jobs.front().title, "aaa.available", "aaa.accepted", "aaa.done"});
	out.journal.encyclopedia.insert(out.journal.encyclopedia.begin(), EncyclopediaEntry{"aaa_decoy", "t", "t", "aaa.flag"});
	return out;
}

}

ACC_CHECK(acc_sav_04, "ACC-SAV-04", "SAV-012") {
	const GameData& data = support::data(t);
	const GameData moved = shifted(data);
	const std::string dir = support::tempDirectory("sav04");
	Session session(data, support::options(dir));
	int saves = 0;
	int contentValues = 0;
	int reloaded = 0;
	std::set<std::string> numericKeys;
	support::playThrough(t, session, [&](const std::string& line) {
		if (session.mode() != Mode::Field) {
			return;
		}
		++saves;
		const std::string text = serializeSave(session.makeSave(false));
		std::istringstream in(text);
		std::string row;
		std::string chunk;
		while (std::getline(in, row)) {
			std::istringstream words(row);
			std::vector<std::string> w;
			for (std::string word; words >> word;) {
				w.push_back(word);
			}
			if (w.empty()) {
				continue;
			}
			if (w[0] == "chunk" && w.size() > 1) {
				chunk = w[1];
				continue;
			}
			if (chunk.empty() || w[0] == "end") {
				continue;
			}
			for (const ContentField& f : kContentFields) {
				if (chunk != f.chunk || w[0] != f.key) {
					continue;
				}
				for (std::size_t i = f.from; i < w.size() && (i == f.from || std::string(f.key) == "lineup"); ++i) {
					const std::string& id = w[i];
					++contentValues;
					t.expect(!isNumber(id) && known(data, f.kind, id), "after '" + line + "': chunk " + chunk + " line '" + row + "' holds '" + id + "', which is not a " + f.kind + " ID");
				}
			}
			for (std::size_t i = 1; i < w.size(); ++i) {
				if (isNumber(w[i])) {
					numericKeys.insert(chunk + "." + w[0]);
				}
			}
		}
		// Loading against the shifted data gives the same world, so nothing in the save is a position in a table.
			std::string error;
		t.expect(writeFileAtomic(dir + "/manual_9.sav", text, error), "setup: " + error);
		Session other(moved, support::options(dir));
		const rules::Message loaded = other.load("manual_9");
		if (t.expect(loaded.empty(), "after '" + line + "': the save does not load against shifted data: " + other.render(loaded))) {
			std::string diff;
			t.expect(sameWorld(other.world(), session.world(), &diff), "after '" + line + "': against shifted data field " + diff + " loads differently");
			++reloaded;
		}
	});
	std::string keys;
	for (const std::string& k : numericKeys) {
		keys += (keys.empty() ? "" : ", ") + k;
	}
	t.note(std::to_string(saves) + " saves from the playthrough: " + std::to_string(contentValues) +
		" content references (areas, characters, actors, abilities, equipment, statuses, items, flags, jobs, encyclopedia entries, difficulty) are all string IDs of the loaded data; numbers appear only in " + keys +
		" (counts, levels, hit points, cells, times, sequence, encounter stream, and macro steps' formation positions, which are target rules); " + std::to_string(reloaded) +
		" of them load to the same world against data with a decoy sorted first into every content table and the areas reordered");
	std::error_code ignored;
	std::filesystem::remove_all(dir, ignored);
}
