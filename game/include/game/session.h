#pragma once

#include "game/area.h"
#include "game/data.h"
#include "game/encounters.h"
#include "game/modes.h"
#include "game/pacing.h"
#include "game/save.h"
#include "game/world.h"

#include "events/host.h"
#include "events/interpreter.h"
#include "rules/battle.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace game {

enum class OutputKind { Line, Choice, Action, Notice, Warning, Autosave };

// Something the front end shows. Battle rounds are returned as core events instead.
struct Output {
	OutputKind kind = OutputKind::Notice;
	std::string text;
};

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
	// Empty disables writing: autosave points are still counted and reported.
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

	// Event
	bool linePending() const;
	// The pending line with names rendered for the current settings, speaker first.
	std::string lineText() const;
	void confirmLine();
	const events::ChoiceRequest* choice() const;
	std::string choose(int index);
	bool canSkip() const;
	bool skip();

	// Battle
	const rules::Battle* battle() const { return battle_ ? &*battle_ : nullptr; }
	std::string setCommand(int actor, const rules::Command& command);
	std::string applyMacro(const std::string& name, std::vector<rules::MacroStepResult>* results = nullptr);
	// Resolves one round. The battle ends inside this call when an outcome is reached.
	std::vector<rules::Event> resolveRound();
	// Fills every party command from the auto-battle macro, or core's auto policy, then resolves.
	std::vector<rules::Event> autoRound();
	BattleSource battleSource() const { return battleSource_; }
	const std::string& battleEncounter() const { return battleEncounter_; }

	// Menus and shops
	std::string openMenu();
	std::string closeMenu();
	const Shop* shop() const;
	const RestPoint* inn() const;
	std::string buy(const std::string& id, int count);
	std::string sell(const std::string& id, int count);
	std::string rest();
	// Field item use on a party member by character id. An item that would change nothing is not used up.
	std::string useItem(const std::string& item, const std::string& member);
	std::string equip(const std::string& member, const std::string& equipment);
	std::string unequip(const std::string& member, rules::EquipSlot slot);
	std::string saveManual(const std::string& slot);
	// Profile settings apply at once; difficulty and reward multiplier from the next battle (BAT-126).
	std::string setSettings(const Settings& settings);

	// The active party's average level in hundredths.
	int averageLevelX100() const;
	SaveFile makeSave(bool autosave) const;

private:
	class GameHost;
	friend class GameHost;

	enum class HandleKind { Line, Choice, Battle, Action };
	struct HandleState {
		HandleKind kind = HandleKind::Action;
		bool done = false;
		int result = 0;
		events::Line line;
		events::ChoiceRequest choice;
	};

	void emit(OutputKind kind, const std::string& text);
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
	std::string startBattle(const std::string& encounter, rules::Opening opening, bool escapeAllowed, bool defeatHasBranch);
	void startFieldBattle(const EncounterStart& start);
	void finishBattle();
	void enterGameOver();
	void autosave(std::string reason);
	std::string loadText(const std::string& text, const std::string& slot);
	events::Handle newHandle(HandleState state);
	std::vector<rules::PartyMember> activeParty() const;
	std::string memberName(const std::string& characterId) const;

	const GameData& data_;
	SessionOptions options_;
	std::optional<SaveStore> store_;
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
	std::string shopId_;
	std::string innId_;
	int autosaveCount_ = 0;

	std::unique_ptr<events::Interpreter> interpreter_;
	std::map<events::Handle, HandleState> handles_;
	events::Handle nextHandle_ = 1;
	events::Handle lineHandle_ = 0;
	events::Handle choiceHandle_ = 0;
	bool eventChangedFlag_ = false;
	bool battleAutosavePending_ = false;

	std::optional<rules::Battle> battle_;
	BattleSource battleSource_ = BattleSource::Visible;
	std::string battleEncounter_;
	int battleEnemyId_ = 0;
	events::Handle battleHandle_ = 0;
	bool battleDefeatHandled_ = false;
	int battleRounds_ = 0;
	bool battleIsBoss_ = false;

	bool gameOver_ = false;
	std::string retrySlot_;
	std::string pendingRetrySlot_;
	int retryCount_ = 0;
};

}
