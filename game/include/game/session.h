#pragma once

#include "game/area.h"
#include "game/data.h"
#include "game/encounters.h"
#include "game/modes.h"
#include "game/pacing.h"
#include "game/profile.h"
#include "game/save.h"
#include "game/world.h"

#include "events/host.h"
#include "events/interpreter.h"
#include "rules/battle.h"
#include "rules/message.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace game {

// Ambient: a party talk line that came up by itself on arrival (FLD-073), shown without stopping play.
enum class OutputKind { Line, Choice, Action, Notice, Warning, Autosave, Ambient };

const char* toString(OutputKind kind);

// Something the front end shows. Battle rounds are returned as core events instead. `message` is the text key
// and its parameters; `text` is it rendered in the profile's language. Warnings about broken data have no key.
struct Output {
	OutputKind kind = OutputKind::Notice;
	rules::Message message;
	std::string text;
};

// One page of the text log (TXT-027). Kept as written, so it shows names under the current setting (TXT-029).
struct LogEntry {
	std::string speaker;
	events::Text text;
	bool skipped = false;
};

struct JobView {
	std::string id;
	std::string title;
	JobState state = JobState::Available;
};

struct QuestLog {
	// Empty when no objective's flag range holds.
	std::string objective;
	std::vector<JobView> jobs;
};

struct EncyclopediaView {
	std::string id;
	std::string title;
	std::string text;
};

// Macro slots per save: the original's eight (BAT-055, BAT-057).
inline constexpr int kMaxMacros = 8;

// What the Menu mode is showing. Shop has its own mode. Port: the destinations of the port the party is at.
enum class MenuKind { None, Field, Inn, SavePrompt, NameEntry, Port, Credits };

const char* toString(MenuKind kind);

// One entry of the credits playing, its text in the profile's language.
struct CreditsEntryView {
	CreditsEntryKind kind = CreditsEntryKind::Line;
	std::string key;
	std::string text;
	int gap = 0;
	int pauseMs = 0;
};

// The credits a script's `credits <id>` plays (MenuKind::Credits): the entries and how far play time has come through
// them. Entry i is shown once elapsedMs reaches the pauses of the entries before it, so the first is shown at once.
struct CreditsView {
	std::string id;
	std::string music;
	std::vector<CreditsEntryView> entries;
	int shown = 0;
	// Play time since the credits began, at most totalMs, the sum of every entry's pause.
	std::int64_t elapsedMs = 0;
	std::int64_t totalMs = 0;
};

// The pending event line, ready to show.
struct LineView {
	// Empty for narration.
	std::string speakerId;
	// The chosen name, the character's name or the actor's speaker string; empty for narration.
	std::string speakerName;
	// The string ID of the text, or empty for inline text.
	std::string textId;
	// Looked up in the profile's language, name tokens resolved for its "show shipped names".
	std::string text;
	// The portrait the line starts with, or empty.
	std::string portrait;
};

// The pending choice, ready to show. An ask's options are the string table's choice.yes and choice.no.
struct ChoiceView {
	// Empty when the script gave no prompt.
	std::string prompt;
	// The choice's `header`, shown with the options; headerId is its string id, empty for quoted text.
	std::string header;
	std::string headerId;
	bool yesNo = false;
	std::vector<std::string> options;
};

// Plays presentation-only script actions (moves, cameras, panels, audio). Warps are applied to the world before
// this is called. Without one, the session's own action queue is used (see Presentation).
class Presenter {
public:
	virtual ~Presenter() = default;
	// Returns true when the action has already finished.
	virtual bool begin(std::uint64_t handle, const events::Action& action) = 0;
	virtual bool finished(std::uint64_t handle) = 0;
	virtual void cancel(std::uint64_t handle) = 0;
};

// How the session's own action queue finishes presentation actions. Instant: every action is done when it starts,
// which the terminal and the tests need. Queued: the event waits on each action until the host calls
// finishAction, so a renderer can play animations of real length. An action with `instant` set (the rebuild after
// a skip or a restore) is done at once either way.
enum class Presentation { Instant, Queued };

enum class ActionPhase { Begin, Cancel };

// A presentation action for the host to play, from pollActions(). Cancel names an action a skip abandoned; it
// carries no action.
struct PresentedAction {
	std::uint64_t handle = 0;
	ActionPhase phase = ActionPhase::Begin;
	events::Action action;
	std::string description;
	// How long the action's animation lasts by the data: game.json's presentation durations for its type and subject,
	// else its type, else the action's own seconds (events::action_seconds); 0 for none.
	double seconds = 0;
	// A move or place of one of the game's actors: the cells it passes, from where it stood to where it ends; one cell
	// when it had no cell in the area or was placed. Empty for any other action.
	std::vector<Cell> route = {};
};

struct SessionOptions {
	// Empty disables writing: autosave points are still counted and reported. The global profile and the
	// clear-data record live there too.
	std::string saveDirectory;
	// Replaces every area's encounter mode, so pacing can be compared without editing areas (ENC-002).
	std::optional<EncounterMode> encounterMode;
	std::uint64_t seed = 1;
	// Save write time. Defaults to a counter so runs are reproducible.
	std::function<std::int64_t()> clock;
	// Replaces the session's own action queue; presentation, onAction, pollActions and finishAction then do nothing.
	Presenter* presenter = nullptr;
	Presentation presentation = Presentation::Instant;
	// Called as each action is queued, besides pollActions; it may call finishAction and nothing else.
	std::function<void(const PresentedAction&)> onAction;
};

// The direction held this frame. A press shorter than a tick still counts: the session keeps the last direction
// pressed until the next tick, which plays it as held once, so a tap turns the leader.
struct FieldInput {
	bool held = false;
	Facing direction = Facing::South;
	// The host's run input. The leader runs on foot while it is held, or while it is not with the profile's always_run,
	// when game.json gives a run_speed_percent; otherwise it changes nothing.
	bool running = false;
};

enum class BattleSource { Visible, Random, Event };

const char* toString(BattleSource source);

struct LevelUpView {
	std::string member;
	int level = 0;
	std::vector<std::string> learned;
};

// A battle after it ended, as resolveRound left it. Kept until the next battle starts, dismissBattle(), a new
// game, a load or quitToTitle().
struct EndedBattle {
	// The final combatants, the outcome and the record.
	rules::Battle battle;
	std::string encounter;
	BattleSource source = BattleSource::Visible;
	rules::Outcome outcome = rules::Outcome::Ongoing;
	bool gameOver = false;
	// What was paid out: experience, money and drops after the reward multiplier; empty unless the party won or
	// reached a scripted end.
	rules::Rewards rewards;
	std::vector<LevelUpView> levelUps;
};

// One round of playBattle(): the output drained before the round was resolved, then the round's events.
struct AutoRound {
	std::vector<Output> output;
	std::vector<rules::Event> events;
	// The round's macro trouble (Session::lastMacroTrouble), empty when no macro filled it.
	rules::MacroTroubleReport trouble;
};

struct AutoBattle {
	// Why auto-battle with the save's auto macro did not start, or empty.
	rules::Message refusal;
	std::vector<AutoRound> rounds;
	// False when the round limit was reached first.
	bool ended = false;
};

// One step of a saved macro, ready for the macro editor (UI-005).
struct MacroStepView {
	rules::MacroStep step;
	std::string memberName;
	// The ability's or item's display name; empty for attack, defend and escape steps.
	std::string commandName;
	// The display name of a Member target, or empty.
	std::string targetName;
	// Why the step cannot run as written now: NotInParty, ActorDefeated, UnknownAbility, NotEnoughTp, NoUsesLeft or
	// NoItem (for a piece step: the member does not wear it and the bag holds none it may cast from there); None when
	// nothing stops it outside battle.
	rules::MacroIssue issue = rules::MacroIssue::None;
};

