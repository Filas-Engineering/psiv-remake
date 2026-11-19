#include "support.h"

#include <filesystem>
#include <map>

using namespace game;

ACC_CHECK(acc_sav_01, "ACC-SAV-01", "SAV-010, SAV-011") {
	const GameData& data = support::data(t);
	const std::string dir = support::tempDirectory("sav01");
	Session session(data, support::options(dir));
	int points = 0;
	int files = 0;
	int midEvent = 0;
	std::map<std::string, std::string> checkedFiles;
	auto roundTrip = [&](const std::string& text, const std::string& where, const World* live) {
		rules::Result<SaveFile> loaded = parseSave(text);
		if (!t.expect(loaded.ok(), where + ": the save does not load: " + (loaded.ok() ? "" : loaded.error))) {
			return;
		}
		t.expect(serializeSave(*loaded.value) == text, where + ": load then save is not byte-identical");
		SaveFile later = *loaded.value;
		later.writeTime += 86400000;
		const std::string resaved = serializeSave(later);
		t.expect(resaved != text && support::withoutWriteTime(resaved) == support::withoutWriteTime(text), where + ": a save with a new write time differs in more than the write time");
		if (live) {
			std::string diff;
			t.expect(sameWorld(loaded.value->world, *live, &diff), where + ": field " + diff + " differs after the round trip");
		}
		midEvent += loaded.value->midEvent.empty() ? 0 : 1;
	};
	support::playThrough(t, session, [&](const std::string& line) {
		if (session.mode() == Mode::Title) {
			return;
		}
		++points;
		roundTrip(serializeSave(session.makeSave(false)), "after '" + line + "'", &session.world());
		for (const auto& entry : std::filesystem::directory_iterator(dir)) {
			if (entry.path().extension() != ".sav" || entry.path().filename() == "clear.sav") {
				continue;
			}
			std::string text;
			readFile(entry.path().string(), text);
			std::string& seen = checkedFiles[entry.path().filename().string()];
			if (seen != text) {
				seen = text;
				++files;
				roundTrip(text, entry.path().filename().string() + " written before '" + line + "'", nullptr);
			}
		}
	});
	t.expect(points + files >= 50, "only " + std::to_string(points + files) + " save points were taken; the check needs 50");
	t.expect(midEvent > 0, "the playthrough wrote no autosave with mid-event state, so that chunk was not round-tripped");
	t.note(std::to_string(points) + " saves taken after each command of game/data/scripts/playthrough.txt and " + std::to_string(files) +
		" save files the game wrote on the way (" + std::to_string(midEvent) + " with mid-event state) load and save again byte-identically; a new write time changes only the header's write time and the checksum; every SAV-001 field compares equal field by field (sameWorld) to the live world; camera volume and vehicle round-trip empty, since the adventure has no camera volumes and no vehicle in the field (its skimmer fights only in an event battle)");
	std::error_code ignored;
	std::filesystem::remove_all(dir, ignored);
}
