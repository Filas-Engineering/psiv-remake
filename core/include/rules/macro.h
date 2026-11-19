#pragma once

#include "rules/command.h"

#include <string>
#include <vector>

namespace rules {

struct MacroStep {
	std::string member;
	CommandKind kind = CommandKind::Attack;
	std::string id;
	// Slot in the enemy lineup, for enemy-targeted steps.
	int enemySlot = -1;
	// Character id, for ally-targeted steps.
	std::string allyMember;
};

struct Macro {
	std::string name;
	std::vector<MacroStep> steps;
};

enum class MacroStepStatus { Applied, Retargeted, FellBack, Skipped };

enum class MacroIssue { None, NotInParty, ActorDefeated, Silenced, NotEnoughTp, NoUsesLeft, NoItem, TargetGone, NoTarget };

struct MacroStepResult {
	std::string member;
	int actor = -1;
	MacroStepStatus status = MacroStepStatus::Applied;
	MacroIssue issue = MacroIssue::None;
	Command command;
};

}
