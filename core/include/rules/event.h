#pragma once

#include "rules/command.h"
#include "rules/types.h"

#include <string>
#include <vector>

namespace rules {

enum class Outcome { Ongoing, Victory, Defeat, Escaped };

enum class EventType {
	RoundStarted,
	ActionStarted,
	ActionFailed,
	ActionPrevented,
	ComboTriggered,
	ResourceSpent,
	ItemHadNoEffect,
	Missed,
	Critical,
	DamageDealt,
	Healed,
	Revived,
	StatusApplied,
	StatusResisted,
	StatusCured,
	StatusTicked,
	StatusExpired,
	StatModified,
	StatModifierExpired,
	CombatantDefeated,
	EscapeAttempted,
	RoundEnded,
	BattleEnded,
};

enum class FailReason { None, Silenced, NotEnoughTp, NoUsesLeft, NoItem, NoTarget };

enum class Effectiveness { Normal, Weak, Resisted, Immune };

enum class StatusEnd { Duration, Recovered, WokeOnDamage };

enum class ResourceKind { None, Tp, SkillUse, Item };

// Fields not relevant to a type keep their defaults.
struct Event {
	EventType type = EventType::RoundStarted;
	int round = 0;
	int actor = -1;
	int target = -1;
	// Ability, item or combo ability id.
	std::string ability;
	std::string status;
	int amount = 0;
	CommandKind command = CommandKind::None;
	Element element = Element::None;
	Effectiveness effectiveness = Effectiveness::Normal;
	Stat stat = Stat::Strength;
	FailReason fail = FailReason::None;
	StatusEnd statusEnd = StatusEnd::Duration;
	Outcome outcome = Outcome::Ongoing;
	ResourceKind resource = ResourceKind::None;
	bool succeeded = false;
	bool firstDiscovery = false;
	std::vector<int> participants;
};

// One line per event: the round, the type, then every field that differs from its default, in declaration order.
std::string serializeEvent(const Event& event);
std::string serializeEvents(const std::vector<Event>& events);

}