struct MacroView {
	std::string name;
	std::vector<MacroStepView> steps;
	// The macro's own options, Default where it leaves the choice to the rules or the caller (setMacroOptions).
	rules::MacroTrouble onTrouble = rules::MacroTrouble::Default;
	rules::MacroRecoverWho recoverWho = rules::MacroRecoverWho::Default;
	rules::MacroUnnamed unnamed = rules::MacroUnnamed::Default;
	rules::MacroPolicy policy = rules::MacroPolicy::Default;
};

// One combo log entry joined with its recipe (UI-011).
struct ComboView {
	std::string recipe;
	// The combination's ability.
	std::string result;
	std::string resultName;
	// The abilities the recipe needs, in recipe order.
	std::vector<std::string> abilities;
	std::int64_t playTimeMs = 0;
	// Character ids of the participants, in the order they acted.
	std::vector<std::string> characters;
};

// What a status screen shows beyond the member's own state.
struct MemberDetail {
	std::string member;
	// Experience still needed for the next level; 0 at the level cap.
	int experienceToNext = 0;
	// Level stats plus equipment.
	rules::Stats stats;
	// Each worn piece's bonus, by slot.
	std::map<rules::EquipSlot, rules::Stats> equipmentBonus;
	// The worn weapon's element, or None.
	rules::Element attackElement = rules::Element::None;
	// Each element whose damage the member takes at other than the normal rate, in the data's element order, with that
	// affinity (on the rules' divisor scale).
	std::vector<std::pair<rules::Element, int>> affinities;
	// The character's status resistances plus its pieces', capped at 100, as a battle combatant gets them.
	rules::StatusResists statusResist;
	// The character's `profile` rows from party.json, in order (R111).
	std::vector<rules::ProfileRow> profile;
	// Strength plus both hands' weapon `stats.attack`: what basic attacks and `attack_stat: "attack"` hit with.
	int attack = 0;
	// The level's stats without equipment.
	rules::Stats baseStats;
	// The experience this level takes in all (experience counts from 0 at each level); 0 at the level cap.
	int experienceForLevel = 0;
	// Every point of experience it has had: what each level before this one took, plus what it has in this one.
	int totalExperience = 0;
	rules::CreatureKind kind = rules::CreatureKind::Living;
	// Instant defeat can strike it (party.json `vulnerable`).
	bool vulnerable = false;
};

// Orders for listing the bag. The bag itself has no order (items and pieces are kept by id), so a sort is a read and
// nothing about it is saved. Kind: field items, battle-only items, then pieces by slot; Name: display name; Count: most
// held first. Ties go to the display name, then the id.
enum class ItemSort { Id, Kind, Name, Count };

const char* toString(ItemSort sort);

// A party talk line for a host's own page: the speaker's id and shown name, the line's string id and its text.
struct TalkLineView {
	std::string speaker;
	std::string speakerName;
	std::string textId;
	std::string text;
};

struct BagEntry {
	std::string id;
	int count = 0;
	bool equipment = false;
};

// A difficulty preset to offer (BAT-120, UI-014): core's name and the description that says what it changes.
struct DifficultyView {
	std::string id;
	std::string name;
	std::string description;
	int rewardPercent = 100;
};

// A member's battle numbers, as rules::memberCombatant builds them for a battle.
struct CombatView {
	rules::Stats stats;
	std::string basicAttack;
	rules::Element attackElement = rules::Element::None;
	std::vector<std::string> abilities;
	rules::StatusResists statusResist;
};

// A piece compared on a member before it is put on (UI-008, UI-009). `after` is the member wearing the piece in place
// of `replaces`; it equals `before` when the member cannot wear it.
struct EquipPreview {
	std::string member;
	rules::EquipmentDef piece;
	// Where the piece would go (P10).
	rules::EquipSlot position = rules::EquipSlot::Weapon;
	// The piece worn there now, or empty.
	std::string replaces;
	// The other hand's piece it also sends back to the bag: beside a two-handed weapon, or held when one goes on.
	std::string alsoReplaces = {};
	// Empty when the member can wear the piece, otherwise core's refusal (equip.wrong_category).
	rules::Message refusal;
	// Lineup members whose equipment categories allow the piece, in lineup order.
	std::vector<std::string> wearers;
	CombatView before;
	CombatView after;
};

// One of the game's actors standing in the party's area, from Session::actorsHere(). An actor no script touched
// stands at the talk point named after it, visible and facing south; script moves, turns, shows and hides change
// that (see README.md, "Actors").
struct ActorView {
	std::string id;
	Cell cell;
	Facing facing = Facing::South;
	bool visible = true;
	// A script has moved, turned, shown or hidden it.
	bool scripted = false;
	// It blocks walking on its cell now: a solid actor (game.json `solid`, or a talk point with `actor_at`) shown there.
	bool solid = false;
	ActorVanish vanish = ActorVanish::None;
	// The one-shot animation playing, with its milliseconds left and in all; empty when none plays.
	std::string animation = {};
	int animationMs = 0;
	int animationTotalMs = 0;
	// A removed actor is not shown, does not block and its talk point does not answer.
	ActorRemoval removed = ActorRemoval::None;
	// Its figure: a script's `look`, else game.json's: an enemy's field sprite, or a look the host names; both empty for the
	// host's own.
	std::string spriteEnemy = {};
	std::string look = {};
	// It walks about by itself now (game.json `wander`): shown, not removed, in its home area and in no running event.
	bool wanders = false;
	// It walks its patrol now (game.json `patrol`), under the same conditions.
	bool patrols = false;
	// It moves along its path now (game.json `path`), under the same conditions. The path's offsets are from `home` (its
	// game.json home, or where a script last put it; only path actors report one), in pixels at game.json's `cell_pixels`: draw it at home times the cell
	// size plus (pathX, pathY). `cell` is the cell holding that point. With a footprint it blocks every cell the footprint
	// (cells wide and high, from that point) overlaps; without one it blocks nothing.
	bool onPath = false;
	Cell home;
	int pathX = 0;
	int pathY = 0;
	int footprintWidth = 0;
	int footprintHeight = 0;
};

// One who walks behind the leader (a lineup member after the first, then the script's follower actor) and the cell it
// stands on: the leader's trail (Session::followers). It blocks nothing.
struct FollowerView {
	std::string id;
	Cell cell;
};

// A moving platform of the party's area (an area's `platforms`), from Session::platformsHere().
struct PlatformView {
	std::string id;
	PlatformMode mode = PlatformMode::Loop;
	Cell from;
	Cell to;
	// The cell it stands on now (the nearest along its run) and its exact place in pixels at game.json's `cell_pixels`,
	// the top left of the cell it covers.
	Cell cell;
	int x = 0;
	int y = 0;
	// At an end, where the party may step on or off: 0 at `from`, 1 at `to`, -1 between them.
	int end = -1;
	// The milliseconds it still rests at that end before it may leave.
	int pauseMs = 0;
	// The party rides it.
	bool carrying = false;
	// Microseconds along its run from `from`, and the whole run.
	std::int64_t positionUs = 0;
	std::int64_t runUs = 0;
};

// The keys a battle's events and the session's battle notices can carry, for a host to check its presentation data
// against: core's Message keys and status message keys with this data, the session's notices around battles, and core's
// cancel reasons (`cancel.<reason>` in the text tables).
struct BattleKeys {
	std::vector<std::string> messages;
	std::vector<std::string> notices;
	std::vector<std::string> cancelReasons;
};

