#pragma once

#include "game/world.h"

#include "rules/result.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace game {

// Schema 2 added the party's statuses and names, core's macro and combo log forms, and the quests and unlocks
// chunks; schema 3 the actors chunk; schema 4 the talk chunk; schema 5 the travel chunk; schema 6 the dev chunk. Older
// schemas are migrated in order when they load (SAV-021).
constexpr int kSaveSchemaVersion = 16;
constexpr const char* kSaveMagic = "PSR-SAVE";
constexpr const char* kClearMagic = "PSR-CLEAR";
constexpr int kClearVersion = 1;
constexpr const char* kGameBuild = "game-0.2";

struct SaveFile {
	World world;
	// events::save_state_to_text of the interpreter, only in the autosave before an event battle (EVT-050).
	std::string midEvent;
	// Each running background event, in start order: its name and events::save_state_to_text of its interpreter (the
	// `background` chunk, written only when one runs).
	std::vector<std::pair<std::string, std::string>> background = {};
	std::int64_t writeTime = 0;
	std::uint64_t sequence = 0;
	bool autosave = false;
	std::uint64_t contentHash = 0;
	// Each lineup member's maximum hit and technique points with its pieces, for the header's `member` lines, which a
	// slot list reads without the data (Session::makeSave fills it; a member it lacks is written as -1).
	std::map<std::string, std::pair<int, int>> memberMaxima = {};
};

// A lineup member as the header gives it. Saves from before schema 16 give the id and level only; the rest is -1.
struct SaveMemberSummary {
	std::string id;
	int level = 0;
	int hp = -1;
	int maxHp = -1;
	int tp = -1;
	int maxTp = -1;
	// Status ids that persist outside battle.
	std::vector<std::string> statuses;
};

// What a slot list shows (SAV-002), read from the header chunk alone.
struct SaveSummary {
	std::string slot;
	std::string area;
	std::int64_t playTimeMs = 0;
	std::vector<std::string> party;
	std::string difficulty;
	std::int64_t writeTime = 0;
	std::uint64_t sequence = 0;
	bool autosave = false;
	// The game is marked by developer tools (World::devMarked).
	bool dev = false;
	bool damaged = false;
	std::string error;
	// The save SaveStore::latest picks (the newest by write time, then sequence, damaged ones aside); one per list at most.
	bool newest = false;
	// -1 in a save from before schema 16.
	int money = -1;
	// The lineup in order, from the `party` line and the `member` lines.
	std::vector<SaveMemberSummary> members;
};

// The chunked text format described in README.md. Chunks the loader did not recognize are written back from
// world.unknownChunks.
std::string serializeSave(const SaveFile& save);

// Verifies magic, schema version and checksum, then migrates an older schema.
rules::Result<SaveFile> parseSave(const std::string& text);

// The world chunks alone, without header or checksum: the opaque blob events::Host::snapshot_world returns.
std::string serializeWorldChunks(const World& world);
bool parseWorldChunks(const std::string& text, World& world, std::string& error);

rules::Result<SaveSummary> readSummary(const std::string& text);

// The clear-data record (SAV-024): the header, party, inventory and world chunks of the save format under its own
// magic, so a later product can read the final party and flags.
std::string serializeClearRecord(const World& world, std::uint64_t contentHash, std::int64_t writeTime);
rules::Result<World> parseClearRecord(const std::string& text);

// Paths are UTF-8 on every platform.
// Writes `path.tmp`, then renames it over `path`, so an interrupted write leaves the old file (SAV-013).
bool writeFileAtomic(const std::string& path, const std::string& text, std::string& error);
bool readFile(const std::string& path, std::string& text);

// Manual slots manual_0 .. manual_{n-1} and autosave slots auto_0 .. auto_{n-1} in one directory.
class SaveStore {
public:
	SaveStore(std::string directory, int manualSlots, int autosaveSlots);

	const std::string& directory() const { return directory_; }
	std::string path(const std::string& slot) const;
	bool validSlot(const std::string& slot) const;

	bool write(const std::string& slot, const std::string& text, std::string& error) const;
	bool read(const std::string& slot, std::string& text) const;
	// Removes the slot's file. False, with the reason, when the file could not be removed.
	bool erase(const std::string& slot, std::string& error) const;

	// Every slot that has a file, manual slots first.
	std::vector<SaveSummary> list() const;
	// The autosave slot the next autosave writes: an empty one, otherwise the oldest (FLOW-054).
	std::string nextAutosaveSlot() const;
	// The clear-data record and the global profile sit next to the slots.
	std::string clearRecordPath() const { return directory_ + "/clear.sav"; }
	std::string profilePath() const { return directory_ + "/profile.cfg"; }
	// The newest save by write time, manual or autosave (FLOW-021). Empty when none exists or all are damaged.
	std::string latest() const;
	// One past the highest sequence number on disk.
	std::uint64_t nextSequence() const;

private:
	std::string directory_;
	int manualSlots_;
	int autosaveSlots_;
};

}
