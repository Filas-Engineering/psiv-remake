#pragma once

#include "game/area.h"
#include "game/world.h"

#include "events/glossary.h"
#include "events/host.h"
#include "events/program.h"
#include "events/registry.h"
#include "events/strings.h"
#include "rules/database.h"
#include "rules/result.h"
#include "rules/setup.h"
#include "rules/text.h"

#include <cstdint>
#include <map>
#include <optional>
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
	// Once set, a job not yet accepted is missed for good.
	std::string missedFlag = {};
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

// What a camera looks at. None leaves it to the host; Marker is a marker in one area; Follow is an actor or a character
// (a party member); Leader is the party leader.
enum class CameraTarget { None, Marker, Follow, Leader };

const char* toString(CameraTarget target);

// A camera scripts may name (EVT-033, EVT-035). The game only names it in actions; its target and zoom are data for the
// host to frame panels and the scene with.
struct CameraDef {
	std::string id;
	CameraTarget target = CameraTarget::None;
	// Marker: the area and the marker. Follow: the actor or character id.
	std::string area;
	std::string marker;
	std::string follow;
	// The view's scale against the field's own, 1 when the data gives none.
	double zoom = 1;
};

// Tracks by situation, outside scripts (which name their own). Every one is an id of PresentationNames::audio; empty
// means the data names none.
struct AreaTrack {
	// Empty: always matches.
	std::string flag;
	std::string track;
};

struct MusicNames {
	std::string title;
	std::string victory;
	std::string defeat;
	// By encounter id.
	std::map<std::string, std::string> encounters;
	// By FieldVehicle id: random battles fought aboard, unless the encounter has its own track.
	std::map<std::string, std::string> vehicles;
	// By area id, in order; the first whose flag is set (or that has none) gives the area's track, in place of the area's own.
	std::map<std::string, std::vector<AreaTrack>> areas;
};

// Names the scripts may use for presentation; the engine owns the assets behind them. None of this is part of the
// content hash.
struct PresentationNames {
	std::vector<std::string> audio;
	std::vector<CameraDef> cameras;
	std::vector<std::string> layouts;
	// Every name listed, the standard ones (events::standard_animations) included when the data lists them.
	std::vector<std::string> animations;
	// Seconds an animation lasts, for the names listed with them; in place of a standard animation's own length.
	std::map<std::string, double> animationSeconds;
	std::vector<std::string> portraits;
	MusicNames music;
	// Authored seconds per action type, and per type and subject (BAT-102, EVT-062), in the timing rule of
	// events::action_seconds.
	std::map<events::ActionType, double> durations;
	std::map<std::pair<events::ActionType, std::string>, double> subjectDurations;

	const CameraDef* camera(const std::string& id) const;
	// The length of one play of `name`: the data's seconds, else a standard animation's own, else 0.
	double animationLength(const std::string& name) const;
};

// One entry of a credits sequence (game.json `credits`): a heading or a line, each a text key of the text tables.
enum class CreditsEntryKind { Heading, Line };

struct CreditsEntry {
	CreditsEntryKind kind = CreditsEntryKind::Line;
	std::string key;
	// Empty lines a front end leaves before the entry.
	int gap = 0;
	// How long the entry holds before the next one, which is when the session counts it as shown.
	int pauseMs = 2000;
};

// The credits a script's `credits <id>` plays, in order. Not part of the content hash.
struct Credits {
	std::string id;
	// An audio id from presentation.audio; empty keeps the track playing.
	std::string music;
	std::vector<CreditsEntry> entries;
	int line = 0;
};