// One of the party's vehicles in the party's area, from Session::vehiclesHere() (FLD-060).
struct VehicleView {
	std::string id;
	Cell cell;
	Facing facing = Facing::South;
	// The party rides it, so it stands on the leader's cell.
	bool aboard = false;
};

// What interact acts on (FLD-030). Talk is an interact hook that is no actor's (a sign); Actor an actor's talk point, or
// the actor where it stands under game.json's `interact: "nearby"`.
enum class InteractKind { Vehicle, Port, Helm, Talk, Actor, Chest, Shop, Rest };

const char* toString(InteractKind kind);

// What interact would act on now, from Session::interactTarget(), for a host to draw a prompt over.
struct InteractTarget {
	InteractKind kind = InteractKind::Talk;
	// The vehicle, port, helm, hook, chest, shop or rest point id; for Actor the actor's, which is its talk point's.
	std::string id;
	// The cell acted on: the one faced, another neighbor, the one beyond a counter, or where the actor stands.
	Cell cell;
	// Where the leader faces to use it; interact turns the leader there first.
	Facing facing = Facing::South;
	// False when interact would refuse: a hook whose flags do not hold or whose actor is removed, a closed shop or port.
	bool ready = true;
	// Used across a counter.
	bool across = false;
	// A shop's or rest point's `keeper`, the actor who keeps it; empty for none and for every other kind.
	std::string keeper = {};
};

// How the leader and the belt under it move now, for a host that slides the leader between cells (Session::fieldMotion).
struct FieldMotion {
	// Milliseconds of held input spent toward the next cell, and the time a cell takes walking and with the run input.
	int stepProgressMs = 0;
	int stepMs = 0;
	int runStepMs = 0;
	// The belt carrying the party: its time per cell and the milliseconds spent toward the next; both 0 off a belt.
	int conveyorStepMs = 0;
	int conveyorProgressMs = 0;
	// The same five in microseconds, exact under game.json's `step_timing` `exact` (R108), where step times may have a
	// fraction and the progress carries the time past the last step.
	std::int64_t stepProgressUs = 0;
	std::int64_t stepUs = 0;
	std::int64_t runStepUs = 0;
	std::int64_t conveyorStepUs = 0;
	std::int64_t conveyorProgressUs = 0;
	// The directions a step would move the leader now (canStep): 1 north, 2 east, 4 south, 8 west.
	int stepMask = 0;
	// Update time until the next field tick, when a step can start or land (game.json `tick_ms` less the time already
	// passed toward it), and until the tick on which the step under way (or one started now) lands with the input held,
	// walking and with the run input: a host glides the leader over that time so it arrives as the session moves it.
	std::int64_t nextTickUs = 0;
	std::int64_t stepLandsInUs = 0;
	std::int64_t runStepLandsInUs = 0;
};

// Ways to travel at once (FLD-051): from a port to another open port, back to a town visited before in the region the
// party is in, or out of the dungeon the party is in to where it stood before going in.
enum class TravelKind { Port, Town, DungeonExit };

const char* toString(TravelKind kind);
bool parseTravelKind(const std::string& text, TravelKind& out);

struct TravelDestination {
	// A port id for Port, an area id for Town and DungeonExit.
	std::string id;
	// Where the party lands: for a town with `return`, not the town.
	std::string area;
	// Where the party lands.
	Cell cell;
	std::string region;
};

// What Session::devList lists: the ids a developer command takes. Markers are "area:marker".
enum class DevList { Areas, Markers, Flags, Items, Equipment, Abilities, Enemies, Encounters, Events, Vehicles, Characters };

const char* toString(DevList list);
bool parseDevList(const std::string& text, DevList& out);

// Where the running event is, from Session::eventPosition.
struct EventPosition {
	// The innermost running event.
	std::string event;
	// The script file and the source line of the instruction it is at.
	std::string file;
	int line = 0;
};

enum class WalkStatus { Walking, Arrived, LeftArea, Stopped, NoPath, TooLong };

// A walk to a cell along the shortest path, from Session::beginWalk.
struct PathWalk {
	Cell goal;
	std::string area;
	int steps = 0;
};

// Work the session has done since it was made, for performance checks (`game_cli --profile`, tests/test_perf.cpp): counts,
// and wall time in microseconds, each timer including the work of those it calls.
struct SessionStats {
	std::uint64_t ticks = 0;
	// Path searches of walkStep and the cells they reached.
	std::uint64_t walkPlans = 0;
	std::uint64_t walkCells = 0;
	// Actor views built (actorsHere and the wanderers' updates of single actors).
	std::uint64_t actorViews = 0;
	// Wandering and patrolling actors considered in field ticks.
	std::uint64_t wanderTurns = 0;
	std::uint64_t settles = 0;
	std::uint64_t autosaves = 0;
	std::uint64_t saveBytes = 0;
	std::uint64_t loads = 0;
	std::uint64_t tickUs = 0;
	std::uint64_t wanderUs = 0;
	std::uint64_t patrolUs = 0;
	std::uint64_t enemyUs = 0;
	std::uint64_t backgroundUs = 0;
	std::uint64_t planUs = 0;
	std::uint64_t settleUs = 0;
	std::uint64_t autosaveUs = 0;
	std::uint64_t loadUs = 0;
};

// The game: world state, modes, the event host, battles, encounters, saves and pacing. An engine calls
// update() every frame and the mode's operations on input; everything it shows comes from drainOutput(),
// the event line and choice accessors, battle() and the core events resolveRound() returns.
class Session {
public:
	Session(const GameData& data, SessionOptions options);
	~Session();
	Session(const Session&) = delete;
	Session& operator=(const Session&) = delete;

	const GameData& data() const { return data_; }
	Mode mode() const { return modes_.mode(); }
	const ModeMachine& modes() const { return modes_; }
	const World& world() const { return world_; }
	World& world() { return world_; }
	// The party's area as it stands now: its cell changes whose flags are set and the barriers broken in it applied to
	// its rows (F3, F8, F10). areaAt gives any area the same way.
	const Area* area() const;
	const Area* areaAt(const std::string& id) const;
	EncounterMode encounterMode() const;
	PacingRecorder& pacing() { return pacing_; }
	const PacingRecorder& pacing() const { return pacing_; }
	std::vector<Output> drainOutput();
	int autosaveCount() const { return autosaveCount_; }
	const SaveStore* store() const { return store_ ? &*store_ : nullptr; }
	const Profile& profile() const { return profile_; }
	// Applies at once and writes the profile file (SAV-005). Refuses a text language the game has no tables for.
	rules::Message setProfile(const Profile& profile);
	// The tables of the profile's text language.
	const LanguageTables& language() const;

	// Text from the game's tables: a key with its parameters, ids shown as display names.
	std::string render(const rules::Message& message) const;
	// A script text: string IDs looked up, name tokens rendered for the current profile.
	std::string scriptText(const events::Text& text) const;
	std::string stringText(const std::string& id) const;
	// A character's chosen name, its own name, an actor's speaker name or an area's name.
	std::string displayName(const std::string& id) const;
	// An element's name in the profile's language (`name.element.<id>`), else its id; "" for none.
	std::string elementName(const rules::Element& element) const;
	// An item's, piece's, ability's, status's, enemy's or difficulty's description in the profile's language, name
	// tokens rendered; empty when that language has none.
	std::string description(DescriptionKind kind, const std::string& id) const;

	// Every operation below that returns a Message returns an empty one when it ran, otherwise the text key and
	// parameters of why it did not; render() shows it.

