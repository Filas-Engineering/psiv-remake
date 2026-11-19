#include "game/data.h"

#include "events/diagnostics.h"

namespace game {

const rules::TextTable& problemText() {
	static const rules::TextTable table = [] {
		rules::TextTable t = {
			{"data.actor_is_character", "actor '{0}' is a character; characters are actors already"},
			{"data.area_object", "an area is an object"},
			{"data.bad_area_kind", "kind must be town, field or dungeon"},
			{"data.bad_cell", "a cell is written [x, y]"},
			{"data.bad_encounter_mode", "encounter_mode must be visible or random"},
			{"data.bad_facing", "facing must be north, east, south or west"},
			{"data.bad_map_character", "map rows use '.' for floor, '#' for wall and '~' for water"},
			{"data.bad_trigger", "trigger must be interact, step, load or flag"},
			{"data.cannot_read", "cannot read {0}"},
			{"data.chest_holds_unknown_equipment", "chest '{0}' holds unknown equipment '{1}'"},
			{"data.chest_holds_unknown_item", "chest '{0}' holds unknown item '{1}'"},
			{"data.count_out_of_range", "'{0}.{1}' must be a whole number from {2} to {3}"},
			{"data.declared_as_flag_and_variable", "'{0}' is declared as a flag and a variable"},
			{"data.duplicate_area_id", "duplicate area id '{0}'"},
			{"data.exit_leads_missing_marker", "exit leads to missing marker '{0} {1}'"},
			{"data.field_cannot_empty", "field '{0}' cannot be empty"},
			{"data.field_out_of_range", "field '{0}' must be a whole number from {1} to {2}"},
			{"data.field_wrong_type", "field '{0}' must be a {1}"},
			{"data.flag_declared_twice", "flag '{0}' is declared twice"},
			{"data.flag_hook_needs_block_flag", "flag hook '{0}' needs a block_flag, or it would fire forever"},
			{"data.flag_not_declared", "flag '{0}' is not declared"},
			{"data.holds_text_only", "'{0}' holds text only"},
			{"data.hook_runs_unknown_event", "hook '{0}' runs unknown event '{1}'"},
			{"data.key_already_in_core", "key '{0}' is already in core's text table"},
			{"data.key_missing_from_table", "key '{0}' of the '{1}' table is missing"},
			{"data.key_not_in_source_table", "key '{0}' is not in the '{1}' table"},
			{"data.language_listed_twice", "language '{0}' is listed twice"},
			{"data.list_at_least_one_language", "list at least one language"},
			{"data.map_rows_differ", "every map row must have the same width"},
			{"data.max_steps_below_min_steps", "max_steps is below min_steps"},
			{"data.missing_field", "missing field '{0}'"},
			{"data.missing_message_key", "missing message key '{0}'"},
			{"data.not_on_floor", "{0} is not on a floor cell"},
			{"data.opening_percents_over_100", "opening percents add up to more than 100"},
			{"data.rest_point_id_taken", "rest point id '{0}' is used by another rest point"},
			{"data.scripted_end_enemy_not_in_group", "the scripted end names enemy '{0}', which is not in encounter '{1}'"},
			{"data.shop_id_taken", "shop id '{0}' is used by another shop"},
			{"data.shop_stock_unknown", "shop '{0}' stocks '{1}', which is unknown or has no price"},
			{"data.spawn_outside_zone", "a spawn point of zone '{0}' is outside the zone"},
			{"data.speaker_not_character", "speaker '{0}' is not a character"},
			{"data.start_lineup_size", "the starting lineup needs 1 to 5 members"},
			{"data.start_marker_missing", "start marker '{0} {1}' does not exist"},
			{"data.string_not_in_dialogue_table", "string '{0}' is not in the dialogue table"},
			{"data.table_entry_object", "a table entry is an object"},
			{"data.table_language_mismatch", "the table is '{0}', but game.json lists it for '{1}'"},
			{"data.talk_set_needs_line", "party talk set '{0}' needs at least one line"},
			{"data.text_not_in_table", "text '{0}' is not in the text table"},
			{"data.unknown_area", "unknown area '{0}'"},
			{"data.unknown_boss_encounter", "unknown boss encounter '{0}'"},
			{"data.unknown_character_join_levels", "unknown character '{0}' in join_levels"},
			{"data.unknown_character_starting_lineup", "unknown character '{0}' in the starting lineup"},
			{"data.unknown_field", "unknown field '{0}'"},
			{"data.unknown_item_prices", "unknown item '{0}' in prices"},
			{"data.unknown_item_starting_items", "unknown item '{0}' in the starting items"},
			{"data.zone_lists_unknown_encounter", "zone '{0}' lists unknown encounter '{1}'"},
			{"data.zone_needs_table_entry", "zone '{0}' needs at least one table entry"},
		};
		t.insert(rules::problemText().begin(), rules::problemText().end());
		t.insert(events::diagnostic_text().begin(), events::diagnostic_text().end());
		return t;
	}();
	return table;
}

}
