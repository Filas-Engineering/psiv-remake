#pragma once

#include "rules/combatant.h"
#include "rules/combo.h"
#include "rules/command.h"
#include "rules/database.h"
#include "rules/event.h"
#include "rules/macro.h"
#include "rules/record.h"
#include "rules/result.h"
#include "rules/rng.h"
#include "rules/setup.h"

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
};

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
	const Command& command(int actor) const { return commands_[actor]; }
	const std::vector<int>& lastTurnOrder() const { return lastTurnOrder_; }
	bool escapeAllowed() const { return record_.setup.flags.escapeAllowed; }
	// False while the opening makes that side lose the coming round.
	bool sideActsNextRound(Side side) const;

	// Returns an empty string on success, otherwise why the command was rejected.
	std::string setCommand(int actor, const Command& command);

	// Sets commands for the macro's members and makes them act as one block, in step order.
	std::vector<MacroStepResult> applyMacro(const Macro& macro);

	// Party members without a command attack; enemies without one ask their AI.
	std::vector<Event> resolveRound();

	// The party's commands from the last resolved round, in the order they acted, with targets as rules.
	Macro lastRoundAsMacro(const std::string& name) const;

	Rewards rewards() const;
	std::vector<PartyMember> partyState() const;

	FailReason checkUsable(int actor, const AbilityDef& ability) const;
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

	bool surprised(Side side, int round) const;
	Attacker attackerOf(int actor) const;
	void fillMissingCommands();
	Command chooseEnemyCommand(int actor);
	bool conditionHolds(const AiCondition& condition, int actor) const;
	int aiTarget(int actor, const AbilityDef& ability, const AiCondition& condition);

	std::vector<int> computeTurnOrder();
	int tryCombo(const std::vector<int>& order, std::size_t index, std::vector<Event>& out);

	void takeTurn(int actor, std::vector<Event>& out);
	bool preventedByStatus(int actor, std::vector<Event>& out);
	bool skipsTurn(int actor) const;
	void useAbility(int actor, const AbilityDef& ability, int target, CommandKind kind, std::vector<Event>& out);
	void useItem(int actor, const Command& command, std::vector<Event>& out);
	void attemptEscape(int actor, std::vector<Event>& out);
	void payCost(int actor, const AbilityDef& ability, std::vector<Event>& out);
	// Returns whether any effect changed the battle state.
	bool applyEffects(int user, const Attacker& attacker, const std::vector<Effect>& effects, const std::vector<int>& targets, bool flatPower, std::vector<Event>& out);
	int dealDamage(int target, int amount, Element element, Effectiveness effectiveness, std::vector<Event>& out);
	void defeat(int target, std::vector<Event>& out);
	void removeStatus(int target, const std::string& statusId);

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
	std::vector<int> lastTurnOrder_;
	std::vector<const ComboRecipe*> combosLongestFirst_;
	Inventory inventory_;
	Outcome outcome_ = Outcome::Ongoing;
	int round_ = 0;
	int partyCount_ = 0;
	int rewardPercent_ = 100;

	friend Result<Battle> replayBattle(const Database& db, const BattleRecord& record, ComboLog& log, std::vector<Event>& events);
};

}
