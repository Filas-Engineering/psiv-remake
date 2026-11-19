#pragma once

#include "rules/command.h"
#include "rules/result.h"

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

// Applied with an issue (Silenced, NotEnoughTp, NoUsesLeft, NoItem, NoTarget) means the command is set as
// written but cannot run now; it is cancelled at execution unless that changes first.
enum class MacroStepStatus { Applied, Retargeted, FellBack, Skipped };

enum class MacroIssue {
	None,
	NotInParty,
	ActorDefeated,
	CaughtOffGuard,
	CannotAct,
	ForcedByStatus,
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

// True for the issues that mean the step's cost cannot be paid now.
bool isCostIssue(MacroIssue issue);

// A versioned line-based text form for saves.
std::string serializeMacros(const std::vector<Macro>& macros);
Result<std::vector<Macro>> parseMacros(const std::string& text);

}
