#pragma once

#include "rules/command.h"
#include "rules/message.h"
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

// What repeating a macro round after round does in a round where trouble holds (macroTrouble). Default: the path's
// rules.json default (auto_macro_on_trouble for the save's auto macro, named_macro_on_trouble for a named one).
// Continue: each step that cannot run takes core's automatic choice, the macro retried in full every round. Recover: the
// members recoverWho names take core's automatic choice for that round only. Stop: core's auto-battle stop rules.
enum class MacroTrouble { Default, Continue, Recover, Stop };
// Who takes core's automatic choice in a recover round. Uncovered: members the macro has no step for. Troubled: only the
// members whose step failed, which is what Continue does.
enum class MacroRecoverWho { Default, All, Uncovered, Troubled };
// What a member the macro has no step for does in a round the macro fills: a basic attack, nothing, or core's choice.
enum class MacroUnnamed { Default, Attack, None, Auto };

struct Macro {
	std::string name;
	std::vector<MacroStep> steps;
	MacroTrouble onTrouble = MacroTrouble::Default;
	MacroRecoverWho recoverWho = MacroRecoverWho::Default;
	MacroUnnamed unnamed = MacroUnnamed::Default;
};

// Applied with an issue (Silenced, NotEnoughTp, NoUsesLeft, NoItem, NoTarget) means the command is set as
// written but cannot run now; it is cancelled at execution unless that changes first. Automatic: the step could not
// run as written and the member takes autoCommand's choice instead (MacroFallback::Automatic). LostTurn: the step
// could not run as written and the member does nothing this round (MacroFallback::LoseTurn).
enum class MacroStepStatus { Applied, Retargeted, FellBack, Skipped, Automatic, LostTurn };

// What applyMacro does with a step that cannot run as written. BasicAttack: a command the battle would refuse becomes
// a basic attack, and one that cannot be paid for or has no target is kept, to be cancelled at execution. Automatic:
// each of those takes core's automatic choice for the member this round (autoCommand). Attack: each of those becomes a
// basic attack on the intended foe or the first living one. LoseTurn: the member does nothing this round and pays nothing.
enum class MacroFallback { BasicAttack, Automatic, Attack, LoseTurn };

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

// Whether a step result means the step could not be carried out as written this round: its member is defeated, cannot act
// or is forced by a status, or the step fell back, took the automatic choice, lost the turn, cannot be paid for or has no
// target. A step for a member outside the battle (a vehicle's step in a party battle, members aboard), a side caught off
// guard and a retarget are not trouble.
bool stepTroubled(const MacroStepResult& result);

// Trouble in a round a macro fills (Battle::macroTrouble): a party combatant taking part is defeated, or a step is troubled.
struct MacroTroubleReport {
	bool holds = false;
	// Party combatant indexes, in index order.
	std::vector<int> fallen;
	// Indexes into the macro's steps.
	std::vector<int> troubledSteps;
	// The combatants whose steps are troubled, in step order.
	std::vector<int> troubledMembers;
	// Set by Battle::applyMacroRound: the trouble mode the round played and the members that took core's automatic choice.
	MacroTrouble mode = MacroTrouble::Default;
	std::vector<int> recovered;
};

// One step's state for a host marking a macro before the player confirms it (Battle::checkMacro).
struct MacroStepCheck {
	std::string member;
	int actor = -1;
	// None, or why the step cannot run as written; TargetGone means it runs on another target.
	MacroIssue issue = MacroIssue::None;
	// The status behind CannotAct or ForcedByStatus.
	std::string status;
	// What setting the step's command by hand would be refused with, or empty.
	Message refusal;
	// The step runs as written this round: no issue but TargetGone and no refusal.
	bool ok = true;
};

// Whether a step's target rule fits its command; `targeting` is the ability's or item's. Default always fits.
bool macroTargetFits(MacroTargetKind target, CommandKind command, Targeting targeting);

// A versioned line-based text form for saves.
std::string serializeMacros(const std::vector<Macro>& macros);
Result<std::vector<Macro>> parseMacros(const std::string& text);

}
