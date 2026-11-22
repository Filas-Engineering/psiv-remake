#include "rules/names.h"

#include "rules/data_keys.h"

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
const char* const kStats[] = {"strength", "mental", "agility", "dexterity", "defense", "magic_defense", "attack"};
const char* const kSides[] = {"party", "enemy"};
const char* const kTargetings[] = {"one_enemy", "enemy_group", "all_enemies", "one_ally", "all_allies", "self", "one_fallen_ally", "random_enemy"};
const char* const kAbilityKinds[] = {"attack", "technique", "skill", "combo"};
const char* const kEffectKinds[] = {"physical_damage", "elemental_damage", "heal", "apply_status", "cure_status", "modify_stat", "revive", "travel", "instant_defeat", "restore_tp", "strip_modifiers", "summon", "merge", "teach", "transform", "split", "self_destruct", "remove_beside", "set_hp_percent", "grant_affinity", "reset_stat"};
const char* const kCreatureKinds[] = {"living", "spirit", "machine"};
const char* const kTravelTargets[] = {"town", "dungeon_exit"};
const char* const kEquipSlots[] = {"weapon", "head", "body", "arms", "accessory", "shield", "left_hand"};
const char* const kOpenings[] = {"normal", "party_first", "enemy_first"};
const char* const kRewardMultipliers[] = {"x1", "x1_5", "x2"};
const char* const kRewardMultipliersBefore[] = {"1", "1.5", "2"};
const char* const kStatusActions[] = {"normal", "cannot_act", "forced"};
const char* const kForcedTargets[] = {"random_any", "random_ally", "random_foe"};
const char* const kStatusStackings[] = {"refresh", "extend", "keep"};
const char* const kAiConditions[] = {
	"self_hp_half", "self_hp_quarter_once", "any_ally_hp_half", "last_standing", "hit_by_attack", "hit_by_ability", "hit_by_technique",
	"techniques_blocked", "hit_by_group", "ambushed", "ally_present", "ally_absent", "free_slot_beside", "group_intact", "after_turns", "all", "any", "first_turn"};
const char* const kAiTargetRules[] = {"default", "random", "lowest_hp", "highest_hp", "has_status", "lacks_status", "last_attacker"};
const char* const kCommandKinds[] = {"none", "attack", "ability", "item", "defend", "escape"};
const char* const kMacroTargets[] = {"default", "self", "member", "enemy", "all"};
const char* const kMacroStepStatuses[] = {"applied", "retargeted", "fell_back", "skipped", "automatic", "lost_turn"};
const char* const kMacroTroubles[] = {"default", "continue", "recover", "stop"};
const char* const kMacroRecoverWho[] = {"default", "all", "uncovered", "troubled"};
const char* const kMacroUnnamed[] = {"default", "attack", "none", "auto"};
// The choices data may name: everything but "default".
const char* const kMacroTroubleChoices[] = {"continue", "recover", "stop"};
const char* const kMacroRecoverWhoChoices[] = {"all", "uncovered", "troubled"};
const char* const kMacroUnnamedChoices[] = {"attack", "none", "auto"};
const char* const kMacroFallbacks[] = {"basic_attack", "automatic", "attack", "lose_turn"};
const char* const kMacroIssues[] = {
	"none", "not_in_party", "actor_defeated", "caught_off_guard", "cannot_act", "forced_by_status", "unknown_ability",
	"silenced", "not_enough_tp", "no_uses_left", "no_item", "escape_not_allowed", "target_gone", "no_target"};
const char* const kScriptedEndKinds[] = {"none", "survive_rounds", "enemy_defeated"};
const char* const kAutoStops[] = {"none", "cancelled", "member_fell", "cannot_pay", "battle_ended"};
const char* const kEventTypes[] = {
	"battle_started", "round_started", "commands_locked", "order_resolved", "retargeted", "action_started",
	"action_cancelled", "combo_triggered", "combo_discovered", "resource_spent", "item_had_no_effect", "hit", "missed",
	"critical", "damage_dealt", "healed", "revived", "status_applied", "status_resisted", "status_removed",
	"stat_modified", "combatant_defeated", "escape_attempted", "message", "round_ended", "battle_ended", "enemy_joined", "enemy_left", "enemy_transformed"};
