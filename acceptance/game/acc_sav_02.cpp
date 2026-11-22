#include "support.h"

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <string>

using namespace game;

namespace {

std::string chunk(const std::string& type, int version, const std::string& payload) {
	return "chunk " + type + " " + std::to_string(version) + " " + std::to_string(payload.size()) + "\n" + payload + "\n";
}

// A schema 1 save in the layout the schema 1 build wrote: macros and the combo log in the battle chunk's own lines,
// no quests or unlocks chunks, and an events-save 2 mid-event state.
std::string schemaOne() {
	std::string text = "PSR-SAVE 1\n";
	text += chunk("header", 1, "schema 1\nbuild game-0.1\nrules 2\ncontent 0\nwritten 5\nsequence 1\nautosave 1\nplay 4200\narea reed_path\nparty fighter:3 mystic:3\ndifficulty original\n");
	text += chunk("location", 1, "area reed_path\nposition 6 4\nfacing west\ncamera -\nvehicle -\n");
	text += chunk("party", 1, "lineup fighter mystic\nmember fighter 3 12 70 6\nwear weapon copper_blade\nmember mystic 3 12 30 20\nuses focus_shot 1\n");
	text += chunk("inventory", 1, "money 64\nitem tonic 2\nitem clear_gel 1\nbag tin_cap 1\n");
	text += chunk("world", 1, "flag tidewater.arrived 1\nflag tidewater.met_guide 1\nvisited tidewater\nvisited reed_path\ncontrol 1\nnext_enemy 3\ngrace 0\nrandom 0 0\nplay 4200\n");
	text += chunk("battle", 1, "macro pair\nstep fighter attack - enemy - 0\nstep mystic ability spark default - -1\nauto pair\ncombo storm_spark\n");
	text += chunk("random", 1, "stream 99\n");
	text += chunk("difficulty", 1, "difficulty original\nreward 1\n");
	text += chunk("midevent", 1, "state events-save%202%20old\n");
	return support::resign(text + "end 0\n");
}

}

