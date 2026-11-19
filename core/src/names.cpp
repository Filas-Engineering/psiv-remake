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
const char* const kCommandKinds[] = {"none", "attack", "ability", "item", "defend", "escape"};
const char* const kEquipSlots[] = {"weapon", "head", "body", "arms", "accessory"};
const char* const kOpenings[] = {"normal", "party_first", "enemy_first"};
const char* const kRewardMultipliers[] = {"1", "1.5", "2"};
const char* const kExperienceSplits[] = {"each_living", "split_living", "each_member"};
const char* const kMacroTargets[] = {"default", "self", "member", "enemy", "all"};
const char* const kEventTypes[] = {
	"RoundStarted", "ActionStarted", "ActionFailed", "ActionPrevented", "ComboTriggered", "ResourceSpent",
	"ItemHadNoEffect", "Missed", "Critical", "DamageDealt", "Healed", "Revived", "StatusApplied", "StatusResisted",
	"StatusCured", "StatusTicked", "StatusExpired", "StatModified", "StatModifierExpired", "CombatantDefeated",
	"EscapeAttempted", "RoundEnded", "BattleEnded"};
const char* const kOutcomes[] = {"ongoing", "victory", "defeat", "escaped"};
const char* const kFailReasons[] = {"none", "silenced", "not_enough_tp", "no_uses_left", "no_item", "no_target"};
const char* const kEffectiveness[] = {"normal", "weak", "resisted", "immune"};
const char* const kStatusEnds[] = {"duration", "recovered", "woke_on_damage"};
const char* const kResources[] = {"none", "tp", "skill_use", "item"};

}

const char* toString(Element value) { return nameOf(kElements, value); }
const char* toString(Stat value) { return nameOf(kStats, value); }
const char* toString(Targeting value) { return nameOf(kTargetings, value); }
const char* toString(AbilityKind value) { return nameOf(kAbilityKinds, value); }
const char* toString(EffectKind value) { return nameOf(kEffectKinds, value); }
const char* toString(AiConditionKind value) { return nameOf(kAiConditions, value); }
const char* toString(CommandKind value) { return nameOf(kCommandKinds, value); }
const char* toString(EquipSlot value) { return nameOf(kEquipSlots, value); }
const char* toString(Opening value) { return nameOf(kOpenings, value); }
const char* toString(RewardMultiplier value) { return nameOf(kRewardMultipliers, value); }
const char* toString(ExperienceSplit value) { return nameOf(kExperienceSplits, value); }
const char* toString(MacroTargetKind value) { return nameOf(kMacroTargets, value); }
const char* toString(EventType value) { return nameOf(kEventTypes, value); }
const char* toString(Outcome value) { return nameOf(kOutcomes, value); }
const char* toString(FailReason value) { return nameOf(kFailReasons, value); }
const char* toString(Effectiveness value) { return nameOf(kEffectiveness, value); }
const char* toString(StatusEnd value) { return nameOf(kStatusEnds, value); }
const char* toString(ResourceKind value) { return nameOf(kResources, value); }

bool parseElement(const std::string& text, Element& out) { return parseName(kElements, text, out); }
bool parseStat(const std::string& text, Stat& out) { return parseName(kStats, text, out); }
bool parseTargeting(const std::string& text, Targeting& out) { return parseName(kTargetings, text, out); }
bool parseAbilityKind(const std::string& text, AbilityKind& out) { return parseName(kAbilityKinds, text, out); }
bool parseEffectKind(const std::string& text, EffectKind& out) { return parseName(kEffectKinds, text, out); }
bool parseAiConditionKind(const std::string& text, AiConditionKind& out) { return parseName(kAiConditions, text, out); }
bool parseCommandKind(const std::string& text, CommandKind& out) { return parseName(kCommandKinds, text, out); }
bool parseEquipSlot(const std::string& text, EquipSlot& out) { return parseName(kEquipSlots, text, out); }
bool parseOpening(const std::string& text, Opening& out) { return parseName(kOpenings, text, out); }
bool parseRewardMultiplier(const std::string& text, RewardMultiplier& out) { return parseName(kRewardMultipliers, text, out); }
bool parseExperienceSplit(const std::string& text, ExperienceSplit& out) { return parseName(kExperienceSplits, text, out); }

}
