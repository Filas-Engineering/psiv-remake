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

enum class OutputKind { Line, Choice, Action, Notice, Warning, Autosave };

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

// What the Menu mode is showing. Shop has its own mode.
enum class MenuKind { None, Field, Inn, SavePrompt, NameEntry };

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
	const Area* area() const;
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
	// Interacts with whatever stands in the cell the leader faces (FLD-030).
	rules::Message interact();
	// Party talk (FLD-070): the lines of the best matching set for this area and the flags, as Line output.
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
	const RestPoint* inn() const;
	rules::Message buy(const std::string& id, int count);
	rules::Message sell(const std::string& id, int count);
	rules::Message rest();
	// Field use through core (BAT-044). `member` is a character id of the active party, or empty for items and
	// techniques that work on everyone. An item that would change nothing is not used up.
	rules::Message useItem(const std::string& item, const std::string& member);
	rules::Message useAbility(const std::string& user, const std::string& ability, const std::string& member);
	rules::Message equip(const std::string& member, const std::string& equipment);
	rules::Message unequip(const std::string& member, rules::EquipSlot slot);
	// From the field menu, or from a save prompt an event opened.
	rules::Message saveManual(const std::string& slot);
	// Name entry: the member being named, and the name to confirm, which closes the entry.
	const std::string& nameEntryMember() const { return nameMember_; }
	rules::Message enterName(const std::string& name);
	// Difficulty and reward multiplier apply from the next battle (BAT-126).
	rules::Message setSettings(const Settings& settings);
	// One of the save's macros, or empty for none.
	rules::Message setAutoMacro(const std::string& macro);

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
	void enterArea(const std::string& areaId, const std::string& marker);
	void arrive(Cell cell);
	void settleField();
	bool startEvent(const std::string& event);
	void pumpEvent(double seconds);
	void endEvent();
	void dropEvent();
	void openChest(const Chest& chest);
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
	void startFieldBattle(const EncounterStart& start);
	void finishBattle();
	void enterGameOver();
	void autosave(std::string reason);
	rules::Message loadText(const std::string& text, const std::string& slot);
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
	std::set<std::string> loadHooksFired_;
	std::set<std::string> areasSinceAutosave_;
	// The autosave point waiting for control to return to the field, if any.
	std::string autosaveReason_;
	bool clearPending_ = false;
	MenuKind menuKind_ = MenuKind::None;
	std::string shopId_;
	std::string innId_;
	std::string nameMember_;
	// The open shop, save prompt or name entry an event is waiting on.
	events::Handle menuHandle_ = 0;
	int autosaveCount_ = 0;

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
