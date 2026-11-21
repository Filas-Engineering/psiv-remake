#pragma once

#include "rules/command.h"
#include "rules/types.h"

#include <string>
#include <vector>

namespace rules {

enum class Outcome { Ongoing, Victory, Defeat, Escaped, ScriptedEnd };

enum class EventType {
	BattleStarted,
	RoundStarted,
	CommandsLocked,
	OrderResolved,
	Retargeted,
	ActionStarted,
	ActionCancelled,
	ComboTriggered,
	ComboDiscovered,
	ResourceSpent,
	ItemHadNoEffect,
	Hit,
	Missed,
	Critical,
	DamageDealt,
	Healed,
	Revived,
	StatusApplied,
	StatusResisted,
	StatusRemoved,
	StatModified,
	CombatantDefeated,
	EscapeAttempted,
	Message,
	RoundEnded,
	BattleEnded,
	// A new enemy enters the formation: a summon, a merge's result or a replacement. Carries its id and group in
	// ids and groups and its formation position in amount.
	EnemyJoined,
	// An enemy leaves without being defeated (merged into another).
	EnemyLeft,
	// An enemy takes another form in place (ids holds the new id), at full hit points.
	EnemyTransformed,
};

// ActorUnable carries the cause in Event::status: a status that stops the actor, or "defeated".
enum class CancelReason { None, ActorUnable, Silenced, NotEnoughTp, NoUsesLeft, NoItem, NoTarget };

enum class Effectiveness { Normal, Weak, Resisted, Immune };

enum class StatusEnd { Cured, Duration, Recovered, WokeOnDamage, Defeated };

enum class ResourceKind { None, Tp, SkillUse, Item };

// Fields not relevant to a type keep their defaults.
struct Event {
	EventType type = EventType::RoundStarted;
	int round = 0;
	int actor = -1;
	int target = -1;
	// Retargeted: the target the command named.
	int previousTarget = -1;
	// Ability, item or combo ability id.
	std::string ability;
	// Combination recipe id.
	std::string combo;
	std::string status;
	// Status events and Message: a key into the host's string table.
	std::string message;
	std::vector<std::string> params;
	int amount = 0;
	// Hit points after the change.
	int hp = -1;
	// StatusApplied: rounds it lasts, 0 for no round limit.
	int duration = 0;
	CommandKind command = CommandKind::None;
	Element element = Element::None;
	Effectiveness effectiveness = Effectiveness::Normal;
	Stat stat = Stat::Strength;
	CancelReason cancel = CancelReason::None;
	StatusEnd statusEnd = StatusEnd::Cured;
	Outcome outcome = Outcome::Ongoing;
	ResourceKind resource = ResourceKind::None;
	Opening opening = Opening::Normal;
	bool succeeded = false;
	bool gameOver = false;
	int experience = 0;
	int currency = 0;
	// BattleEnded: the items the enemies dropped.
	Inventory items;
	// CommandsLocked: the macro that set the party's commands this round.
	std::string macro;
	std::vector<int> participants;
	// OrderResolved: combatant indexes in execution order.
	std::vector<int> order;
	// BattleStarted: definition id and group per combatant index.
	std::vector<std::string> ids;
	std::vector<std::string> groups;
	int partyCount = 0;
	BattleFlags flags;
	// CommandsLocked: every combatant's command for the round, by index.
	std::vector<RecordedCommand> commands;
};

// One line per event: the round, the type, then every field that differs from its default, in declaration order.
std::string serializeEvent(const Event& event);
std::string serializeEvents(const std::vector<Event>& events);

}
