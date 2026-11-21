#pragma once

#include "rules/command.h"
#include "rules/result.h"
#include "rules/types.h"

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
	// Item steps: `id` is an equipment piece with a battle use, worn by the member or cast from the bag, not an item.
	bool piece = false;
};

struct Macro {
	std::string name;
	std::vector<MacroStep> steps;
};

// Applied with an issue (Silenced, NotEnoughTp, NoUsesLeft, NoItem, NoTarget) means the command is set as
// written but cannot run now; it is cancelled at execution unless that changes first. Automatic: the step could not
// run as written and the member takes autoCommand's choice instead (MacroFallback::Automatic).
enum class MacroStepStatus { Applied, Retargeted, FellBack, Skipped, Automatic };

// What applyMacro does with a step that cannot run as written. BasicAttack: a command the battle would refuse becomes
// a basic attack, and one that cannot be paid for or has no target is kept, to be cancelled at execution. Automatic:
// each of those takes core's automatic choice for the member this round (autoCommand).
enum class MacroFallback { BasicAttack, Automatic };

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

// Whether a step's target rule fits its command; `targeting` is the ability's or item's. Default always fits.
bool macroTargetFits(MacroTargetKind target, CommandKind command, Targeting targeting);

// A versioned line-based text form for saves.
std::string serializeMacros(const std::vector<Macro>& macros);
Result<std::vector<Macro>> parseMacros(const std::string& text);

}