	// Title (FLOW-020).
	rules::Message newGame();
	rules::Message continueGame();
	rules::Message load(const std::string& slot);
	// Whether load() may run from the current state (the spec allows it from the title only), for a host that shows or
	// hides Load; saves() says whether there is anything to load.
	bool canLoad() const;
	// The text key area `id` shows as now: its `name` variant for the area the party came from and the flags, else
	// `area.<id>`. displayName() and the arrival notice use it.
	std::string areaNameKey(const std::string& id) const;
	// What battle events and notices can carry with this data (BattleKeys).
	BattleKeys battleKeys() const;
	// Deletes a manual or autosave slot's file, damaged ones included, in any mode; the game being played is not touched.
	// Refused as load() refuses a slot it cannot read: no saving, an unknown slot name or an empty slot. Erasing the
	// autosave a retry would load ends that retry.
	rules::Message eraseSave(const std::string& slot);
	// After a game over, reloads the autosave taken before the lost event battle (FLOW-042).
	rules::Message retryBattle();
	bool gameOver() const { return gameOver_; }
	bool canRetry() const { return gameOver_ && !retrySlot_.empty(); }
	// The pause overlay's Quit to Title (FLOW-022). Not a transition: it resets the machine.
	void quitToTitle();

	// Advances the active mode by `ms`. In Field, `input` is the held direction.
	void update(int ms, const FieldInput& input = FieldInput());
	// Holds `direction` until the leader enters the next cell or something stops the field.
	rules::Message walk(Facing direction);
	// The same with the run input as FieldInput::running holds it.
	rules::Message walk(Facing direction, bool running);
	// Whether a step in `direction` would move the leader now: the field under player control, no belt carrying the party,
	// and a cell it can enter with no visible enemy on it (bumping one starts its battle instead).
	bool canStep(Facing direction) const;
	// The leader's time per cell now: the area's step time, a vehicle's speed aboard, and on foot with `running` (the run
	// input as FieldInput::running holds it) the run speed.
	int leaderStepMs(bool running = false) const;
	// The same in microseconds: the area's step_ms with its fraction over the vehicle's or the run's speed.
	std::int64_t leaderStepUs(bool running = false) const;
	// Whether the last area entry announced itself (game.arrive and a host's banner, R107): game.json's arrive_notice and
	// the area's own.
	bool arrivalAnnounced() const { return arrivalAnnounced_; }
	// The sound cue of the exit the party left by at the last area entry (an area exit's `sound`); empty when it came
	// another way or the exit has none.
	const std::string& exitSound() const { return exitSound_; }
	// The story pictures up now, bottom first (scripts' `picture show`, `hide` and `clear`). They stay through lines,
	// the end of the event and area changes, and the save keeps them.
	const std::vector<std::string>& pictures() const { return world_.pictures; }
	// The scene camera a script's `camera to` or `camera reset` set: held on a cell, or going back to the leader while
	// `ms` is above 0; neither when it follows the leader as usual.
	const World::SceneCamera& sceneCamera() const { return world_.sceneCamera; }
	// The moving platforms of the party's area, in the area's order.
	std::vector<PlatformView> platformsHere() const;
	// The platform the party rides, empty for none. While it moves, the leader's cell follows it and held input waits.
	const std::string& ridingPlatform() const { return world_.ridingPlatform; }
	// The lineup after the leader, then the follower actor, each on a cell of the leader's trail: the leader's earlier cells
	// in the area, newest first, the last one taken by any beyond it; all on the leader's cell after an area entry under
	// game.json's `followers_arrive` `stacked` (the default), and on the floor straight behind the leader under `behind`,
	// aboard a vehicle and on a moving platform.
	std::vector<FollowerView> followers() const;
	// game.json bag_slots (0: no limit), the slots items fill now, and how many more of `id` the bag takes (equipment and
	// any item without a limit: up to item_cap).
	int bagSlots() const { return data_.config.bagSlots; }
	int bagSlotsUsed() const;
	int bagRoom(const std::string& id) const;
	// Whether autosaves are written (game.json autosave and autosave_slots).
	bool autosaveOn() const { return data_.config.autosave && data_.config.autosaveSlots > 0; }
	// What the running foreground event shows next (events::Interpreter::next_output), without advancing it; `none` when
	// none runs.
	events::Interpreter::NextOutput nextEventOutput() const;
	// Turns the leader in place.
	rules::Message face(Facing direction);
	// A walk to `goal` along the shortest path over walkable cells, crossing no exit cell a step would take (game.json's
	// `exit_fires`, an exit's `fires`) other than the goal. Visible
	// enemies are not avoided, so bumping into one starts its battle. Call walkStep until it returns something
	// other than Walking, and run events and battles between the calls: an event or battle on the way stops the
	// walk only if the field does not come back. Walking ends at the goal (Arrived), when the party leaves the
	// area (LeftArea), when the field does not come back (Stopped), without a path (NoPath) or after 4000 steps
	// (TooLong).
	PathWalk beginWalk(Cell goal) const;
	WalkStatus walkStep(PathWalk& walk);
	// The actors in the party's area, in game.json order, hidden ones included.
	std::vector<ActorView> actorsHere() const;
	// The track the area plays now: the one a script set (`music area`) until the party leaves, else aboard a vehicle
	// its `music.vehicles` `field` track, else the first of game.json's `music.areas` entries for the area whose flag is set, else the area's own. Flags are read on each call,
	// so a listed flag set in the area changes it at once.
	std::string areaMusic() const;
	// The battle's track, live or ended: the encounter's from game.json, else for a random battle aboard the vehicle's;
	// empty when the data names none.
	std::string battleMusic() const;
	// Interacts with whatever stands in the cell the leader faces (FLD-030): a vehicle is boarded, a port opens its
	// destinations. Refused while the party rides a vehicle. game.json's interact rules can pick another cell
	// (interactTarget).
	rules::Message interact();
	// What interact would act on now; empty when nothing answers, outside the field, without control or aboard.
	std::optional<InteractTarget> interactTarget() const;
	// What stands on `cell` and whether the leader can use it from where it stands: the target interact would act on once
	// the leader faced that way (a neighbor, the cell beyond a counter, or an actor standing there), with the facing to
	// turn to; empty when nothing there answers from here, and when interactTarget would be.
	std::optional<InteractTarget> interactAt(Cell cell) const;
	// The leader's step progress and times, the belt's, and where a step can go (all 0 outside the field).
	FieldMotion fieldMotion() const;
	// The background events running now, in start order, each with the script line it is at (events/FORMAT.md,
	// "Background events"). They run only in field ticks, after the leader's step and the wanderers' and visible enemies'.
	std::vector<EventPosition> backgroundEvents() const;
	// The reserved actor name (`enemy.<zone>.<n>`) a script addresses the visible enemy with this id by; empty for none.
	std::string enemyActor(int enemyId) const;
	// The party's vehicles in its area, in id order, the one it rides included.
	std::vector<VehicleView> vehiclesHere() const;
	// The field rules of the vehicle the party rides, or null on foot.
	const FieldVehicle* riding() const;
	// Boards the vehicle in the cell the leader faces (FLD-061); interact() does the same.
	rules::Message board();
	// Steps off the vehicle onto the cell the leader faces, or else the first of the other neighbors (north, east, south,
	// west) the party could walk on; the vehicle stays where it is. Refused when none is.
	rules::Message leaveVehicle();
	// The port whose destinations the Port menu shows, or null.
	const Port* port() const;
	// Where a travel of `kind` can go now: the other open ports while a port's menu is up, the towns of the region the
	// party has visited that have an arrival marker (the area it is in left out), or the one way out of the dungeon it is
	// in: the dungeon area's `dungeon_exit` when it has one, else where the party stood before going in. Empty when there
	// is none.
	std::vector<TravelDestination> travelDestinations(TravelKind kind) const;
	// Travels to `destination`. Port: from a port's menu. Town and DungeonExit: from the field menu with `thing`, an item,
	// or a technique that `user` (a lineup member) knows, whose travel effect matches; it is spent as core's field use
	// spends it. Town is refused inside a dungeon, DungeonExit outside one; DungeonExit takes an empty destination. A
	// vehicle the party rides stays behind (FLD-063).
	rules::Message travel(TravelKind kind, const std::string& destination, const std::string& user = "", const std::string& thing = "");
	// Party talk (FLD-070): the lines of the best matching set for this area and the flags, as Line output. The same set
	// also comes up by itself, as Ambient output, the first time control returns to the field in an area under it while
	// the profile's ambient talk is on (FLD-073); World::ambientHeard keeps which ones have.
	rules::Message partyTalk();
	// The lines partyTalk would show now, with their speakers, from any mode with a game; nothing is shown, heard or
	// changed. Empty when nobody talks (and at the title).
	std::vector<TalkLineView> partyTalkLines() const;