ACC_CHECK(acc_sav_02, "ACC-SAV-02", "SAV-020, SAV-021") {
	const GameData& data = support::data(t);
	const std::string dir = support::tempDirectory("sav02");
	std::string error;

	// Migration: the oldest schema loads, then passes ACC-SAV-01's round trip.
	rules::Result<SaveFile> migrated = parseSave(schemaOne());
	if (t.expect(migrated.ok(), "a schema 1 save does not load: " + (migrated.ok() ? "" : migrated.error))) {
		const World& w = migrated.value->world;
		t.expect(w.macros.size() == 1 && w.macros[0].name == "pair" && w.autoMacro == "pair", "the schema 1 macro lines did not migrate to the save's macros");
		t.expect(w.comboLog.entries().size() == 1 && w.comboLog.entries()[0].recipe == "storm_spark", "the schema 1 combo line did not migrate to the combo log");
		t.expect(migrated.value->midEvent.empty(), "the schema 1 mid-event state, which no build can resume, was kept");
		const std::string text = serializeSave(*migrated.value);
		t.expect(text.compare(0, 12, "PSR-SAVE 13\n") == 0, "a migrated save is not written in the current schema");
		rules::Result<SaveFile> again = parseSave(text);
		std::string diff;
		t.expect(again.ok() && serializeSave(*again.value) == text && sameWorld(again.value->world, w, &diff), "a migrated save does not round-trip: " + diff);
	}
	Session session(data, support::options(dir));
	t.expect(writeFileAtomic(dir + "/manual_1.sav", schemaOne(), error), "setup: " + error);
	const rules::Message loaded = session.load("manual_1");
	t.expect(loaded.empty() && session.mode() == Mode::Field, "the game does not load a schema 1 save: " + session.render(loaded));
	t.expect(session.world().jobs.count("lost_cache") && session.world().unlocked.count("tidewater"), "the quests and unlocks a schema 1 save lacks were not filled from its flags");

	// Unknown chunks survive a load and a save through the game.
	const std::string unknown = chunk("ferry_routes", 4, "route tidewater bell_cove weekly\n");
	std::string withUnknown = serializeSave(session.makeSave(false));
	withUnknown.insert(withUnknown.find("chunk random"), unknown);
	withUnknown = support::resign(withUnknown);
	t.expect(writeFileAtomic(dir + "/manual_2.sav", withUnknown, error), "setup: " + error);
	session.quitToTitle();
	t.expect(session.load("manual_2").empty(), "a save with an unknown chunk does not load");
	t.expect(session.openMenu().empty() && session.saveManual("manual_3").empty(), "setup: the field menu cannot save");
	std::string written;
	readFile(dir + "/manual_3.sav", written);
	t.expect(written.find(unknown) != std::string::npos, "an unknown chunk was not written back unchanged after a load and a save");
	rules::Result<SaveFile> kept = parseSave(written);
	t.expect(kept.ok() && kept.value->world.unknownChunks.size() == 1 && kept.value->world.unknownChunks[0].version == 4, "the written save does not keep the unknown chunk's type and version");

	// Each older schema is the same save without the chunks later schemas added; each loads with those parts empty and
	// round-trips.
	auto older = [&](int schema, std::initializer_list<const char*> dropped) {
		std::string text = written;
		text.replace(0, 11, "PSR-SAVE " + std::to_string(schema));
		text.replace(text.find("schema 13\n"), 10, "schema " + std::to_string(schema) + "\n");
		if (schema <= 9) {
			// The header chunk's payload is a byte shorter.
			const std::size_t size = text.find("chunk header 1 ") + 15;
			text.replace(size, text.find('\n', size) - size, std::to_string(std::stoi(text.substr(size)) - 1));
		}
		if (schema <= 10) {
			text.replace(text.find("chunk actors 2 "), 15, "chunk actors 1 ");
			text.replace(text.find("chunk party 5 "), 14, "chunk party 4 ");
		}
		if (schema <= 9) {
			text.replace(text.find("chunk battle 3 "), 15, "chunk battle 2 ");
			text.replace(text.find("macros 2\n"), 9, "macros 1\n");
		}
		if (schema <= 7) {
			for (const char* chunk : {"party 4", "travel 3"}) {
				const std::size_t at = text.find(std::string("chunk ") + chunk + " ");
				if (at != std::string::npos) {
					text[at + 6 + std::string(chunk).size() - 1] -= 1;
				}
			}
		}
		if (schema <= 6) {
			for (const char* chunk : {"party 3", "world 2", "travel 2"}) {
				const std::size_t at = text.find(std::string("chunk ") + chunk + " ");
				if (at != std::string::npos) {
					text[at + 6 + std::string(chunk).size() - 1] -= 1;
				}
			}
		}
		for (const char* type : dropped) {
			const std::size_t at = text.find(std::string("chunk ") + type + " ");
			if (!t.expect(at != std::string::npos, std::string("a written save has no ") + type + " chunk")) {
				return rules::Result<SaveFile>::failure("setup");
			}
			const std::size_t next = text.find("chunk ", at + 1);
			text.erase(at, (next == std::string::npos ? text.rfind("\nend ") + 1 : next) - at);
		}
		rules::Result<SaveFile> migrated = parseSave(support::resign(text));
		const std::string name = "a schema " + std::to_string(schema) + " save";
		t.expect(migrated.ok() && serializeSave(*migrated.value).compare(0, 12, "PSR-SAVE 13\n") == 0, name + " does not migrate: " + (migrated.ok() ? "" : migrated.error));
		if (migrated.ok()) {
			std::string diff;
			rules::Result<SaveFile> again = parseSave(serializeSave(*migrated.value));
			t.expect(again.ok() && sameWorld(again.value->world, migrated.value->world, &diff), name + " does not round-trip after migrating: " + diff);
		}
		return migrated;
	};
	rules::Result<SaveFile> ten = older(10, {});
	t.expect(!ten.ok() || std::all_of(ten.value->world.actors.begin(), ten.value->world.actors.end(), [](const auto& actor) {
		return actor.second.vanish == ActorVanish::None && actor.second.animation.empty() && actor.second.removed == ActorRemoval::None;
	}), "a schema 10 save does not load with no actor vanished, animating or removed");
	rules::Result<SaveFile> nine = older(9, {});
	t.expect(!nine.ok() || std::none_of(nine.value->world.macros.begin(), nine.value->world.macros.end(), [](const rules::Macro& m) {
		return std::any_of(m.steps.begin(), m.steps.end(), [](const rules::MacroStep& step) { return step.piece; });
	}), "a schema 9 save does not load with no macro step naming a piece");
	rules::Result<SaveFile> seven = older(7, {});
	t.expect(!seven.ok() || (seven.value->world.shipPort.empty() && seven.value->world.brokenCells.empty() && seven.value->world.brokenHere.empty()),
		"a schema 7 save does not load ashore with no barrier broken");
	rules::Result<SaveFile> six = older(6, {});
	t.expect(!six.ok() || (six.value->world.follower.empty() && std::all_of(six.value->world.roster.begin(), six.value->world.roster.end(), [](const rules::PartyMember& m) { return m.taught.empty(); })),
		"a schema 6 save does not load with no follower and nothing taught");
	rules::Result<SaveFile> five = older(5, {"dev"});
	t.expect(!five.ok() || !five.value->world.devMarked, "a schema 5 save does not load unmarked by developer tools");
	rules::Result<SaveFile> four = older(4, {"travel", "dev"});
	t.expect(!four.ok() || (four.value->world.vehicles.empty() && four.value->world.aboard.empty() && four.value->world.dungeonExitArea.empty()),
		"a schema 4 save does not load on foot with no vehicles and no dungeon exit");
	rules::Result<SaveFile> three = older(3, {"talk", "travel", "dev"});
	t.expect(!three.ok() || three.value->world.ambientHeard.empty(), "a schema 3 save does not load with no party talk heard yet");
	rules::Result<SaveFile> two = older(2, {"actors", "talk", "travel", "dev"});
	t.expect(!two.ok() || two.value->world.actors.empty(), "a schema 2 save does not load with every actor at its talk point");

	// Newer saves are refused with a message, by schema and by chunk version.
	std::string newerSchema = written;
	newerSchema.replace(0, 11, "PSR-SAVE 14");
	std::string newerChunk = written;
	newerChunk.replace(newerChunk.find("chunk party 5"), 13, "chunk party 6");
	for (const auto& [what, text] : {std::make_pair(std::string("schema 13"), newerSchema), std::make_pair(std::string("a party chunk of version 6"), newerChunk)}) {
		rules::Result<SaveFile> refused = parseSave(support::resign(text));
		t.expect(!refused.ok() && refused.error.find("newer build") != std::string::npos, "a save with " + what + " was not refused as from a newer build");
	}
	t.expect(writeFileAtomic(dir + "/manual_4.sav", support::resign(newerSchema), error), "setup: " + error);
	session.closeMenu();
	session.quitToTitle();
	const rules::Message refused = session.load("manual_4");
	t.expect(refused.key == "game.refuse.save_damaged" && session.render(refused).find("newer build") != std::string::npos, "the game does not tell the player that a newer save cannot load");
	t.note("schema 1 (no build is released yet) migrates (macros and combo log to core's forms, quests and unlocks filled from flags, the unresumable mid-event state dropped), loads in the game and then round-trips; schema 2 gets empty actors, talk, travel and dev chunks, schema 3 empty talk, travel and dev chunks, schema 4 empty travel and dev chunks (no vehicles, on foot) and schema 5 an empty dev chunk (not marked by developer tools), each round-tripping; an unknown chunk survives a load and a manual save byte for byte; a newer schema and a newer chunk version are refused with a newer-build message the game shows");
	std::error_code ignored;
	std::filesystem::remove_all(dir, ignored);
}
