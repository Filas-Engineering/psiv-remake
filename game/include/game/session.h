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
enum class MenuKind { None, Field, Inn, SavePrompt, NameEntry, Port };

const char* toString(MenuKind kind);

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
	// NoItem; None when nothing stops it outside battle.
	rules::MacroIssue issue = rules::MacroIssue::None;
};

struct MacroView {
	std::string name;
	std::vector<MacroStepView> steps;
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
	// Percent of each element's damage taken, every element listed.
	std::map<rules::Element, int> affinities;
	// The character's status resistances plus its pieces', capped at 100, as a battle combatant gets them.
	rules::StatusResists statusResist;
};

// Orders for listing the bag. The bag itself has no order (items and pieces are kept by id), so a sort is a read and
// nothing about it is saved. Kind: field items, battle-only items, then pieces by slot; Name: display name; Count: most
// held first. Ties go to the display name, then the id.
enum class ItemSort { Id, Kind, Name, Count };

const char* toString(ItemSort sort);

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
};

// One of the party's vehicles in the party's area, from Session::vehiclesHere() (FLD-060).
struct VehicleView {
	std::string id;
	Cell cell;
	Facing facing = Facing::South;
	// The party rides it, so it stands on the leader's cell.
	bool aboard = false;
};

// Ways to travel at once (FLD-051): from a port to another open port, back to a town visited before in the region the
// party is in, or out of the dungeon the party is in to where it stood before going in.
enum class TravelKind { Port, Town, DungeonExit };

const char* toString(TravelKind kind);
bool parseTravelKind(const std::string& text, TravelKind& out);

struct TravelDestination {
	// A port id for Port, an area id for Town and DungeonExit.
	std::string id;
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
	// An item's, piece's, ability's, status's, enemy's or difficulty's description in the profile's language, name
	// tokens rendered; empty when that language has none.
	std::string description(DescriptionKind kind, const std::string& id) const;

	// Every operation below that returns a Message returns an empty one when it ran, otherwise the text key and
	// parameters of why it did not; render() shows it.

