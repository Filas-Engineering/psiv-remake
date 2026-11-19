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

// Something the front end shows. Battle rounds are returned as core events instead. `message` is the text key
// and its parameters; `text` is it rendered from the English tables. Warnings about broken data have no key.
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

// Plays presentation-only script actions (moves, cameras, panels, audio). The default treats every action as
// instant, which is what the terminal and the tests need. Warps are applied to the world before this is called.
class Presenter {
public:
	virtual ~Presenter() = default;
	// Returns true when the action has already finished.
	virtual bool begin(std::uint64_t handle, const events::Action& action) = 0;
	virtual bool finished(std::uint64_t handle) = 0;
	virtual void cancel(std::uint64_t handle) = 0;
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
	Presenter* presenter = nullptr;
};

struct FieldInput {
	bool held = false;
	Facing direction = Facing::South;
};

enum class BattleSource { Visible, Random, Event };

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
	// Applies at once and writes the profile file (SAV-005).
	std::string setProfile(const Profile& profile);

	// Text from the game's tables: a key with its parameters, ids shown as display names.
	std::string render(const rules::Message& message) const;
	// A script text: string IDs looked up, name tokens rendered for the current profile.
	std::string scriptText(const events::Text& text) const;
	// A character's chosen name, its own name, or an actor's speaker name.
	std::string displayName(const std::string& id) const;

	// Title (FLOW-020). Each returns an empty string or why it could not run.
	std::string newGame();
	std::string continueGame();
	std::string load(const std::string& slot);
	// After a game over, reloads the autosave taken before the lost event battle (FLOW-042).
	std::string retryBattle();
	bool gameOver() const { return gameOver_; }
	bool canRetry() const { return gameOver_ && !retrySlot_.empty(); }
	// The pause overlay's Quit to Title (FLOW-022). Not a transition: it resets the machine.
	void quitToTitle();

	// Advances the active mode by `ms`. In Field, `input` is the held direction.
	void update(int ms, const FieldInput& input = FieldInput());
	// Holds `direction` until the leader enters the next cell or something stops the field.
	std::string walk(Facing direction);
	// Interacts with whatever stands in the cell the leader faces (FLD-030).
	std::string interact();
	// Party talk (FLD-070): the lines of the best matching set for this area and the flags, as Line output.
	std::string partyTalk();

	// Event
	bool linePending() const;
	// The pending line with names rendered for the current settings, speaker first.
	std::string lineText() const;
	void confirmLine();
	const events::ChoiceRequest* choice() const;
	std::string choose(int index);
	bool canSkip() const;
	bool skip();
	// The last 500 pages since the game was loaded, rendered for the current profile, skipped ones marked.
	std::vector<std::string> textLog() const;

	// Battle
	const rules::Battle* battle() const { return battle_ ? &*battle_ : nullptr; }
	std::string setCommand(int actor, const rules::Command& command);
	std::string applyMacro(const std::string& name, std::vector<rules::MacroStepResult>* results = nullptr);
	// Auto-battle (BAT-103, BAT-104) with the save's auto macro. fillFromAuto fills the round and returns true
	// while it runs; once a stop rule fires it returns false and reports why.
	std::string startAuto();
	void cancelAuto();
	bool autoActive() const;
	bool fillFromAuto();
	// Core's simple per-member policy for every party member who acts this round: how the terminal plays a
	// battle on its own.
	void fillFromPolicy();
	// Resolves one round. The battle ends inside this call when an outcome is reached.
	std::vector<rules::Event> resolveRound();
	BattleSource battleSource() const { return battleSource_; }
	const std::string& battleEncounter() const { return battleEncounter_; }

	// Menus and shops. A shop, save prompt or name entry an event opened closes back to the event.
	std::string openMenu();
	std::string closeMenu();
	MenuKind menuKind() const { return mode() == Mode::Menu ? menuKind_ : MenuKind::None; }
	const Shop* shop() const;
	const RestPoint* inn() const;
	std::string buy(const std::string& id, int count);
	std::string sell(const std::string& id, int count);
	std::string rest();
	// Field use through core (BAT-044). `member` is a character id of the active party, or empty for items and
	// techniques that work on everyone. An item that would change nothing is not used up.
	std::string useItem(const std::string& item, const std::string& member);
	std::string useAbility(const std::string& user, const std::string& ability, const std::string& member);
	std::string equip(const std::string& member, const std::string& equipment);
	std::string unequip(const std::string& member, rules::EquipSlot slot);
	// From the field menu, or from a save prompt an event opened.
	std::string saveManual(const std::string& slot);
	// Name entry: the member being named, and the name to confirm, which closes the entry.
	const std::string& nameEntryMember() const { return nameMember_; }
	std::string enterName(const std::string& name);
	// Difficulty and reward multiplier apply from the next battle (BAT-126).
	std::string setSettings(const Settings& settings);

	QuestLog questLog() const;
	std::vector<EncyclopediaView> encyclopedia() const;

	// The active party's average level in hundredths.
	int averageLevelX100() const;
	SaveFile makeSave(bool autosave) const;

private:
	class GameHost;
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
	void giveThing(const std::string& id, int count);
	void reportFieldUse(const std::vector<rules::Event>& events, const std::vector<rules::PartyMember>& party);
	std::string payRest(const RestPoint* point);
	void openScriptMenu(Mode mode, MenuKind kind, events::Handle handle);
	void finishScriptMenu();
	void addLog(const events::Line& line, bool skipped);
	void refreshJournal(bool announce);
	void writeClearRecord();
	std::string startBattle(const std::string& encounter, rules::Opening opening, bool escapeAllowed, bool defeatHasBranch);
	void startFieldBattle(const EncounterStart& start);
	void finishBattle();
	void enterGameOver();
	void autosave(std::string reason);
	std::string loadText(const std::string& text, const std::string& slot);
	events::Handle newHandle(HandleState state);
	std::vector<rules::PartyMember> activeParty() const;

	const GameData& data_;
	SessionOptions options_;
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