	// Event
	bool linePending() const;
	std::optional<LineView> line() const;
	// The pending line as "Speaker: text", or the text alone for narration.
	std::string lineText() const;
	void confirmLine();
	const events::ChoiceRequest* choice() const;
	std::optional<ChoiceView> choiceView() const;
	rules::Message choose(int index);
	bool canSkip() const;
	bool skip();
	// The last 500 pages since the game was loaded, rendered for the current profile, skipped ones marked.
	std::vector<std::string> textLog() const;

	// Battle
	const rules::Battle* battle() const { return battle_ ? &*battle_ : nullptr; }
	rules::Message setCommand(int actor, const rules::Command& command);
	// MacroFallback::Automatic gives a member whose step cannot be paid for, has no target or would be refused core's
	// automatic choice for the round (rules::autoCommand) instead; the default keeps core's handling.
	rules::Message applyMacro(const std::string& name, std::vector<rules::MacroStepResult>* results = nullptr,
		rules::MacroFallback fallback = rules::MacroFallback::BasicAttack);
	// Auto-battle (BAT-103, BAT-104) with the save's auto macro. fillFromAuto fills the round and returns true
	// while it runs; once a stop rule fires it returns false and reports why.
	rules::Message startAuto();
	// With a trouble mode in place of the macro's on_trouble or, failing that, rules.json auto_macro_on_trouble (default
	// Stop, the stop rules). Continue and Recover never stop by themselves (rules::Battle::startAuto).
	rules::Message startAuto(rules::MacroTrouble trouble);
	// And with the automatic choice's policy for the battle, over the macro's and rules.json's (Default leaves it to them).
	rules::Message startAuto(rules::MacroTrouble trouble, rules::MacroPolicy policy);
	void cancelAuto();
	bool autoActive() const;
	bool fillFromAuto();
	// Core's simple per-member policy for every party member who acts this round: how the terminal plays a
	// battle on its own.
	void fillFromPolicy();
	// Fills the round the way auto play does: on the first round, starts auto-battle with the save's auto macro
	// when one is set; then fillFromAuto, or fillFromPolicy once auto-battle is off. Returns why auto-battle did
	// not start, if it did not. With an auto play macro it applies that macro with MacroFallback::Automatic instead.
	rules::Message fillAutoRound();
	// A macro auto play (fillAutoRound, playBattle) fills every round with, in place of the save's auto macro and
	// core's policy: each member whose step cannot be paid for or has no target takes core's automatic choice, and
	// the rest of the macro stands. Empty, the default, plays as before. Kept for the session, not saved; refuses a
	// name no macro has. A macro deleted or renamed later is refused at the next round, which plays as before.
	rules::Message setAutoPlayMacro(const std::string& name);
	// `trouble`, unless Default, wins over the macro's on_trouble and the rules' default for the path: for the named macro
	// (named_macro_on_trouble, default Continue) or, with an empty name, for the save's auto macro (auto_macro_on_trouble,
	// default Stop). Stop on a named macro plays it under the stop rules, then core's policy.
	rules::Message setAutoPlayMacro(const std::string& name, rules::MacroTrouble trouble);
	// `policy`, unless Default, is the automatic choice's policy in those battles, over the macro's and rules.json's
	// auto_policy (rules::Battle::setAutoPolicy).
	rules::Message setAutoPlayMacro(const std::string& name, rules::MacroTrouble trouble, rules::MacroPolicy policy);
	rules::MacroTrouble autoPlayTrouble() const { return autoPlayTrouble_; }
	rules::MacroPolicy autoPlayPolicy() const { return autoPlayPolicy_; }
	// The trouble of the round fillAutoRound filled last, with the mode played and who took core's choice
	// (rules::MacroTroubleReport); empty when no macro filled it.
	const rules::MacroTroubleReport& lastMacroTrouble() const { return lastMacroTrouble_; }
	// The trouble a round of the macro would meet now, without setting anything.
	rules::Message macroTrouble(const std::string& name, rules::MacroTroubleReport& out) const;
	// Each step's state this round (rules::Battle::checkMacro), for a host marking steps before the player confirms.
	rules::Message checkMacro(const std::string& name, std::vector<rules::MacroStepCheck>& out) const;
	const std::string& autoPlayMacro() const { return autoPlayMacro_; }
	// Plays the battle to its end with fillAutoRound and resolveRound, at most `maxRounds` rounds.
	AutoBattle playBattle(int maxRounds = 60);
	// Resolves one round. The battle ends inside this call when an outcome is reached; endedBattle() then has it.
	std::vector<rules::Event> resolveRound();
	BattleSource battleSource() const { return battleSource_; }
	const std::string& battleEncounter() const { return battleEncounter_; }
	const EndedBattle* endedBattle() const { return ended_ ? &*ended_ : nullptr; }
	void dismissBattle();

	// Menus and shops. A shop, save prompt or name entry an event opened closes back to the event.
	rules::Message openMenu();
	rules::Message closeMenu();
	MenuKind menuKind() const { return mode() == Mode::Menu ? menuKind_ : MenuKind::None; }
	const Shop* shop() const;
	// A shop is open while its flags hold, and sells the stock entries whose flags hold (F7, F10).
	bool shopOpen(const Shop& shop) const;
	std::vector<std::string> stock(const Shop& shop) const;
	const RestPoint* inn() const;
	// What resting at the point costs now: its cost, times the lineup's size when it is per_member (R113).
	int restCost(const RestPoint& point) const;
	rules::Message buy(const std::string& id, int count);
	rules::Message sell(const std::string& id, int count);
	// From the field menu: throws `count` of an item or an unworn equipment piece away (notice game.discarded). Refused for
	// an item whose data says `discardable: false`, for fewer than one and for more than the bag holds.
	rules::Message discard(const std::string& id, int count);
	rules::Message rest();
	// Field use through core (BAT-044). `member` is a character id of the active party, or empty for items and
	// techniques that work on everyone. An item that would change nothing is not used up.
	rules::Message useItem(const std::string& item, const std::string& member);
	rules::Message useAbility(const std::string& user, const std::string& ability, const std::string& member);
	// The lineup members a one-ally or one-fallen-ally ability or item (`kind` Ability or Item) used by `user` would be
	// refused on because nothing in it can reach them, so a host can grey them before the player picks: in a live battle
	// those setCommand refuses with command.no_effect_on_target (rules::Battle::alliesOutOfReach), elsewhere those field use
	// refuses with field.no_target (rules::fieldTargetsOutOfReach; `user` matters for abilities only). Empty for other
	// targetings, kinds and unknown ids.
	std::vector<std::string> alliesOutOfReach(rules::CommandKind kind, const std::string& id, const std::string& user) const;
	rules::Message equip(const std::string& member, const std::string& equipment);
	// At a position the piece fits: a hand for weapons and shields (P10).
	rules::Message equip(const std::string& member, const std::string& equipment, rules::EquipSlot position);
	rules::Message unequip(const std::string& member, rules::EquipSlot slot);
	// From the field menu, or from a save prompt an event opened.
	rules::Message saveManual(const std::string& slot);
	// The credits playing, while menuKind() is Credits. update() moves them on; closeMenu() is how the front end says they
	// finished (at any point: the session never ends them itself), and the event that started them goes on. Saving is
	// refused while they play, as anywhere outside the field menu and a save prompt.
	std::optional<CreditsView> credits() const;
	// Name entry: the member being named, and the name to confirm, which closes the entry.
	const std::string& nameEntryMember() const { return nameMember_; }
	rules::Message enterName(const std::string& name);
	// Difficulty and reward multiplier apply from the next battle (BAT-126). They belong to a save (SAV-005), so they
	// change only with a game in progress and outside battle.
	rules::Message setSettings(const Settings& settings);
	bool canChangeSettings() const { return mode() != Mode::Title && mode() != Mode::Battle; }
	// Every difficulty preset in id order.
	std::vector<DifficultyView> difficulties() const;
	// One of the save's macros, or empty for none.
	rules::Message setAutoMacro(const std::string& macro);

