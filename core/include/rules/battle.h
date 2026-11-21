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
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rules {

constexpr int kMaxPartySize = 5;
constexpr int kMaxEnemies = 8;

struct Rewards {
	int experience = 0;
	int currency = 0;
	// Experience each party member gets, by party index: an even share for each member not defeated.
	std::vector<int> memberExperience;
	// Item drops. The reward multiplier and the difficulty reward scale do not apply to them (SAV-023).
	Inventory items;
	// Equipment drops.
	EquipmentBag equipment = {};
};

// Why auto-battle turned itself off (BAT-104).
enum class AutoStop { None, Cancelled, MemberFell, CannotPay, BattleEnded };

class Battle;

// A party member as a battle sets it up before any difficulty overlay: level stats plus equipment, the basic attack a
// weapon replaces, the weapon's element, abilities and status resistances. The character must exist in `db`.
Combatant memberCombatant(const Database& db, const PartyMember& member);

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
	// Places in the enemy formation: the encounter's entries, free ones included, or more once an enemy joins past them.
	int formationSize() const;
	// The combatant that started the battle at a formation position, or -1 for a free place.
	int enemyAt(int position) const;
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
	// The encounter's drop is rolled once, from the battle's generator, when the battle ends in Victory.
	Rewards rewards() const { return rewards_; }
	// The vehicle's skill uses after the battle, to carry into the next one (B19). Empty outside vehicle battles.
	std::map<std::string, int> vehicleSkillUses() const;
	// Hit points, technique points, skill uses, equipment and the statuses that persist outside battle.
	std::vector<PartyMember> partyState() const;

	CancelReason checkUsable(int actor, const AbilityDef& ability) const;
	// Random enemy targeting returns every candidate; the battle draws one at execution. A single ally target stays
	// as named, fallen or not, and a gone foe resolves to the first living one: replacing it is the battle's job
	// when the action runs.
	std::vector<int> resolveTargets(int user, Targeting targeting, int requested, bool revives) const;
	std::vector<int> livingFoes(int actor) const;
	std::vector<int> allies(int actor) const;

private:
	// The stats an action's formulas read.
	struct Attacker {
		int strength = 0;
		int mental = 0;
		int dexterity = 0;
		int critical = 0;
		Element element = Element::None;
		// Basic attacks roll to hit and can be critical. Every other ability lands.
		bool rollsToHit = false;
		// An attack aimed at a group is never critical, however many targets are left.
		bool singleTarget = false;
		bool technique = false;
		// Aimed at a whole group or every enemy, for the HitByGroup override.
		bool groupTarget = false;
		// How far a stat change moves the stat: mental for a technique, strength for a skill.
		int changeAmount = 0;
		// A combo's damage uses its participants' summed stats and damage powers in place of its own.
		bool combo = false;
		int comboAttack = 0;
		int comboBonus = 0;
	};

	Battle() : rng_(0) {}

	static Battle partyOnly(const Database& db, const std::vector<PartyMember>& party);

	bool surprised(Side side, int round) const;
	Attacker attackerOf(int actor) const;
	const StatusDef* statusWith(int actor, StatusAction action) const;
	bool countsAsDown(int index) const;
	void drawDrops();
	Rewards computeRewards() const;
	void recoverMachines(std::vector<Event>& out);
	int addEnemy(const EnemyDef& def, const std::string& group, int position, int actor, std::vector<Event>& out);
	void transform(int target, const EnemyDef& def, std::vector<Event>& out);
	std::vector<int> freeSlotsBeside(int actor) const;
	const EquipmentDef* wornPiece(int actor, const std::string& id) const;
	const EquipmentDef* bagPiece(int actor, const std::string& id) const;
	void leaveBattle(int target, int actor, bool pays, std::vector<Event>& out);
	void split(int target, const EnemyDef& def, int count, std::vector<Event>& out);
	void useWornPiece(int actor, const Command& command, const EquipmentDef& piece, bool fromBag, std::vector<Event>& out);
	void fillMissingCommands();
	Command forcedCommand(int actor, const StatusDef& status);
	int drawFoeByPartyOrder(int actor);
	void chooseEnemyAction(int actor, std::vector<Event>& out);
	bool overrideHolds(const AiOverride& entry, int actor) const;
	std::vector<int> aiCandidates(int actor, const AbilityDef& ability, const AiTarget& target) const;
	int aiTarget(int actor, const AbilityDef& ability, const AiTarget& target);

	std::vector<int> computeTurnOrder();
	int tryCombo(const std::vector<int>& order, std::size_t index, std::vector<Event>& out);
	int tryEnemyCombo(const std::vector<int>& order, std::size_t index, std::vector<Event>& out);
	Attacker comboTeam(const std::vector<int>& participants, const std::vector<std::string>& components, const AbilityDef& combo, std::vector<Event>& out);

	// Returns the queued turns it used: 1, or more when an enemy combination fired.
	int takeTurn(const std::vector<int>& order, std::size_t index, std::vector<Event>& out);
	bool cancelledByStatus(int actor, const Command& command, std::vector<Event>& out);
	void cancel(int actor, const Command& command, CancelReason reason, const std::string& status, std::vector<Event>& out);
	int aimAt(int user, Targeting targeting, const std::vector<Effect>& effects, int requested, std::vector<Event>& out);
	std::vector<int> executionTargets(int user, Targeting targeting, const std::vector<Effect>& effects, int requested, std::vector<Event>& out);
	int primaryFoe(int user, int requested) const;
	int retargetFoe(int user, const std::vector<Effect>& effects);
	int macroAllyTarget(int actor, const std::vector<Effect>& effects) const;
	void useAbility(int actor, const AbilityDef& ability, int target, CommandKind kind, std::vector<Event>& out);
	void useItem(int actor, const Command& command, std::vector<Event>& out);
	void attemptEscape(int actor, std::vector<Event>& out);
	int rollSpread();
	void payCost(int actor, const AbilityDef& ability, std::vector<Event>& out);
	// Returns whether any effect changed the battle state.
	bool applyEffects(int user, const Attacker& attacker, const std::string& source, const std::vector<Effect>& effects, const std::vector<int>& targets, bool flatPower, std::vector<Event>& out);
	int dealDamage(int user, int target, int amount, Element element, Effectiveness effectiveness, std::vector<Event>& out);
	void noteHit(int user, int target, const Attacker& attacker, bool item);
	void defeat(int target, std::vector<Event>& out);
	void removeStatus(int target, const std::string& statusId);
	void syncPins(int target);
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
	// Per combatant: whether the enemy AI picks this round's action when the enemy's turn comes.
	std::vector<bool> aiTurn_;
	// The action slot the AI drew last, for every enemy; -1 before the first draw.
	int previousSlot_ = -1;
	bool quarterOverrideUsed_ = false;
	std::vector<int> lastTurnOrder_;
	Inventory inventory_;
	Inventory drops_;
	EquipmentBag pieceDrops_;
	// Vehicle battles: the vehicle's combatant index, or -1.
	int vehicleIndex_ = -1;
	Outcome outcome_ = Outcome::Ongoing;
	int round_ = 0;
	int partyCount_ = 0;
	// Combatants the battle started with; enemies that join later come after them.
	int startingCount_ = 0;
	Rewards rewards_;
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
