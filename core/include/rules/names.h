#pragma once

#include "rules/battle.h"
#include "rules/command.h"
#include "rules/enemy.h"
#include "rules/event.h"
#include "rules/macro.h"
#include "rules/setup.h"
#include "rules/status.h"
#include "rules/types.h"

#include <string>

namespace rules {

// Each enum has a stable name per value, and parse functions accept exactly those names.
const char* toString(Element value);
const char* toString(Stat value);
const char* toString(Side value);
const char* toString(Targeting value);
const char* toString(AbilityKind value);
const char* toString(EffectKind value);
const char* toString(EquipSlot value);
const char* toString(Opening value);
const char* toString(RewardMultiplier value);
const char* toString(ExperienceSplit value);
const char* toString(StatusAction value);
const char* toString(ForcedTarget value);
const char* toString(StatusStacking value);
const char* toString(AiConditionKind value);
const char* toString(AiTargetRule value);
const char* toString(CommandKind value);
const char* toString(MacroTargetKind value);
const char* toString(MacroStepStatus value);
const char* toString(MacroIssue value);
const char* toString(ScriptedEndKind value);
const char* toString(AutoStop value);
const char* toString(EventType value);
const char* toString(Outcome value);
const char* toString(CancelReason value);
const char* toString(Effectiveness value);
const char* toString(StatusEnd value);
const char* toString(ResourceKind value);

bool parseElement(const std::string& text, Element& out);
bool parseStat(const std::string& text, Stat& out);
bool parseSide(const std::string& text, Side& out);
bool parseTargeting(const std::string& text, Targeting& out);
bool parseAbilityKind(const std::string& text, AbilityKind& out);
bool parseEffectKind(const std::string& text, EffectKind& out);
bool parseEquipSlot(const std::string& text, EquipSlot& out);
bool parseOpening(const std::string& text, Opening& out);
bool parseRewardMultiplier(const std::string& text, RewardMultiplier& out);
bool parseExperienceSplit(const std::string& text, ExperienceSplit& out);
bool parseStatusAction(const std::string& text, StatusAction& out);
bool parseForcedTarget(const std::string& text, ForcedTarget& out);
bool parseStatusStacking(const std::string& text, StatusStacking& out);
bool parseAiConditionKind(const std::string& text, AiConditionKind& out);
bool parseAiTargetRule(const std::string& text, AiTargetRule& out);
bool parseCommandKind(const std::string& text, CommandKind& out);
bool parseMacroTargetKind(const std::string& text, MacroTargetKind& out);
bool parseMacroStepStatus(const std::string& text, MacroStepStatus& out);
bool parseMacroIssue(const std::string& text, MacroIssue& out);
bool parseScriptedEndKind(const std::string& text, ScriptedEndKind& out);
bool parseAutoStop(const std::string& text, AutoStop& out);
bool parseEventType(const std::string& text, EventType& out);
bool parseOutcome(const std::string& text, Outcome& out);
bool parseCancelReason(const std::string& text, CancelReason& out);
bool parseEffectiveness(const std::string& text, Effectiveness& out);
bool parseStatusEnd(const std::string& text, StatusEnd& out);
bool parseResourceKind(const std::string& text, ResourceKind& out);

}