	// Party order, from the field menu: swaps two positions of the lineup, the leader's included. The party's
	// members change only through events (SAV-034).
	rules::Message swapLineup(int a, int b);
	// The macro editor (UI-005). Reading works any time; changes are made from the field menu. Positions and step
	// indexes count from 0. A macro holds at most one step per party member (BAT-050) and there are at most
	// kMaxMacros (BAT-055). Deleting the auto macro turns auto-battle's macro off with a notice; renaming it keeps it.
	std::vector<MacroView> macroViews() const;
	rules::Message createMacro(const std::string& name);
	rules::Message renameMacro(const std::string& name, const std::string& newName);
	rules::Message deleteMacro(const std::string& name);
	rules::Message moveMacro(const std::string& name, int position);
	// Replaces the step at `index`, or appends it when `index` is the step count.
	rules::Message setMacroStep(const std::string& name, int index, const rules::MacroStep& step);
	rules::Message removeMacroStep(const std::string& name, int index);
	rules::Message moveMacroStep(const std::string& name, int from, int to);
	// The macro's on_trouble, recover_who and unnamed; Default leaves each to the caller and rules.json. Saved with it.
	rules::Message setMacroOptions(const std::string& name, rules::MacroTrouble onTrouble, rules::MacroRecoverWho recoverWho, rules::MacroUnnamed unnamed);
	// The macro's automatic choice policy (rules::MacroPolicy; Default: rules.json's auto_policy), saved with it.
	rules::Message setMacroPolicy(const std::string& name, rules::MacroPolicy policy);
	// A new macro named `name` from a discovered combination (BAT-065, UI-011): one ability step per component, in the
	// recipe's order, which is a valid order for an ordered recipe too, each with its default target. Each component goes
	// to a lineup member who knows the ability now (the character a component is tied to), trying the members the log
	// recorded first; refused when no such assignment exists. The name rules are createMacro's.
	rules::Message macroFromCombo(const std::string& recipe, const std::string& name);
	// The combo log in discovery order; data().db.combos has every recipe, for the locked entries.
	std::vector<ComboView> comboLog() const;
	// Empty for a character not in the roster.
	std::optional<MemberDetail> memberDetail(const std::string& member) const;
	// Items, then unworn pieces, in id order for ItemSort::Id; otherwise sorted as one list.
	std::vector<BagEntry> bag(ItemSort order) const;
	// Empty for a character not in the roster or a piece that is not equipment. The piece need not be in the bag.
	std::optional<EquipPreview> equipPreview(const std::string& member, const std::string& piece) const;
	std::optional<EquipPreview> equipPreview(const std::string& member, const std::string& piece, rules::EquipSlot position) const;

	// Developer tools (docs/dev-tools.md). Every command that changes the game marks it (World::devMarked, kept in its
	// saves): from then on it writes no clear-data record and adds nothing to the pacing numbers. devList only reads.
	std::vector<std::string> devList(DevList list) const;
	// To a marker, or to a walkable cell, of any area; from the field.
	rules::Message devWarp(const std::string& area, const std::string& marker);
	rules::Message devWarpCell(const std::string& area, Cell cell);
	// A declared flag, set or cleared; flag hooks of the area run as when a script sets it.
	rules::Message devSetFlag(const std::string& flag, bool value);
	// Adds `count` of an item or equipment piece (a negative count takes them away), capped like any gain.
	rules::Message devGive(const std::string& id, int count);
	// Adds `amount` to the money (negative takes it away), kept between 0 and the cap.
	rules::Message devMoney(int amount);
	// Restores the roster and the vehicles' skill uses as a rest does, free, with no notice of resting, no autosave and
	// nothing else changed.
	rules::Message devRest();
	// Sets a roster member's level with no experience into it, and refills it as a rest does, reviving it.
	rules::Message devSetLevel(const std::string& member, int level);
	// Starts a battle with the encounter from the field, as a random battle (a vehicle battle while riding).
	rules::Message devBattle(const std::string& encounter);
	// Ends the live battle: every enemy (victory) or every party combatant (defeat) falls, then a round resolves, so the
	// battle ends the way that round's outcome leads. `events` gets the round's events.
	rules::Message devEndBattle(bool victory, std::vector<rules::Event>* events = nullptr);
	// In a battle, for testing: the recipe's abilities as this round's commands of lineup members who know them now (each
	// component's tied character when it has one), acting first and in the recipe's order (rules::Battle::setLeadOrder), so
	// the combination fires when the round resolves unless a part cannot be paid for or its member cannot act. Other
	// members' commands stand.
	rules::Message devCombo(const std::string& recipe);
	// Gives the party a vehicle with field rules, or moves one it has, to a marker of a field area; from the field.
	rules::Message devVehicle(const std::string& vehicle, const std::string& area, const std::string& marker);
	// Turns random battles, battles aboard and visible enemies' battles off or on for this session (not saved).
	rules::Message devEncounters(bool on);
	bool encountersOn() const { return encountersOn_; }
	// Runs an event from the field, as a hook would.
	rules::Message devRunEvent(const std::string& event);
	// Marks the game without changing anything else, for a host's own tools (a data reload).
	void devMark();
	bool devMarked() const { return world_.devMarked; }
	// The running event and the source line its interpreter is at; empty when no event runs.
	std::optional<EventPosition> eventPosition() const;
	// The game as a save file's text, with the running event's interpreter state, and during a battle the world as it
	// was when the battle was set up (an event battle is set up again when it loads; a field battle is gone). Refused at
	// the title (game.refuse.no_game).
	rules::Message saveText(std::string& out) const;
	// Loads such a text, or any save file's text, from the title, as load() does.
	rules::Message loadSaveText(const std::string& text);

	// Presentation actions of the session's own queue since the last call, oldest first. With Instant
	// presentation only the last 256 are kept.
	std::vector<PresentedAction> pollActions();
	void finishAction(std::uint64_t handle);

	QuestLog questLog() const;
	std::vector<EncyclopediaView> encyclopedia() const;

