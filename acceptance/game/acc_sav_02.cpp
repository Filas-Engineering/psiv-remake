#include "support.h"

#include <filesystem>

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
		t.expect(text.compare(0, 11, "PSR-SAVE 3\n") == 0, "a migrated save is not written in the current schema");
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

	// Schema 2: the same save without the actors chunk loads with every actor at its talk point and round-trips.
	std::string schemaTwo = written;
	schemaTwo.replace(0, 10, "PSR-SAVE 2");
	schemaTwo.replace(schemaTwo.find("schema 3\n"), 9, "schema 2\n");
	const std::size_t actors = schemaTwo.find("chunk actors 1 ");
	if (t.expect(actors != std::string::npos, "a written save has no actors chunk")) {
		schemaTwo.erase(actors, schemaTwo.find("chunk ", actors + 1) - actors);
		rules::Result<SaveFile> two = parseSave(support::resign(schemaTwo));
		std::string diff;
		t.expect(two.ok() && two.value->world.actors.empty() && serializeSave(*two.value).compare(0, 11, "PSR-SAVE 3\n") == 0,
			"a schema 2 save does not migrate: " + (two.ok() ? "" : two.error));
		if (two.ok()) {
			rules::Result<SaveFile> again = parseSave(serializeSave(*two.value));
			t.expect(again.ok() && sameWorld(again.value->world, two.value->world, &diff), "a migrated schema 2 save does not round-trip: " + diff);
		}
	}

	// Newer saves are refused with a message, by schema and by chunk version.
	std::string newerSchema = written;
	newerSchema.replace(0, 10, "PSR-SAVE 4");
	std::string newerChunk = written;
	newerChunk.replace(newerChunk.find("chunk party 2"), 13, "chunk party 3");
	for (const auto& [what, text] : {std::make_pair(std::string("schema 4"), newerSchema), std::make_pair(std::string("a party chunk of version 3"), newerChunk)}) {
		rules::Result<SaveFile> refused = parseSave(support::resign(text));
		t.expect(!refused.ok() && refused.error.find("newer build") != std::string::npos, "a save with " + what + " was not refused as from a newer build");
	}
	t.expect(writeFileAtomic(dir + "/manual_4.sav", support::resign(newerSchema), error), "setup: " + error);
	session.closeMenu();
	session.quitToTitle();
	const rules::Message refused = session.load("manual_4");
	t.expect(refused.key == "game.refuse.save_damaged" && session.render(refused).find("newer build") != std::string::npos, "the game does not tell the player that a newer save cannot load");
	t.note("schema 1 (no build is released yet) migrates (macros and combo log to core's forms, quests and unlocks filled from flags, the unresumable mid-event state dropped), loads in the game and then round-trips; schema 2 gets an empty actors chunk and round-trips; an unknown chunk survives a load and a manual save byte for byte; a newer schema and a newer chunk version are refused with a newer-build message the game shows");
	std::error_code ignored;
	std::filesystem::remove_all(dir, ignored);
}
