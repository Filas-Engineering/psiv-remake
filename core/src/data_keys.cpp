#include "rules/data_keys.h"

#include <cstdlib>

namespace rules {

const std::vector<DataKeySet>& coreDataKeys() {
	static const std::vector<DataKeySet> sets = {
		DataKeySet("abilities.json", "", {"abilities"}),
		DataKeySet("abilities.json", "abilities[]", {"id", "name", "kind", "cost", "uses", "target", "effects", "field", "family", "attack_stat", "variance", "auto"}),
		DataKeySet("combos.json", "", {"combos", "enemy_combos"}),
		DataKeySet("combos.json", "combos[]", {"id", "abilities", "ordered", "result", "orders"}),
		DataKeySet("combos.json", "combos[].abilities[]", {"ability", "character"}),
		DataKeySet("combos.json", "enemy_combos[]", {"id", "parts", "result", "ordered"}),
		DataKeySet("combos.json", "enemy_combos[].parts[]", {"enemy", "ability"}),
		DataKeySet("difficulties.json", "", {"difficulties"}),
		DataKeySet("difficulties.json", "difficulties[]", {"id", "name", "reward_percent", "abilities", "enemies"}),
		DataKeySet("difficulties.json", "difficulties[].abilities[]", {"id", "power_percent", "cost", "uses"}),
		DataKeySet("difficulties.json", "difficulties[].enemies[]", {"id", "stats_percent", "add_abilities", "slots", "overrides", "status_resist"}),
		DataKeySet("difficulties.json", "difficulties[].enemies[].stats_percent", {"hp", "tp", "strength", "mental", "agility", "dexterity", "defense"}),
		DataKeySet("enemies.json", "", {"enemies", "encounters"}),
		DataKeySet("enemies.json", "encounters[]", {"id", "enemies", "escape", "awareness", "escape_rating", "drop", "replace_fallen"}),
		DataKeySet("enemies.json", "encounters[].drop", {"item", "equipment", "rate"}),
		DataKeySet("enemies.json", "encounters[].enemies[]", {"enemy", "group"}),
		DataKeySet("enemies.json", "enemies[]", {"id", "name", "stats", "basic_attack", "abilities", "affinities", "status_resist", "slots", "overrides", "experience", "currency", "kind", "vulnerable", "statuses", "summons", "merges_into", "next_form", "flying", "next_form_at", "merges_with", "transforms_into", "splits_into", "attack_element", "keeps_ambush"}),
		DataKeySet("enemies.json", "enemies[].overrides[] condition part", {"when", "all", "any", "enemy", "turns", "count", "compare"}),
		DataKeySet("enemies.json", "enemies[].overrides[]", {"when", "all", "any", "ability", "target", "target_status", "enemy", "turns", "count", "compare"}),
		DataKeySet("equipment.json", "", {"equipment"}),
		DataKeySet("equipment.json", "equipment[]", {"id", "name", "slot", "category", "stats", "element", "attack", "abilities", "status_resist", "use", "hands", "use_from_bag", "affinities", "use_power"}),
		DataKeySet("equipment.json", "equipment[].affinities set", {"set"}),
		DataKeySet("items.json", "", {"items"}),
		DataKeySet("items.json", "items[]", {"id", "name", "target", "effects", "field", "consumed", "consume_when_no_effect"}),
		DataKeySet("party.json", "", {"characters", "vehicles", "lineup", "inventory", "equipment_bag", "macros"}),
		DataKeySet("party.json", "characters[]", {"id", "name", "base", "growth", "basic_attack", "abilities", "learns", "affinities", "status_resist", "equip", "equipment", "kind", "vulnerable", "teachable", "experience_table", "level_stats", "profile", "skill_uses"}),
		DataKeySet("party.json", "characters[].learns[]", {"level", "ability"}),
		DataKeySet("party.json", "characters[].profile[]", {"key", "value"}),
		DataKeySet("party.json", "macros[]", {"name", "steps", "on_trouble", "recover_who", "unnamed", "policy"}),
		DataKeySet("party.json", "macros[].steps[]", {"member", "command", "id", "target", "piece"}),
		DataKeySet("party.json", "macros[].steps[].target", {"enemy", "member"}),
		DataKeySet("party.json", "vehicles[]", {"id", "name", "stats", "basic_attack", "abilities", "affinities", "status_resist"}),
		DataKeySet("rules.json", "", {"surprise_rounds", "rest_revives", "machine_recover_hp", "affinity_divisor", "magic_defense", "instant_defeat_kinds", "ambush_opening", "elements", "neutral_affinity", "defend_affinity", "defend_elemental", "defend_elements", "damage_variance", "defend_timing", "immune_min_damage", "critical_bonus", "damage_wraparound", "escape_party_first_round", "target_weights", "stat_wraparound", "macro_ally_pick", "auto_policy", "effectiveness_baseline", "element_baselines", "field_poison_every", "field_poison_status", "field_paralysis", "auto_macro_on_trouble", "named_macro_on_trouble", "macro_recover_who", "macro_unnamed", "consume_when_no_effect"}),
		DataKeySet("rules.json", "field_paralysis", {"status", "from", "chance", "by", "everywhere"}),
		DataKeySet("rules.json", "elements[]", {"id", "normal"}),
		DataKeySet("shared", "effects[] apply_status", {"type", "kinds", "status", "threshold", "user_stat", "target_stat", "user_power", "element", "always_lands"}),
		DataKeySet("shared", "effects[] cure_status", {"type", "kinds", "status"}),
		DataKeySet("shared", "effects[] elemental_damage", {"type", "kinds", "power", "element", "stat_percent", "stat_permille", "stat_value"}),
		DataKeySet("shared", "effects[] heal", {"type", "kinds", "power", "stat_value"}),
		DataKeySet("shared", "effects[] instant_defeat", {"type", "kinds", "threshold", "element", "user_stat", "target_stat", "user_power", "always_lands"}),
		DataKeySet("shared", "effects[] merge", {"type"}),
		DataKeySet("shared", "effects[] modify_stat", {"type", "kinds", "stat", "direction", "threshold", "power", "user_stat", "target_stat", "user_power", "element", "always_lands"}),
		DataKeySet("shared", "effects[] physical_damage", {"type", "kinds", "power", "element", "stat_percent", "stat_permille", "stat_value"}),
		DataKeySet("shared", "effects[] remove_beside", {"type", "rewards"}),
		DataKeySet("shared", "effects[] reset_stat", {"type", "kinds", "stat"}),
		DataKeySet("shared", "effects[] set_hp_percent", {"type", "kinds", "power"}),
		DataKeySet("shared", "effects[] grant_affinity", {"type", "kinds", "element", "affinity", "permanent"}),
		DataKeySet("shared", "effects[] restore_tp", {"type", "kinds", "power"}),
		DataKeySet("shared", "effects[] revive", {"type", "kinds", "power", "living_target", "hp"}),
		DataKeySet("shared", "effects[] self_destruct", {"type", "rewards"}),
		DataKeySet("shared", "effects[] split", {"type", "count"}),
		DataKeySet("shared", "effects[] strip_modifiers", {"type", "kinds", "equipment_affinities"}),
		DataKeySet("shared", "effects[] summon", {"type"}),
		DataKeySet("shared", "effects[] teach", {"type", "ability"}),
		DataKeySet("shared", "effects[] transform", {"type"}),
		DataKeySet("shared", "effects[] travel", {"type", "kinds", "to"}),
		DataKeySet("shared", "stats", {"hp", "tp", "strength", "mental", "agility", "dexterity", "defense", "level", "critical", "evasion", "magic_defense", "attack"}),
		DataKeySet("shared", "variance", {"percent", "distribution"}),
		DataKeySet("statuses.json", "", {"statuses"}),
		DataKeySet("statuses.json", "statuses[]", {"id", "name", "action", "forced_target", "blocks_techniques", "wakes_on_damage", "stats_to_one", "removes", "duration", "enemy_duration", "recover_chance", "stacking", "persists_outside_battle", "counts_toward_defeat", "survives_defeat", "rest_cures", "damage_cap"}),

	};
	return sets;
}

const DataKeySet& dataKeys(const std::string& file, const std::string& object) {
	for (const DataKeySet& set : coreDataKeys()) {
		if (set.file == file && set.object == object) {
			return set;
		}
	}
	std::abort();
}

}
