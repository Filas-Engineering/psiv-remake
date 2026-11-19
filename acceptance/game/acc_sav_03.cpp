#include "support.h"

#include <filesystem>
#include <fstream>

using namespace game;

ACC_CHECK(acc_sav_03, "ACC-SAV-03", "SAV-013, SAV-014") {
	const GameData& data = support::data(t);
	const std::string dir = support::tempDirectory("sav03");
	std::string error;

	// A save with every chunk, the mid-event one included: the autosave before the boss battle.
	Session session(data, support::options(dir));
	std::string text;
	support::playThrough(t, session, [&](const std::string&) {
		for (const SaveSummary& summary : session.store()->list()) {
			std::string saved;
			if (text.empty() && session.store()->read(summary.slot, saved) && saved.find("chunk midevent") != std::string::npos) {
				text = saved;
			}
		}
	});
	if (!t.expect(!text.empty(), "the playthrough wrote no pre-battle autosave to damage")) {
		return;
	}
	const std::string original = text;
	int truncations = 0;
	int flips = 0;
	int harmless = 0;
	auto damaged = [&](const std::string& bad, const std::string& what) {
		rules::Result<SaveFile> r = parseSave(bad);
		if (!r.ok()) {
			return;
		}
		// Only the line break after the checksum is outside it; losing or changing that loses nothing.
		const bool same = serializeSave(*r.value) == original;
		harmless += same ? 1 : 0;
		t.expect(same, what + " loaded as a different save instead of being reported damaged");
	};
	for (std::size_t n = 0; n < original.size(); ++n) {
		++truncations;
		damaged(original.substr(0, n), "the save cut to " + std::to_string(n) + " bytes");
	}
	const unsigned char masks[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xff};
	for (std::size_t i = 0; i < original.size(); ++i) {
		for (unsigned char mask : masks) {
			std::string bad = original;
			bad[i] = static_cast<char>(static_cast<unsigned char>(bad[i]) ^ mask);
			++flips;
			damaged(bad, "the save with byte " + std::to_string(i) + " flipped by " + std::to_string(mask));
		}
	}

	// The game lists a damaged slot as damaged, refuses to load it and Continue skips it.
	session.quitToTitle();
	std::string bad = original;
	bad[bad.size() / 2] = static_cast<char>(bad[bad.size() / 2] ^ 0x04);
	t.expect(writeFileAtomic(dir + "/manual_5.sav", bad, error), "setup: " + error);
	bool listed = false;
	for (const SaveSummary& s : session.store()->list()) {
		listed = listed || (s.slot == "manual_5" && s.damaged);
	}
	t.expect(listed, "the slot list does not mark a damaged save as damaged");
	const rules::Message refused = session.load("manual_5");
	t.expect(refused.key == "game.refuse.save_damaged" && session.mode() == Mode::Title, "the game loaded a damaged save or did not say it is damaged");
	t.expect(session.store()->latest() != "manual_5", "Continue would pick a damaged save");

	// An interrupted write: the process stopped after writing part of the temporary file.
	std::string before;
	session.store()->read("manual_0", before);
	t.expect(!before.empty(), "setup: the playthrough's manual save is missing");
	{
		std::ofstream partial(dir + "/manual_0.sav.tmp", std::ios::binary);
		partial << before.substr(0, before.size() / 3);
	}
	std::string after;
	session.store()->read("manual_0", after);
	t.expect(after == before, "a half-written temporary file changed the slot it was replacing");
	t.expect(session.load("manual_0").empty(), "the previous save does not load next to a half-written temporary file");

	// A write that fails part way, here because the temporary file cannot be created: the old file stays whole and
	// play goes on.
	std::filesystem::remove(dir + "/manual_0.sav.tmp");
	std::filesystem::create_directory(dir + "/manual_0.sav.tmp");
	t.expect(session.openMenu().empty(), "setup: the field menu does not open");
	const rules::Message failed = session.saveManual("manual_0");
	after.clear();
	session.store()->read("manual_0", after);
	t.expect(!failed.empty() && after == before, "a failed write was reported as saved or damaged the previous save");
	t.expect(session.closeMenu().empty() && session.mode() == Mode::Field, "play does not continue after a failed write");
	std::filesystem::remove(dir + "/manual_0.sav.tmp");
	t.expect(session.openMenu().empty() && session.saveManual("manual_0").empty(), "the next save after a failed write does not work");

	t.note("pre-battle autosave of " + std::to_string(original.size()) + " bytes: all " + std::to_string(truncations) + " truncations and all " + std::to_string(flips) +
		" flips of every byte with each single-bit mask and 0xff are refused (" + std::to_string(harmless) +
		" load unchanged: they touch only the line break after the checksum); no damaged save crashed the loader; the slot list marks a damaged save, the game refuses it and Continue skips it; a half-written temporary file leaves the slot loadable and unchanged; a write that fails leaves the previous file byte-identical, reports the failure and play continues");
	std::error_code ignored;
	std::filesystem::remove_all(dir, ignored);
}
