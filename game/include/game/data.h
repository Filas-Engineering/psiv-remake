#pragma once

#include "game/area.h"
#include "game/world.h"

#include "events/glossary.h"
#include "events/program.h"
#include "events/registry.h"
#include "events/strings.h"
#include "rules/database.h"
#include "rules/result.h"
#include "rules/setup.h"
#include "rules/text.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace game {

struct Boss {
	std::string encounter;
	// The story flag the boss's event sets when it falls; pacing keys M5 by it.
	std::string flag;
};

// A flag range (FLD-072, UI-012): holds while `after` is set (or empty) and `until` is not set (or empty).
struct FlagRange {
	std::string after;
	std::string until;
};

// The main objective line of the quest log; the last listed one whose range holds is shown.
struct Objective {
	std::string id;
	std::string text;
	FlagRange range;
};

// A guild job. Its state follows its flags and never moves back (UI-012).
struct Job {
	std::string id;
	std::string title;
	// Empty: available from the start.
	std::string availableFlag;
	std::string acceptedFlag;
	std::string completedFlag;
};

struct EncyclopediaEntry {
	std::string id;
	std::string title;
	std::string text;
	std::string flag;
};

struct TalkLine {
	std::string speaker;
	std::string text;
};

// A party talk set (FLD-072). Without an area it is a chapter set, the fallback for its flag range.
struct TalkSet {
	std::string id;
	std::string area;
	FlagRange range;
	std::vector<TalkLine> lines;
};

// journal.json. Every text is a string ID of the dialogue tables.
struct Journal {
	std::vector<Objective> objectives;
	std::vector<Job> jobs;
	std::vector<EncyclopediaEntry> encyclopedia;
	std::vector<TalkSet> partyTalk;
};

// Names the scripts may use for presentation; the engine owns the assets behind them.
struct PresentationNames {
	std::vector<std::string> audio;
	std::vector<std::string> cameras;
	std::vector<std::string> layouts;
	std::vector<std::string> animations;
	std::vector<std::string> portraits;
};

// The two tables of one language: the dialogue string table (events format) and the game's own text table
// (core format).
struct LanguageFiles {
	std::string code;
	std::string strings;
	std::string text;
};

// game.json: everything about the adventure that is not an area, a script or battle data.
struct GameConfig {
	std::string title;
	std::string coreData;
	std::vector<std::string> areaFiles;
	std::vector<std::string> eventFiles;
	std::string glossaryFile;
	// The first language is the one every other language must match key for key.
	std::vector<LanguageFiles> languages;
	std::string journalFile;
	// Script actors that are not characters; each needs a `speaker.<id>` string.
	std::vector<std::string> actors;
	PresentationNames presentation;
	// Every flag and variable a script, hook or journal entry uses, except chest flags, which the areas declare.
	std::vector<std::string> flags;
	std::map<std::string, int> vars;
	// The flag the last story event sets; control returning after it writes the clear-data record (SAV-024).
	std::string clearFlag;

	std::string startArea;
	std::string startMarker;
	Facing startFacing = Facing::South;
	std::vector<std::string> startLineup;
	int startLevel = 1;
	int startMoney = 0;
	rules::Inventory startItems;
	// Level a character has when an event first adds it.
	std::map<std::string, int> joinLevels;

	// Buy prices for items and equipment. Anything not listed cannot be bought or sold.
	std::map<std::string, int> prices;
	// SAV-036 is to measure; these are the proposed defaults.
	int itemCap = 99;
	int moneyCap = 999999;

	int tickMs = 50;
	int graceMs = 3000;
	// Battle time per resolved round, for play time and pacing; headless battles have no clock.
	int roundMs = 4000;
	int autosaveSlots = 3;
	int manualSlots = 10;
	std::vector<Boss> bosses;
};

struct LanguageTables {
	std::string code;
	// Core's text table merged with the game's own keys. Core ships English only, so every language uses it.
	rules::TextTable text;
	events::StringTable strings;
};

struct GameData {
	GameConfig config;
	rules::Database db;
	// In the order of game.json; the first is the fallback for an unknown language.
	std::vector<LanguageTables> languages;
	// String IDs the game shows itself (speaker names, journal text), so the string check counts them as used.
	events::GameStrings gameStrings;
	events::Program program;
	// Built from the data; the scripts were compiled against it.
	events::Registry registry;
	events::Glossary glossary;
	Journal journal;
	std::map<std::string, Area> areas;
	// Battle data hash mixed with the script fingerprint and the area texts.
	std::uint64_t contentHash = 0;

	const LanguageTables& language(const std::string& code) const;
	const Area* area(const std::string& id) const;
	const Boss* boss(const std::string& encounter) const;
	int price(const std::string& id) const;
	// Shop and rest point ids are unique across areas, so scripts can name them without an area.
	const Shop* shop(const std::string& id) const;
	const RestPoint* rest(const std::string& id) const;
};

struct DataDirectories {
	// Holds game.json; every file game.json names is relative to it.
	std::string game;
	// Core's battle data and text_en.json. Empty: game.json's core_data, relative to `game`.
	std::string core;
};

// Loads game.json, then the battle data, text tables, areas, journal, glossary and scripts it names, and checks
// every cross-reference. The scripts compile against a registry built from the rest, and must pass the strict
// check: string IDs only, no warnings, tables and glossary consistent. On failure every problem is in
// `problems` with a text key (problemText() has them in English) and in `error` as English lines.
rules::Result<GameData> loadGameData(const DataDirectories& directories);
// The same with game.json's core_data.
rules::Result<GameData> loadGameData(const std::string& directory);

// The English template of every key a load problem can carry: core's (load.*, json.*, text.*), the events
// library's (script.*, strings.*, names.*, registry.*, lint.*, timing.*) and the game's own (data.*).
const rules::TextTable& problemText();

// The registry in the events registry file format, for `event_cli check --registry`.
std::string registryText(const events::Registry& registry);
// The game's own string IDs in the events format, for `event_cli check --used`.
std::string gameStringsText(const events::GameStrings& strings);

// The string IDs of an ask's two answers, in every language's dialogue table.
inline constexpr const char* kChoiceYes = "choice.yes";
inline constexpr const char* kChoiceNo = "choice.no";

// Every key of the game's own text table the session can emit.
const std::vector<std::string>& sessionTextKeys();

}
