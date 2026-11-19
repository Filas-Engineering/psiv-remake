#pragma once

#include "rules/command.h"
#include "rules/types.h"

#include <string>
#include <vector>

namespace rules {

enum class Outcome { Ongoing, Victory, Defeat, Fled };

enum class EventType {
	RoundStarted,
	ActionStarted,
	ActionFailed,
	ActionPrevented,
	ComboTriggered,
	Missed,
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
	FleeAttempted,
	RoundEnded,
	BattleEnded,
};

enum class FailReason { None, Silenced, NotEnoughTp, NoUsesLeft, NoItem, NoTarget };

enum class Effectiveness { Normal, Weak, Resisted, Immune };

enum class StatusEnd { Duration, Recovered, WokeOnDamage };

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
	bool succeeded = false;
	bool firstDiscovery = false;
	std::vector<int> participants;
};

}
