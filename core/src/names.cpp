#include "rules/names.h"

namespace rules {

namespace {

template <class E, std::size_t N>
const char* nameOf(const char* const (&names)[N], E value) {
	const std::size_t index = static_cast<std::size_t>(value);
	return index < N ? names[index] : "unknown";
}

template <class E, std::size_t N>
bool parseName(const char* const (&names)[N], const std::string& text, E& out) {
	for (std::size_t i = 0; i < N; ++i) {
		if (text == names[i]) {
			out = static_cast<E>(i);
			return true;
		}
	}
	return false;
}

// Order matches the enum declarations.
const char* const kElements[] = {"none", "heat", "chill", "shock", "gale"};
const char* const kStats[] = {"strength", "mental", "agility", "dexterity", "defense"};
const char* const kTargetings[] = {"one_enemy", "enemy_group", "all_enemies", "one_ally", "all_allies", "self"};
const char* const kAbilityKinds[] = {"attack", "technique", "skill", "combo"};
const char* const kEffectKinds[] = {"physical_damage", "elemental_damage", "heal", "apply_status", "cure_status", "modify_stat", "revive"};
const char* const kAiConditions[] = {"always", "self_hp_below", "ally_hp_below", "ally_defeated", "self_has_status", "foe_has_status"};
const char* const kCommandKinds[] = {"none", "attack", "ability", "item", "defend", "flee"};

}

const char* toString(Element value) { return nameOf(kElements, value); }
const char* toString(Stat value) { return nameOf(kStats, value); }
const char* toString(Targeting value) { return nameOf(kTargetings, value); }
const char* toString(AbilityKind value) { return nameOf(kAbilityKinds, value); }
const char* toString(EffectKind value) { return nameOf(kEffectKinds, value); }
const char* toString(AiConditionKind value) { return nameOf(kAiConditions, value); }
const char* toString(CommandKind value) { return nameOf(kCommandKinds, value); }

bool parseElement(const std::string& text, Element& out) { return parseName(kElements, text, out); }
bool parseStat(const std::string& text, Stat& out) { return parseName(kStats, text, out); }
bool parseTargeting(const std::string& text, Targeting& out) { return parseName(kTargetings, text, out); }
bool parseAbilityKind(const std::string& text, AbilityKind& out) { return parseName(kAbilityKinds, text, out); }
bool parseEffectKind(const std::string& text, EffectKind& out) { return parseName(kEffectKinds, text, out); }
bool parseAiConditionKind(const std::string& text, AiConditionKind& out) { return parseName(kAiConditions, text, out); }
bool parseCommandKind(const std::string& text, CommandKind& out) { return parseName(kCommandKinds, text, out); }

}