	// The active party's average level in hundredths.
	int averageLevelX100() const;
	SaveFile makeSave(bool autosave) const;
	const SessionStats& stats() const { return stats_; }

private:
	class GameHost;
	class ActionQueue;
	class BackgroundHost;
	friend class GameHost;
	friend class BackgroundHost;

	struct BackgroundRun {
		std::string event;
		std::unique_ptr<BackgroundHost> host;
		std::unique_ptr<events::Interpreter> interpreter;
	};

	enum class HandleKind { Line, Choice, Battle, Action, Menu };
	struct HandleState {
		HandleKind kind = HandleKind::Action;
		bool done = false;
		int result = 0;
		events::Line line;
		events::ChoiceRequest choice;
	};

	void emit(OutputKind kind, const std::string& key, std::vector<std::string> params = {});
	void warn(const std::string& text);
	std::string text(const std::string& key, std::vector<std::string> params = {}) const;
	void addTime(int ms);
	void tickField(const FieldInput& input);
	void enterArea(const std::string& areaId, const std::string& marker, std::optional<Cell> standing = std::nullopt);
	// `standing` is where the party stood before, when it is not World::position (an exit cell it stepped onto).
	void enterAreaAt(const std::string& areaId, Cell cell, std::optional<Cell> standing = std::nullopt);
	// Whether the leader can move onto `cell`: on foot floor that nothing stands on, including the party's vehicles;
	// aboard, terrain the vehicle crosses that nothing stands on.
	bool canEnter(const Area& area, Cell cell) const;
	// The same with the cells solid actors stand on given (actorCells()), for many cells at a time.
	bool canEnter(const Area& area, Cell cell, const std::set<Cell>& standing) const;
	// The id of a vehicle of the party standing on `cell` of the party's area, the ridden one left out.
	std::string vehicleAt(Cell cell) const;
	// Leaves the ridden vehicle where it stands without stepping off, when a warp or a travel takes the party away.
	void leaveVehicleBehind();
	// Puts a vehicle down, or back at its last boarding point when the party could not reach it there (FLD-063).
	void parkVehicle(const std::string& id, const std::string& area, Cell cell);
	// Off the ridden vehicle onto its cell, or the nearest cell the party can walk on.
	void dismount();
	void healMachinesOnStep();
	void settleMachineBank();
	void shareWithMembersOutside(const std::vector<rules::PartyMember>& fought, const rules::Rewards& rewards);
	void arrive(Cell cell);
	void settleField();
	const TalkSet* talkSet() const;
	// Lines of `set` whose speakers are in the active party, as output of `kind`; false when none is.
	bool speak(const TalkSet& set, OutputKind kind);
	bool startEvent(const std::string& event);
	bool backgroundEvent(const std::string& event) const;
	// Starts a background event beside the field; ignored while one of that name runs, refused past background_limit.
	void spawnBackground(const std::string& event);
	void stepBackground(int ms);
	// Cancels every background event: on entering an area, a load, a new game and the title.
	void stopBackground();
	bool restoreBackground(const std::string& event, const std::string& text);
	void setActorLook(const std::string& actor, const std::string& enemy, const std::string& look);
	// The touch hook of the actor standing on `cell` of the party's area whose flags hold, or null.
	// `deliberate`: a step of the player's own or interact, which `touch_from: keys` hooks also answer.
	const Hook* touchHookAt(Cell cell, bool deliberate = true) const;
	const Hook* touchHook(const std::string& actor, bool deliberate = true) const;
	// Turns an actor of the party's area toward the leader (a hook's `turns`).
	void turnTowardLeader(const std::string& actor);
	// actorsHere() kept up to date through a field tick: the view of the actor last touched is rebuilt, and all of them
	// after an event, which may change anything. `slot` is each view's place in `views` by the actor's place in game.json.
	struct TickViews {
		std::vector<ActorView> views;
		std::vector<std::size_t> slot;
		bool stale = true;
		const ActorDef* touched = nullptr;
	};
	void refreshViews(TickViews& views, const Area& here) const;
	const ActorView* viewOf(const TickViews& views, const ActorDef& def) const;
	// Steps every wandering actor of the party's area by one tick, after the leader's step.
	void wanderActors(int ms, TickViews& views);
	// Then the patrols, the paths and the platforms.
	void patrolActors(int ms, TickViews& views);
	void pathActors();
	void movePlatforms(int ms);
	bool fieldStatusStep();
	void placeFollowers();
	// The area the party was in before, kept while it walks (World::cameFrom holds it for areas with `name` variants).
	std::string previousArea_;
	// Set by a return to a town that lands outside it: the next entry counts as coming from that town.
	std::string arrivingFrom_;
	// The track a script's `music play` started (empty after `music stop`) until the party enters an area; unset plays the
	// area's track. Not saved: after a load the area's track plays.
	std::optional<std::string> scriptTrack_;
	// The `leader`, `music`, `area` and `tile` condition tests.
	bool stateTest(const std::string& test, const std::string& value) const;
	// Puts the area's platforms where entering it does, keeping those `keep` lists.
	void resetPlatforms(bool keep);
	// Whether an actor walking by itself may step onto `next` (terrain, points, exits, other actors, enemies, vehicles).
	bool actorMayEnter(const Area& area, const std::string& terrain, Cell next, const std::vector<ActorView>& standing) const;
	// Finishes the patrol entries with nothing left to walk (their facing and pause) until one has a cell to walk to or a
	// pause starts; returns that cell, if any.
	std::optional<Cell> settlePatrol(const ActorDef& def, ActorPlace& place) const;
	// The cells a path actor's footprint overlaps with its anchor and tick.
	std::vector<Cell> pathCells(const ActorDef& def, Cell anchor, int tick) const;
	// A platform of the party's area at an end on `cell`, or null.
	const Platform* dockedPlatformAt(Cell cell) const;
	bool platformDocked(const std::string& id) const;
	VisibleEnemy* visibleEnemyByActor(const std::string& actor);
	// What answers on `cell` of `area` for interact, its facing and cell left to the caller; `acrossable` is set when it
	// may be used across a counter.
	std::optional<InteractTarget> pointAt(const Area& area, Cell cell, bool& acrossable) const;
	// What a marked counter's far side holds (R109): a point there, or a shop's or rest point's keeper standing there.
	std::optional<InteractTarget> beyondCounter(const Area& area, Cell cell) const;
	void pumpEvent(double seconds);
	void endEvent();
	void dropEvent();
	void openChest(const Chest& chest);
	// A `sound` action outside any event (game.json `presentation.sounds`), which nothing waits on; nothing for an empty id.
	void playFieldSound(const std::string& cue);
	const VehicleSounds* vehicleSounds(const std::string& vehicle) const;
	void boardShip(const Port& port);
	rules::Message callVehicle(const FieldVehicle& vehicle);
	// Where an actor no script touched stands in `area`: its talk point (or the hook's `actor_at`), or its game.json home.
	struct ActorHome {
		Cell cell;
		Facing facing = Facing::South;
		bool visible = true;
	};
	std::optional<ActorHome> actorHome(const Area* area, const std::string& actor) const;
	// The view actorsHere() gives of one actor in `here`; false when it is not there.
	bool actorView(const ActorDef& def, const Area& here, ActorView& view) const;
	const ActorDef* actorDef(const std::string& id) const;
	// What an area's data holds, by area id, built at first use: the objects on each cell, the talk points, the actors that
	// may stand there unmoved and what area()'s view depends on. The data does not change under a session.
	struct AreaIndex {
		int width = 0;
		int height = 0;
		std::vector<std::uint8_t> objects;
		std::map<std::string, std::size_t> talkPoints;
		std::vector<std::size_t> homeActors;
		std::vector<std::size_t> touchActors;
		// Talk points that answer on their actor's cell (`talk_at: actor`), by hook index.
		std::vector<std::size_t> actorTalk;
		bool gatedExits = false;
		bool gatedHooks = false;
		std::vector<std::size_t> whileActiveHooks;
	};
	const AreaIndex& areaIndex(const std::string& id) const;
	// Whether `cell` may hold a hook, exit, shop, rest point, chest, port, helm or gangway cell of the area; true when unsure.
	bool objectsMayStand(const Area& area, Cell cell) const;
	// Area::exitAt and exitFiresFrom by the area index.
	const Exit* exitOn(const Area& area, Cell cell) const;
	bool exitTakes(const Area& area, const Exit& exit, Cell from) const;
	// `animationMs` is how long an `anim` plays by the data, 0 for an instant one.
	void applyActorAction(const events::Action& action, int animationMs);
	ActorPlace& touchActor(const std::string& actor);
	std::optional<Cell> actorCell(const std::string& actor) const;
	// The cells from `from` through each stop in turn: along x then y, or with `pathfind` around what stands in the way.
	std::vector<Cell> actorRoute(const Area& area, const std::string& actor, Cell from, const std::vector<Cell>& stops, bool pathfind) const;
	// Cells solid actors stand on in the party's area, `except` left out.
	std::set<Cell> actorCells(const std::string& except = "") const;
	// The same from actorsHere() as it is now.
	std::set<Cell> actorCells(const std::vector<ActorView>& views, const std::string& except) const;
	void removeActor(const std::string& actor, bool untilReload);
	void advanceActors(int ms);
	void giveThing(const std::string& id, int count);
	void reportFieldUse(const std::vector<rules::Event>& events, const std::vector<rules::PartyMember>& party);
	rules::Message payRest(const RestPoint* point);
	void openScriptMenu(Mode mode, MenuKind kind, events::Handle handle);
	void finishScriptMenu();
	void addLog(const events::Line& line, bool skipped);
	void refreshJournal(bool announce);
	void writeClearRecord();
	std::string startBattle(const std::string& encounter, rules::Opening opening, bool escapeAllowed, bool defeatHasBranch, const rules::ScriptedEnd& end,
		const std::string& vehicle = "");
	void startFieldBattle(const EncounterStart& start, const std::string& vehicle = "");
	void finishBattle();
	void enterGameOver();
	void autosave(std::string reason);
	// `slot` empty: a text from saveText, announced without a slot.
	rules::Message loadText(const std::string& text, const std::string& slot);
	// Refusals shared by the developer commands, empty when the command may run.
	rules::Message devNeedsGame() const;
	rules::Message devNeedsField() const;
	// Pacing counts only play before the game is marked.
	bool pacingOn() const { return !world_.devMarked; }
	events::Handle newHandle(HandleState state);
	std::vector<rules::PartyMember> activeParty() const;
	Presenter* presenter() const;

