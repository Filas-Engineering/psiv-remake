#pragma once

#include "rules/command.h"

#include <string>
#include <vector>

namespace rules {

// Macro targets are rules, so a macro still means the same thing in a battle with another formation.
enum class MacroTargetKind { Default, Self, Member, Enemy, All };

struct MacroTarget {
	MacroTargetKind kind = MacroTargetKind::Default;
	// Character id, for Member.
	std::string member;
	// Formation position from 0, for Enemy.
	int enemySlot = -1;
};

struct MacroStep {
	std::string member;
	CommandKind kind = CommandKind::Attack;
	std::string id;
	MacroTarget target;
};

struct Macro {
	std::string name;
	std::vector<MacroStep> steps;
};

enum class MacroStepStatus { Applied, Retargeted, FellBack, Skipped };

enum class MacroIssue {
	None,
	NotInParty,
	ActorDefeated,
	CaughtOffGuard,
	UnknownAbility,
	Silenced,
	NotEnoughTp,
	NoUsesLeft,
	NoItem,
	EscapeNotAllowed,
	TargetGone,
	NoTarget,
};

struct MacroStepResult {
	std::string member;
	int actor = -1;
	MacroStepStatus status = MacroStepStatus::Applied;
	MacroIssue issue = MacroIssue::None;
	Command command;
};

}