// How an actor walks about by itself (game.json actors' `wander`), around its home, a cell at a time, by the same field ticks
// and encounter stream as visible enemies' wander.
struct ActorWander {
	// Cells at most this far (along x plus along y) from its home; 0 with a region.
	int radius = 0;
	// Or the cells of this rectangle, both corners included.
	std::optional<Cell> from = std::nullopt;
	std::optional<Cell> to = std::nullopt;
	// Its speed against the area's step time: 50 crosses a cell in twice the leader's walking time.
	int speedPercent = 50;
	// How long it stands after each step, drawn between the two (no draw when they are equal).
	int pauseMinMs = 1000;
	int pauseMaxMs = 3000;
	// The map characters it may stand on.
	std::string terrain = ".";

	bool covers(Cell home, Cell cell) const;
};

// One of game.json's `actors`. A plain id stands at the talk point named after it; an object can also draw it with
// another figure and give it a home that is not a talk point.
struct ActorDef {
	std::string id;
	// Drawn as that enemy's field figure (an enemy id of the battle data), or with a look the host names; empty for
	// the host's own figure for the actor. The actor never fights.
	std::string spriteEnemy;
	std::string look;
	// A home with no talk point: the area, and a marker of it or a cell, where the actor stands facing `facing` until
	// a script moves it. Empty area for none.
	std::string area;
	std::string marker;
	std::optional<Cell> at;
	Facing facing = Facing::South;
	// The cell it stands on blocks walking while it is shown there.
	bool solid = true;
	int line = 0;
	// Walks about its home by itself; needs a home.
	std::optional<ActorWander> wander = std::nullopt;
};

// The two tables of one language: the dialogue string table (events format) and the game's own text table
// (core format).
struct LanguageFiles {
	std::string code;
	std::string strings;
	std::string text;
};

// How a battle fought aboard a vehicle opens: rolled from the vehicle's agility like a random battle on foot, or always
// normally, with no roll drawn.
enum class VehicleOpening { Roll, Normal };

// How a vehicle moves in the field (FLD-061). Its battle stats, attack and skills are core's vehicle of the same id,
// which fights its vehicle battles (BAT-140).
struct FieldVehicle {
	std::string id;
	// Map characters it crosses: '.' floor, '~' water, ':' soft ground. Walls stop every vehicle.
	std::string crosses;
	// Map characters it breaks through, turning them to floor (F3): '%' barriers.
	std::string breaks = {};
	// Whether a broken cell stays floor for good; otherwise it is whole again once the party leaves the area.
	bool breaksForGood = true;
	// An item that calls the vehicle from the item list anywhere outdoors (F3): it comes to the party, who board it.
	std::string callItem = {};
	// Its speed against walking: 200 crosses a cell in half the area's step time.
	int speedPercent = 100;
	// The encounter table of battles while riding it (ENC-007).
	std::vector<TableEntry> encounters;
	// Its own opening rule, in place of GameConfig::vehicleOpening.
	std::optional<VehicleOpening> opening = std::nullopt;

	bool canCross(char terrain) const { return crosses.find(terrain) != std::string::npos || breaks.find(terrain) != std::string::npos; }
};

// What a member who leaves the party does with its equipment (P2, SAV-035): keeps wearing it, so it goes with it, or
// gives it back to the bag.
enum class LeaveEquipment { Keep, Bag };