// The spellings record and event text files used before the names were unified.
const char* const kEventTypesBefore[] = {
	"BattleStarted", "RoundStarted", "CommandsLocked", "OrderResolved", "Retargeted", "ActionStarted", "ActionCancelled",
	"ComboTriggered", "ComboDiscovered", "ResourceSpent", "ItemHadNoEffect", "Hit", "Missed", "Critical", "DamageDealt",
	"Healed", "Revived", "StatusApplied", "StatusResisted", "StatusRemoved", "StatModified", "CombatantDefeated",
	"EscapeAttempted", "Message", "RoundEnded", "BattleEnded"};
const char* const kOutcomes[] = {"ongoing", "victory", "defeat", "escaped", "scripted_end"};
const char* const kCancelReasons[] = {"none", "actor_unable", "silenced", "not_enough_tp", "no_uses_left", "no_item", "no_target"};
const char* const kEffectiveness[] = {"normal", "weak", "resisted", "immune"};
const char* const kStatusEnds[] = {"cured", "duration", "recovered", "woke_on_damage", "defeated"};
const char* const kResources[] = {"none", "tp", "skill_use", "item"};

}

std::string toString(const Element& value) { return value.id.empty() ? "none" : value.id; }
const char* toString(Stat value) { return nameOf(kStats, value); }
const char* toString(Side value) { return nameOf(kSides, value); }
const char* toString(Targeting value) { return nameOf(kTargetings, value); }
const char* toString(AbilityKind value) { return nameOf(kAbilityKinds, value); }
const char* toString(EffectKind value) { return nameOf(kEffectKinds, value); }
const char* toString(CreatureKind value) { return nameOf(kCreatureKinds, value); }
const char* toString(TravelTarget value) { return nameOf(kTravelTargets, value); }
const char* toString(EquipSlot value) { return nameOf(kEquipSlots, value); }
const char* toString(Opening value) { return nameOf(kOpenings, value); }
const char* toString(RewardMultiplier value) { return nameOf(kRewardMultipliers, value); }
const char* toString(StatusAction value) { return nameOf(kStatusActions, value); }
const char* toString(ForcedTarget value) { return nameOf(kForcedTargets, value); }
const char* toString(StatusStacking value) { return nameOf(kStatusStackings, value); }
const char* toString(AiConditionKind value) { return nameOf(kAiConditions, value); }
const char* toString(AiTargetRule value) { return nameOf(kAiTargetRules, value); }
const char* toString(CommandKind value) { return nameOf(kCommandKinds, value); }
const char* toString(MacroTargetKind value) { return nameOf(kMacroTargets, value); }
const char* toString(MacroStepStatus value) { return nameOf(kMacroStepStatuses, value); }
const char* toString(MacroIssue value) { return nameOf(kMacroIssues, value); }
const char* toString(MacroTrouble value) { return nameOf(kMacroTroubles, value); }
const char* toString(MacroRecoverWho value) { return nameOf(kMacroRecoverWho, value); }
const char* toString(MacroUnnamed value) { return nameOf(kMacroUnnamed, value); }
const char* toString(MacroFallback value) { return nameOf(kMacroFallbacks, value); }
const char* toString(ScriptedEndKind value) { return nameOf(kScriptedEndKinds, value); }
const char* toString(AutoStop value) { return nameOf(kAutoStops, value); }
const char* toString(EventType value) { return nameOf(kEventTypes, value); }
const char* toString(Outcome value) { return nameOf(kOutcomes, value); }
const char* toString(CancelReason value) { return nameOf(kCancelReasons, value); }
const char* toString(Effectiveness value) { return nameOf(kEffectiveness, value); }
const char* toString(StatusEnd value) { return nameOf(kStatusEnds, value); }
const char* toString(ResourceKind value) { return nameOf(kResources, value); }

