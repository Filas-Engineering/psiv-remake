#pragma once

#include "rules/combatant.h"
#include "rules/combo.h"
#include "rules/command.h"
#include "rules/database.h"
#include "rules/event.h"
#include "rules/field.h"
#include "rules/macro.h"
#include "rules/message.h"
#include "rules/record.h"
#include "rules/result.h"
#include "rules/rng.h"
#include "rules/setup.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rules {

constexpr int kMaxPartySize = 5;
constexpr int kMaxEnemies = 8;

struct Rewards {
	int experience = 0;
	int currency = 0;
	// Experience each party member gets, by party index, after the experience split.
	std::vector<int> memberExperience;
	// Item drops. The reward multiplier and the difficulty reward scale do not apply to them (SAV-023).
	Inventory items;
};

// Why auto-battle turned itself off (BAT-104).
enum class AutoStop { None, Cancelled, MemberFell, CannotPay, BattleEnded };

class Battle;

// Rebuilds a battle from its record. `log` starts from the combos the record says were known.
Result<Battle> replayBattle(const Database& db, const BattleRecord& record, ComboLog& log, std::vector<Event>& events);

// Combatant indexes are stable for the whole battle: party members first, in lineup order, then enemies in
// formation order. The battle keeps its own copy of the data with the difficulty overlay applied. The combo
// log must outlive the battle.
class Battle {
public:
	static Result<Battle> create(const Database& db, const BattleSetup& setup, ComboLog* comboLog);

	const Database& database() const { return *db_; }
	const BattleSetup& setup() const { return record_.setup; }
	const BattleRecord& record() const { return record_; }
	const std::vector<Combatant>& combatants() const { return combatants_; }
	const Combatant& combatant(int index) const { return combatants_[index]; }
	Combatant& combatant(int index) { return combatants_[index]; }
	int partyCount() const { return partyCount_; }
	bool isParty(int index) const { return index < partyCount_; }
	const Inventory& inventory() const { return inventory_; }
	int round() const { return round_; }
	Outcome outcome() const { return outcome_; }
	// Defeat in a battle without a defeat branch.
	bool gameOver() const { return outcome_ == Outcome::Defeat && !record_.setup.flags.defeatHasBranch; }
	const Command& command(int actor) const { return commands_[actor]; }
	const std::vector<int>& lastTurnOrder() const { return lastTurnOrder_; }
	bool escapeAllowed() const { return record_.setup.flags.escapeAllowed; }
	// False while the opening makes that side lose the coming round.
	bool sideActsNextRound(Side side) const;

	// Returns an empty message on success, otherwise why the command was rejected.
	Message setCommand(int actor, const Command& command);

	// Sets commands for the macro's members and makes each one act no earlier than the member listed ahead of it.
	std::vector<MacroStepResult> applyMacro(const Macro& macro);

	// The host's play time, stored in the combo log when a combination is first discovered.
	void setPlayTime(std::int64_t playTimeMs) { playTimeMs_ = playTimeMs; }

	// Turns auto-battle on with this macro. Returns false when the battle has already ended.
	bool startAuto(const Macro& macro);
	// The player's cancel: auto-battle stops at the next command phase.
	void cancelAuto() { autoCancelRequested_ = true; }
	// Call at each command phase. While auto-battle is on and no stop rule applies, fills the round from the
	// auto macro and returns true. Otherwise auto-battle is off, autoStop() says why, and the round is empty.
	bool fillFromAuto();
	bool autoActive() const { return autoActive_; }
	AutoStop autoStop() const { return autoStop_; }

	// Party members without a command attack; enemies without one ask their AI.
	std::vector<Event> resolveRound();

	// The party's commands from the last resolved round, in the order they acted, with targets as rules.
	Macro lastRoundAsMacro(const std::string& name) const;

	// Victory: every enemy's rewards. Scripted end: the rewards of the enemies that fell. Otherwise none.
	// Drops are drawn once, from the battle's generator, when the battle ends.
	Rewards rewards() const;
	// Hit points, technique points, skill uses, equipment and the statuses that persist outside battle.
	std::vector<PartyMember> partyState() const;

