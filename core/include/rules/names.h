#pragma once

#include "rules/command.h"
#include "rules/enemy.h"
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

bool parseElement(const std::string& text, Element& out);
bool parseStat(const std::string& text, Stat& out);
bool parseTargeting(const std::string& text, Targeting& out);
bool parseAbilityKind(const std::string& text, AbilityKind& out);
bool parseEffectKind(const std::string& text, EffectKind& out);
bool parseAiConditionKind(const std::string& text, AiConditionKind& out);
bool parseCommandKind(const std::string& text, CommandKind& out);

}
