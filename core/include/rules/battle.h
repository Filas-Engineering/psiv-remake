#pragma once

#include "rules/combatant.h"
#include "rules/combo.h"
#include "rules/command.h"
#include "rules/database.h"
#include "rules/event.h"
#include "rules/macro.h"
#include "rules/result.h"
#include "rules/rng.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rules {

constexpr int kMaxPartySize = 5;
constexpr int kMaxEnemies = 8;

struct Rewards {
	int experience = 0;
	int currency = 0;
};

// Combatant indexes are stable for the whole battle: party members first, in lineup order, then enemies.
// The database and combo log must outlive the battle.
class Battle {
public:
	static Result<Battle> create(
		const Database& db,
		const std::vector<PartyMember>& party,
		const std::vector<std::string>& enemyIds,
		const Inventory& inventory,
		std::uint64_t seed,
		ComboLog* comboLog);

	const Database& database() const { return *db_; }
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

	// Returns an empty string on success, otherwise why the command was rejected.
	std::string setCommand(int actor, const Command& command);

	// Sets commands for the macro's members and makes them act as one block, in step order.
	std::vector<MacroStepResult> applyMacro(const Macro& macro);

	// Party members without a command attack; enemies without one ask their AI.
	std::vector<Event> resolveRound();

	Rewards rewards() const;
	std::vector<PartyMember> partyState() const;

	FailReason checkUsable(int actor, const AbilityDef& ability) const;
	std::vector<int> resolveTargets(int user, Targeting targeting, int requested, bool revives) const;
	std::vector<int> livingFoes(int actor) const;
	std::vector<int> allies(int actor) const;

private:
	Battle() : rng_(0) {}

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
	void attemptFlee(int actor, std::vector<Event>& out);
	void payCost(int actor, const AbilityDef& ability);
	void applyEffects(int user, const std::vector<Effect>& effects, const std::vector<int>& targets, bool flatPower, std::vector<Event>& out);
	void dealDamage(int target, int amount, Element element, Effectiveness effectiveness, std::vector<Event>& out);
	void defeat(int target, std::vector<Event>& out);
	void removeStatus(int target, const std::string& statusId);

	void endOfRound(std::vector<Event>& out);
	void updateOutcome();
	Event& emit(std::vector<Event>& out, EventType type);

	const Database* db_ = nullptr;
	Rng rng_;
	ComboLog* comboLog_ = nullptr;
	std::vector<Combatant> combatants_;
	std::vector<Command> commands_;
	std::vector<int> forcedOrder_;
	std::vector<int> lastTurnOrder_;
	std::vector<const ComboRecipe*> combosLongestFirst_;
	std::vector<PartyMember> partySource_;
	Inventory inventory_;
	Outcome outcome_ = Outcome::Ongoing;
	int round_ = 0;
	int partyCount_ = 0;
};

}