bool parseElement(const std::string& text, Element& out) {
	if (text == "none") {
		out = Element::None;
		return true;
	}
	for (const char* builtIn : kBuiltInElements) {
		if (text == builtIn) {
			out = Element(text);
			return true;
		}
	}
	return false;
}
bool parseStat(const std::string& text, Stat& out) { return parseName(kStats, text, out); }
bool parseSide(const std::string& text, Side& out) { return parseName(kSides, text, out); }
bool parseTargeting(const std::string& text, Targeting& out) { return parseName(kTargetings, text, out); }
bool parseAbilityKind(const std::string& text, AbilityKind& out) { return parseName(kAbilityKinds, text, out); }
bool parseEffectKind(const std::string& text, EffectKind& out) { return parseName(kEffectKinds, text, out); }
bool parseCreatureKind(const std::string& text, CreatureKind& out) { return parseName(kCreatureKinds, text, out); }
bool parseTravelTarget(const std::string& text, TravelTarget& out) { return parseName(kTravelTargets, text, out); }
bool parseEquipSlot(const std::string& text, EquipSlot& out) { return parseName(kEquipSlots, text, out); }
bool parseOpening(const std::string& text, Opening& out) { return parseName(kOpenings, text, out); }
bool parseRewardMultiplier(const std::string& text, RewardMultiplier& out) {
	return parseName(kRewardMultipliers, text, out) || parseName(kRewardMultipliersBefore, text, out);
}
bool parseStatusAction(const std::string& text, StatusAction& out) { return parseName(kStatusActions, text, out); }
bool parseForcedTarget(const std::string& text, ForcedTarget& out) { return parseName(kForcedTargets, text, out); }
bool parseStatusStacking(const std::string& text, StatusStacking& out) { return parseName(kStatusStackings, text, out); }
bool parseAiConditionKind(const std::string& text, AiConditionKind& out) { return parseName(kAiConditions, text, out); }
bool parseAiTargetRule(const std::string& text, AiTargetRule& out) { return parseName(kAiTargetRules, text, out); }
bool parseCommandKind(const std::string& text, CommandKind& out) { return parseName(kCommandKinds, text, out); }
bool parseMacroTargetKind(const std::string& text, MacroTargetKind& out) { return parseName(kMacroTargets, text, out); }
bool parseMacroStepStatus(const std::string& text, MacroStepStatus& out) { return parseName(kMacroStepStatuses, text, out); }
bool parseMacroIssue(const std::string& text, MacroIssue& out) { return parseName(kMacroIssues, text, out); }
bool parseMacroTrouble(const std::string& text, MacroTrouble& out) { return parseName(kMacroTroubles, text, out); }
bool parseMacroRecoverWho(const std::string& text, MacroRecoverWho& out) { return parseName(kMacroRecoverWho, text, out); }
bool parseMacroUnnamed(const std::string& text, MacroUnnamed& out) { return parseName(kMacroUnnamed, text, out); }
bool parseMacroFallback(const std::string& text, MacroFallback& out) { return parseName(kMacroFallbacks, text, out); }
bool parseScriptedEndKind(const std::string& text, ScriptedEndKind& out) { return parseName(kScriptedEndKinds, text, out); }
bool parseAutoStop(const std::string& text, AutoStop& out) { return parseName(kAutoStops, text, out); }
bool parseEventType(const std::string& text, EventType& out) {
	return parseName(kEventTypes, text, out) || parseName(kEventTypesBefore, text, out);
}
bool parseOutcome(const std::string& text, Outcome& out) { return parseName(kOutcomes, text, out); }
bool parseCancelReason(const std::string& text, CancelReason& out) { return parseName(kCancelReasons, text, out); }
bool parseEffectiveness(const std::string& text, Effectiveness& out) { return parseName(kEffectiveness, text, out); }
bool parseStatusEnd(const std::string& text, StatusEnd& out) { return parseName(kStatusEnds, text, out); }
bool parseResourceKind(const std::string& text, ResourceKind& out) { return parseName(kResources, text, out); }

template <std::size_t N>
DataEnum enumOf(const char* name, const char* const (&names)[N]) {
	return DataEnum{name, std::vector<std::string>(names, names + N)};
}

const std::vector<DataEnum>& coreDataEnums() {
	static const std::vector<DataEnum> enums = {
		enumOf("ability_kind", kAbilityKinds),
		enumOf("ai_condition", kAiConditions),
		enumOf("ai_target_rule", kAiTargetRules),
		enumOf("creature_kind", kCreatureKinds),
		enumOf("effect_type", kEffectKinds),
		enumOf("equip_slot", kEquipSlots),
		enumOf("forced_target", kForcedTargets),
		enumOf("macro_command", kCommandKinds),
		enumOf("macro_on_trouble", kMacroTroubleChoices),
		enumOf("macro_recover_who", kMacroRecoverWhoChoices),
		enumOf("macro_unnamed", kMacroUnnamedChoices),
		enumOf("macro_target", kMacroTargets),
		enumOf("opening", kOpenings),
		enumOf("stat", kStats),
		enumOf("status_action", kStatusActions),
		enumOf("status_stacking", kStatusStackings),
		enumOf("targeting", kTargetings),
		enumOf("travel_target", kTravelTargets),
	};
	return enums;
}

}