	// Title (FLOW-020).
	rules::Message newGame();
	rules::Message continueGame();
	rules::Message load(const std::string& slot);
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
	// Turns the leader in place.
	rules::Message face(Facing direction);
	// A walk to `goal` along the shortest path over walkable cells, avoiding exits other than the goal. Visible
	// enemies are not avoided, so bumping into one starts its battle. Call walkStep until it returns something
	// other than Walking, and run events and battles between the calls: an event or battle on the way stops the
	// walk only if the field does not come back. Walking ends at the goal (Arrived), when the party leaves the
	// area (LeftArea), when the field does not come back (Stopped), without a path (NoPath) or after 4000 steps
	// (TooLong).
	PathWalk beginWalk(Cell goal) const;
	WalkStatus walkStep(PathWalk& walk);
	// The actors in the party's area, in game.json order, hidden ones included.
	std::vector<ActorView> actorsHere() const;
	// Interacts with whatever stands in the cell the leader faces (FLD-030): a vehicle is boarded, a port opens its
	// destinations. Refused while the party rides a vehicle.
	rules::Message interact();
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
	// in. Empty when there is none.
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
	rules::Message applyMacro(const std::string& name, std::vector<rules::MacroStepResult>* results = nullptr);
	// Auto-battle (BAT-103, BAT-104) with the save's auto macro. fillFromAuto fills the round and returns true
	// while it runs; once a stop rule fires it returns false and reports why.
	rules::Message startAuto();
	void cancelAuto();
	bool autoActive() const;
	bool fillFromAuto();
	// Core's simple per-member policy for every party member who acts this round: how the terminal plays a
	// battle on its own.
	void fillFromPolicy();
	// Fills the round the way auto play does: on the first round, starts auto-battle with the save's auto macro
	// when one is set; then fillFromAuto, or fillFromPolicy once auto-battle is off. Returns why auto-battle did
	// not start, if it did not.
	rules::Message fillAutoRound();
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
	rules::Message buy(const std::string& id, int count);
	rules::Message sell(const std::string& id, int count);
	rules::Message rest();
	// Field use through core (BAT-044). `member` is a character id of the active party, or empty for items and
	// techniques that work on everyone. An item that would change nothing is not used up.
	rules::Message useItem(const std::string& item, const std::string& member);
	rules::Message useAbility(const std::string& user, const std::string& ability, const std::string& member);
	rules::Message equip(const std::string& member, const std::string& equipment);
	// At a position the piece fits: a hand for weapons and shields (P10).
	rules::Message equip(const std::string& member, const std::string& equipment, rules::EquipSlot position);
	rules::Message unequip(const std::string& member, rules::EquipSlot slot);
	// From the field menu, or from a save prompt an event opened.
	rules::Message saveManual(const std::string& slot);
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
	// Sets a roster member's level with no experience into it, and refills it as a rest does, reviving it.
	rules::Message devSetLevel(const std::string& member, int level);
	// Starts a battle with the encounter from the field, as a random battle (a vehicle battle while riding).
	rules::Message devBattle(const std::string& encounter);
	// Ends the live battle: every enemy (victory) or every party combatant (defeat) falls, then a round resolves, so the
	// battle ends the way that round's outcome leads. `events` gets the round's events.
	rules::Message devEndBattle(bool victory, std::vector<rules::Event>* events = nullptr);
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

private:
	class GameHost;
	class ActionQueue;
	friend class GameHost;

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
	// The id of a vehicle of the party standing on `cell` of the party's area, the ridden one left out.
	std::string vehicleAt(Cell cell) const;
	// Leaves the ridden vehicle where it stands without stepping off, when a warp or a travel takes the party away.
	void leaveVehicleBehind();
	// Puts a vehicle down, or back at its last boarding point when the party could not reach it there (FLD-063).
	void parkVehicle(const std::string& id, const std::string& area, Cell cell);
	// Off the ridden vehicle onto its cell, or the nearest cell the party can walk on.
	void dismount();
	void healMachinesOnStep();
	void shareWithMembersOutside(const std::vector<rules::PartyMember>& fought, const rules::Rewards& rewards);
	void arrive(Cell cell);
	void settleField();
	const TalkSet* talkSet() const;
	// Lines of `set` whose speakers are in the active party, as output of `kind`; false when none is.
	bool speak(const TalkSet& set, OutputKind kind);
	bool startEvent(const std::string& event);
	void pumpEvent(double seconds);
	void endEvent();
	void dropEvent();
	void openChest(const Chest& chest);
	void boardShip(const Port& port);
	rules::Message callVehicle(const FieldVehicle& vehicle);
	void applyActorAction(const events::Action& action);
	std::optional<Cell> actorCell(const std::string& actor) const;
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
	std::int64_t clockCounter_ = 0;

	int tickRemainderMs_ = 0;
	int stepProgressMs_ = 0;
	std::optional<Facing> latchedDirection_;
	// Not saved: hooks fire once per area entry and autosave points are judged per session (FLOW-050).
	bool loadHooksPending_ = false;
	bool ambientPending_ = false;
	std::set<std::string> loadHooksFired_;
	std::set<std::string> areasSinceAutosave_;
	// The autosave point waiting for control to return to the field, if any.
	std::string autosaveReason_;
	bool clearPending_ = false;
	MenuKind menuKind_ = MenuKind::None;
	std::string shopId_;
	std::string innId_;
	std::string portId_;
	std::string nameMember_;
	// The open shop, save prompt or name entry an event is waiting on.
	events::Handle menuHandle_ = 0;
	int autosaveCount_ = 0;
	bool encountersOn_ = true;
	// Per area: the flags and broken cells its rows were built for, and the copy area() hands out.
	mutable std::map<std::string, std::pair<std::string, Area>> areaViews_;

	std::unique_ptr<events::Interpreter> interpreter_;
	std::map<events::Handle, HandleState> handles_;
	events::Handle nextHandle_ = 1;
	events::Handle lineHandle_ = 0;
	events::Handle choiceHandle_ = 0;
	bool eventChangedFlag_ = false;
	// The slot a mid-event save was loaded from, until its battle is set up again.
	std::string resumedSlot_;

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