	const GameData& data_;
	SessionOptions options_;
	std::unique_ptr<ActionQueue> actions_;
	std::optional<SaveStore> store_;
	Profile profile_;
	std::deque<LogEntry> textLog_;
	std::unique_ptr<GameHost> host_;
	World world_;
	ModeMachine modes_;
	PacingRecorder pacing_;
	std::vector<Output> output_;
	// Moving time aboard since the vehicle's last move sound (presentation.sounds.vehicles `move_ms`).
	int vehicleMoveMs_ = 0;
	std::int64_t clockCounter_ = 0;

	int tickRemainderMs_ = 0;
	// Held input spent toward the leader's next cell and time on a belt toward its next carry, in microseconds (whole
	// milliseconds under `step_timing` `ticks`).
	std::int64_t stepProgressUs_ = 0;
	std::int64_t conveyorUs_ = 0;
	// The area entry under way came from walking through an exit or a gangway, and whether the last one announced itself.
	bool arrivalWalked_ = false;
	void moveSceneCamera(const events::Action& action, int ms);
	bool arrivalAnnounced_ = true;
	std::string exitSound_;
	std::string pendingExitSound_;
	// The followers' placement the area entry under way asked for (an exit's, or game.json's for an exit or a gangway; a
	// town return's, port's, arrival marker's or script warp's own), and the placement of the last entry; unset at an
	// entry is game.json's `followers_arrive_other`.
	std::optional<FollowersArrive> walkedFollowers_;
	FollowersArrive arrivalFollowers_ = FollowersArrive::Stacked;
	// A path walk's step is under way, which touch hooks with `touch_from: keys` do not answer.
	bool pathStep_ = false;
	std::optional<Facing> latchedDirection_;
	// Not saved: hooks fire once per area entry and autosave points are judged per session (FLOW-050).
	bool loadHooksPending_ = false;
	bool ambientPending_ = false;
	std::set<std::string> loadHooksFired_;
	std::set<std::string> areasSinceAutosave_;
	// The autosave point waiting for control to return to the field, if any.
	std::string autosaveReason_;
	bool clearPending_ = false;
	// A script's `end game` ran; the event's end writes the clear-data record and goes back to the title.
	bool endGamePending_ = false;
	MenuKind menuKind_ = MenuKind::None;
	std::string shopId_;
	std::string innId_;
	std::string portId_;
	std::string nameMember_;
	std::string creditsId_;
	std::int64_t creditsElapsedMs_ = 0;
	// The open shop, save prompt, name entry or credits an event is waiting on.
	events::Handle menuHandle_ = 0;
	int autosaveCount_ = 0;
	bool encountersOn_ = true;
	// Per area: the flags and broken cells its rows were built for, and the copy area() hands out.
	mutable std::map<std::string, std::pair<std::string, Area>> areaViews_;
	// Actors whose cell blocks walking while they are shown: game.json homes with `solid`, and talk points with `actor_at`.
	std::set<std::string> solidActors_;
	std::map<std::string, std::size_t> actorDefIndex_;
	mutable std::map<std::string, AreaIndex> areaIndex_;
	// The index last asked for.
	mutable std::string indexedId_;
	mutable const AreaIndex* indexed_ = nullptr;
	mutable SessionStats stats_;
	// The route of the actor action being started, for its presented record.
	std::vector<Cell> actionRoute_;

	std::unique_ptr<events::Interpreter> interpreter_;
	std::vector<BackgroundRun> background_;
	bool backgroundChangedFlag_ = false;
	std::map<events::Handle, HandleState> handles_;
	events::Handle nextHandle_ = 1;
	events::Handle lineHandle_ = 0;
	events::Handle choiceHandle_ = 0;
	bool eventChangedFlag_ = false;
	// The slot a mid-event save was loaded from, until its battle is set up again.
	std::string resumedSlot_;
	std::string autoPlayMacro_;
	rules::MacroTrouble autoPlayTrouble_ = rules::MacroTrouble::Default;
	rules::MacroPolicy autoPlayPolicy_ = rules::MacroPolicy::Default;
	rules::MacroTroubleReport lastMacroTrouble_;

	std::optional<rules::Battle> battle_;
	std::optional<EndedBattle> ended_;
	BattleSource battleSource_ = BattleSource::Visible;
	std::string battleEncounter_;
	int battleEnemyId_ = 0;
	events::Handle battleHandle_ = 0;
	bool battleDefeatHandled_ = false;
	int battleRounds_ = 0;
	bool battleIsBoss_ = false;
	// The encounter stream before the battle seed was drawn: the pre-battle autosave holds it, so loading that
	// save sets up the same battle (BAT-134).
	std::uint64_t streamBeforeBattle_ = 0;

	bool gameOver_ = false;
	std::string retrySlot_;
	std::string pendingRetrySlot_;
	int retryCount_ = 0;
};

}