	CancelReason checkUsable(int actor, const AbilityDef& ability) const;
	// Random enemy targeting returns every candidate; the battle draws one at execution.
	std::vector<int> resolveTargets(int user, Targeting targeting, int requested, bool revives) const;
	std::vector<int> livingFoes(int actor) const;
	std::vector<int> allies(int actor) const;

private:
	// The stats an action's formulas read. A combo averages its participants'.
	struct Attacker {
		int strength = 0;
		int mental = 0;
		int dexterity = 0;
		int critical = 0;
		Element element = Element::None;
	};

	Battle() : rng_(0) {}

	static Battle partyOnly(const Database& db, const std::vector<PartyMember>& party);

	bool surprised(Side side, int round) const;
	Attacker attackerOf(int actor) const;
	const StatusDef* statusWith(int actor, StatusAction action) const;
	bool countsAsDown(int index) const;
	void drawDrops();
	void passDamageToCrew(int user, int dealt, std::vector<Event>& out);
	void fillMissingCommands();
	Command forcedCommand(int actor, const StatusDef& status);
	Command chooseEnemyCommand(int actor);
	bool conditionHolds(const AiCondition& condition, int actor) const;
	std::vector<int> aiCandidates(int actor, const AbilityDef& ability, const AiRule& rule) const;
	int aiTarget(int actor, const AbilityDef& ability, const AiRule& rule);

	std::vector<int> computeTurnOrder();
	int tryCombo(const std::vector<int>& order, std::size_t index, std::vector<Event>& out);

	void takeTurn(int actor, std::vector<Event>& out);
	bool cancelledByStatus(int actor, const Command& command, std::vector<Event>& out);
	void cancel(int actor, const Command& command, CancelReason reason, const std::string& status, std::vector<Event>& out);
	std::vector<int> executionTargets(int user, Targeting targeting, int requested, bool revives);
	int primaryFoe(int user, int requested) const;
	void noteRetarget(int actor, Targeting targeting, int requested, const std::vector<int>& targets, std::vector<Event>& out);
	void useAbility(int actor, const AbilityDef& ability, int target, CommandKind kind, std::vector<Event>& out);
	void useItem(int actor, const Command& command, std::vector<Event>& out);
	void attemptEscape(int actor, std::vector<Event>& out);
	void payCost(int actor, const AbilityDef& ability, std::vector<Event>& out);
	// Returns whether any effect changed the battle state.
	bool applyEffects(int user, const Attacker& attacker, const std::string& source, const std::vector<Effect>& effects, const std::vector<int>& targets, bool flatPower, std::vector<Event>& out);
	int dealDamage(int user, int target, int amount, Element element, Effectiveness effectiveness, std::vector<Event>& out);
	void defeat(int target, std::vector<Event>& out);
	void removeStatus(int target, const std::string& statusId);
	void emitStatusRemoved(int actor, int target, const std::string& statusId, StatusEnd end, std::vector<Event>& out);

	void endOfRound(std::vector<Event>& out);
	void updateOutcome();
	Event& emit(std::vector<Event>& out, EventType type);

	std::shared_ptr<const Database> db_;
	Rng rng_;
	ComboLog* comboLog_ = nullptr;
	BattleRecord record_;
	std::vector<Combatant> combatants_;
	std::vector<Command> commands_;
	std::vector<Command> lastCommands_;
	std::vector<int> forcedOrder_;
	std::string macroName_;
	// Per combatant: whether a status supplied this round's command.
	std::vector<bool> forced_;
	std::vector<int> lastTurnOrder_;
	std::vector<const ComboRecipe*> combosLongestFirst_;
	Inventory inventory_;
	Inventory drops_;
	// Vehicle battles: the vehicle's combatant index, or -1.
	int vehicleIndex_ = -1;
	int crewDamagePercent_ = 0;
	Outcome outcome_ = Outcome::Ongoing;
	int round_ = 0;
	int partyCount_ = 0;
	int rewardPercent_ = 100;
	std::int64_t playTimeMs_ = 0;
	Macro autoMacro_;
	bool autoActive_ = false;
	bool autoCancelRequested_ = false;
	bool autoMemberFell_ = false;
	AutoStop autoStop_ = AutoStop::None;

	friend Result<Battle> replayBattle(const Database& db, const BattleRecord& record, ComboLog& log, std::vector<Event>& events);
	friend FieldUse useItemOutsideBattle(const Database& db, std::vector<PartyMember>& party, Inventory& inventory, const std::string& itemId, int target);
	friend FieldUse useAbilityOutsideBattle(const Database& db, std::vector<PartyMember>& party, int user, const std::string& abilityId, int target);
};

}
