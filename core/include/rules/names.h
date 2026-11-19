#pragma once

#include "rules/command.h"
#include "rules/enemy.h"
#include "rules/event.h"
#include "rules/macro.h"
#include "rules/types.h"

#include <string>

namespace rules {

const char* toString(Element value);
const char* toString(Stat value);
const char* toString(Targeting value);
const char* toString(AbilityKind value);
const char* toString(EffectKind value);
const char* toString(AiConditionKind value);
const char* toString(CommandKind value);
const char* toString(EquipSlot value);
const char* toString(Opening value);
const char* toString(RewardMultiplier value);
const char* toString(ExperienceSplit value);
const char* toString(MacroTargetKind value);
const char* toString(EventType value);
const char* toString(Outcome value);
const char* toString(FailReason value);
const char* toString(Effectiveness value);
const char* toString(StatusEnd value);
const char* toString(ResourceKind value);

bool parseElement(const std::string& text, Element& out);
bool parseStat(const std::string& text, Stat& out);
bool parseTargeting(const std::string& text, Targeting& out);
bool parseAbilityKind(const std::string& text, AbilityKind& out);
bool parseEffectKind(const std::string& text, EffectKind& out);
bool parseAiConditionKind(const std::string& text, AiConditionKind& out);
bool parseCommandKind(const std::string& text, CommandKind& out);
bool parseEquipSlot(const std::string& text, EquipSlot& out);
bool parseOpening(const std::string& text, Opening& out);
bool parseRewardMultiplier(const std::string& text, RewardMultiplier& out);
bool parseExperienceSplit(const std::string& text, ExperienceSplit& out);

}