// What interact acts on (game/README.md, "Interaction"): the cell the leader faces, or, when that holds nothing, the first
// neighbor that answers, facing first, the leader turning to it.
enum class InteractReach { Faced, Nearby };

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
	// Script actors that are not characters; each needs a `speaker.<id>` string unless it is nameless.
	std::vector<std::string> actors;
	// One per id of `actors`, in the same order.
	std::vector<ActorDef> actorDefs;
	const ActorDef* actorDef(const std::string& id) const;
	// Actors with no name (townsfolk): their lines show no speaker, and they have no `speaker.<id>` string.
	std::vector<std::string> namelessActors;
	bool nameless(const std::string& actor) const;
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
	std::vector<FieldVehicle> vehicles;
	// Hit points a machine member regains with each step the party takes in the field (P6).
	int machineStepHeal = 0;
	// Members who have joined and are out of the party get the same experience share as those in it until this flag
	// is set (SAV-032); empty gives them none.
	std::string outsideExperienceUntil = {};
	LeaveEquipment leaveEquipment = LeaveEquipment::Keep;
	// Every area plays in random mode (F1, ENC-002): one switch for the whole game, off in the placeholder.
	bool randomEncountersEverywhere = false;
	// Every zone's grace steps start again after every battle, as a zone's grace_after_battle does.
	bool graceAfterBattle = false;
	InteractReach interactReach = InteractReach::Faced;
	// Every point is used across a counter, as a hook with `across` is.
	bool interactAcross = false;
	// The leader's speed on foot while running against walking: 200 crosses a cell in half the step time. 0: no running.
	int runSpeedPercent = 0;
	VehicleOpening vehicleOpening = VehicleOpening::Roll;
	// Background events that may run at once (events/FORMAT.md, "Background events"); a spawn past it is dropped with a
	// warning.
	int backgroundLimit = 4;
	std::vector<Credits> credits = {};
	const Credits* creditsById(const std::string& id) const;
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
	// Port ids are unique across areas too. `area` is set to the port's area.
	const Port* port(const std::string& id, const Area** area = nullptr) const;
	const FieldVehicle* vehicle(const std::string& id) const;
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
// Loads like loadGameData but keeps in `out` whatever loaded, and returns every problem (none when the data
// loaded). A battle data problem does not stop the game's own files from being read; the scripts are compiled
// and the cross-references checked only when everything before them loaded; `referencesChecked` says whether
// the load got that far. The data check (check.h) uses it.
std::vector<rules::Problem> loadGameDataPartly(const DataDirectories& directories, GameData& out, bool* referencesChecked = nullptr);

// The English template of every key a load problem can carry: core's (load.*, json.*, text.*), the events
// library's (script.*, strings.*, names.*, registry.*, lint.*, timing.*) and the game's own (data.*).
const rules::TextTable& problemText();

// The registry in the events registry file format, for `event_cli check --registry`.
std::string registryText(const events::Registry& registry);

// The reserved actor name a script addresses a visible enemy by, `enemy.<zone>.<n>`: the n-th (from 1) of the zone's
// visible enemies still in the field, in the order they spawned. The registry lists one per place of each zone's cap.
std::string visibleEnemyActor(const std::string& zone, int n);
bool parseVisibleEnemyActor(const std::string& actor, std::string& zone, int& n);
// The game's own string IDs in the events format, for `event_cli check --used`.
std::string gameStringsText(const events::GameStrings& strings);

// The string IDs of an ask's two answers, in every language's dialogue table.
inline constexpr const char* kChoiceYes = "choice.yes";
inline constexpr const char* kChoiceNo = "choice.no";

// Every key of the game's own text table the session can emit.
const std::vector<std::string>& sessionTextKeys();

// What a description describes. A description is the optional text key `desc.<kind>.<id>` of the game's text tables
// (UI-007): a missing one shows as empty, and the content hash leaves them out, so writing or editing one changes no
// save or record.
enum class DescriptionKind { Item, Equipment, Ability, Status, Enemy, Difficulty };

const char* toString(DescriptionKind kind);
// The text key of an id's description.
std::string descriptionKey(DescriptionKind kind, const std::string& id);
// Whether `id` is a thing of that kind in the battle data.
bool describes(const rules::Database& db, DescriptionKind kind, const std::string& id);
inline constexpr const char* kDescriptionPrefix = "desc.";

// A thing's name in one language is the optional text key `name.<kind>.<id>`, with kind character, ability, item,
// equipment, status, enemy, vehicle or difficulty; without one the battle data's name shows. Like descriptions they
// are left out of the key-for-key rule and the content hash.
inline constexpr const char* kNamePrefix = "name.";
// Whether `key` is `name.<kind>.<id>` for a thing the battle data has.
bool namesKnownThing(const rules::Database& db, const std::string& key);

}
